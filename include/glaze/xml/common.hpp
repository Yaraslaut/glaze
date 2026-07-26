// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "glaze/core/buffer_traits.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/xml/opts.hpp"

namespace glz::xml
{
   struct cp_range final
   {
      char32_t lo{};
      char32_t hi{};
   };

   // XML 1.0 5th ed. [4] NameStartChar, minus ':' which is handled by the
   // Name/NCName distinction rather than by the table.
   inline constexpr std::array<cp_range, 15> name_start_ranges{{
      {U'A', U'Z'},
      {U'_', U'_'},
      {U'a', U'z'},
      {0xC0, 0xD6},
      {0xD8, 0xF6},
      {0xF8, 0x2FF},
      {0x370, 0x37D},
      {0x37F, 0x1FFF},
      {0x200C, 0x200D},
      {0x2070, 0x218F},
      {0x2C00, 0x2FEF},
      {0x3001, 0xD7FF},
      {0xF900, 0xFDCF},
      {0xFDF0, 0xFFFD},
      {0x10000, 0xEFFFF},
   }};

   // XML 1.0 5th ed. [4a] NameChar adds these on top of NameStartChar.
   // One sorted, disjoint table so a single binary search covers all of them.
   inline constexpr std::array<cp_range, 5> name_char_extra_ranges{{
      {U'-', U'.'}, // '-' is 0x2D, '.' is 0x2E — contiguous
      {U'0', U'9'},
      {0xB7, 0xB7},
      {0x300, 0x36F},
      {0x203F, 0x2040},
   }};

   template <size_t N>
   constexpr bool in_ranges(const std::array<cp_range, N>& ranges, char32_t cp) noexcept
   {
      // Ranges are sorted and disjoint, so binary search is valid.
      size_t lo = 0;
      size_t hi = N;
      while (lo < hi) {
         const size_t mid = lo + (hi - lo) / 2;
         if (cp < ranges[mid].lo) {
            hi = mid;
         }
         else if (cp > ranges[mid].hi) {
            lo = mid + 1;
         }
         else {
            return true;
         }
      }
      return false;
   }

   constexpr bool is_name_start_char(char32_t cp) noexcept { return cp == U':' || in_ranges(name_start_ranges, cp); }

   constexpr bool is_name_char(char32_t cp) noexcept
   {
      return is_name_start_char(cp) || in_ranges(name_char_extra_ranges, cp);
   }

   // XML 1.0 [2] Char — the set of code points that may appear anywhere in a
   // document. Excludes most C0 controls, surrogates, and the two noncharacters
   // at the end of the BMP.
   constexpr bool is_xml_char(char32_t cp) noexcept
   {
      return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
             (cp >= 0x10000 && cp <= 0x10FFFF);
   }

   constexpr bool is_whitespace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

   // Decodes one UTF-8 scalar. Returns the number of bytes consumed, or 0 if
   // the sequence is malformed, overlong, an encoded surrogate, or out of range.
   constexpr size_t decode_utf8(const char* it, const char* end, char32_t& cp) noexcept
   {
      if (it >= end) return 0;

      const auto b0 = static_cast<unsigned char>(*it);

      if (b0 < 0x80) {
         cp = b0;
         return 1;
      }

      const auto cont = [&](size_t i) -> bool {
         return (it + i) < end && (static_cast<unsigned char>(it[i]) & 0xC0) == 0x80;
      };
      const auto val = [&](size_t i) -> char32_t {
         return static_cast<char32_t>(static_cast<unsigned char>(it[i]) & 0x3F);
      };

      if ((b0 & 0xE0) == 0xC0) {
         if (!cont(1)) return 0;
         cp = (char32_t(b0 & 0x1F) << 6) | val(1);
         return cp < 0x80 ? 0 : 2; // reject overlong
      }
      if ((b0 & 0xF0) == 0xE0) {
         if (!cont(1) || !cont(2)) return 0;
         cp = (char32_t(b0 & 0x0F) << 12) | (val(1) << 6) | val(2);
         if (cp < 0x800) return 0; // overlong
         if (cp >= 0xD800 && cp <= 0xDFFF) return 0; // encoded surrogate
         return 3;
      }
      if ((b0 & 0xF8) == 0xF0) {
         if (!cont(1) || !cont(2) || !cont(3)) return 0;
         cp = (char32_t(b0 & 0x07) << 18) | (val(1) << 12) | (val(2) << 6) | val(3);
         if (cp < 0x10000 || cp > 0x10FFFF) return 0;
         return 4;
      }
      return 0;
   }

   // [5] Name ::= NameStartChar (NameChar)*
   constexpr bool validate_name(std::string_view s) noexcept
   {
      if (s.empty()) return false;

      const char* it = s.data();
      const char* const end = it + s.size();

      char32_t cp{};
      size_t n = decode_utf8(it, end, cp);
      if (n == 0 || !is_name_start_char(cp)) return false;
      it += n;

      while (it < end) {
         n = decode_utf8(it, end, cp);
         if (n == 0 || !is_name_char(cp)) return false;
         it += n;
      }
      return true;
   }

   // Namespaces in XML 1.0 [4] NCName ::= Name - (Char* ':' Char*)
   constexpr bool validate_ncname(std::string_view s) noexcept
   {
      return validate_name(s) && s.find(':') == std::string_view::npos;
   }

   // Member-key sigils that partition a reflected object's members at compile
   // time into: attribute ("@name"), text content ("#text"), and child element
   // (everything else).
   constexpr bool is_attribute_key(std::string_view key) noexcept { return !key.empty() && key.front() == '@'; }

   constexpr bool is_text_key(std::string_view key) noexcept { return key == "#text"; }

   constexpr std::string_view strip_sigil(std::string_view key) noexcept
   {
      return is_attribute_key(key) ? key.substr(1) : key;
   }

   // Encodes one scalar as UTF-8. Returns bytes written, or 0 if cp is not a
   // character XML permits.
   constexpr size_t encode_utf8(char32_t cp, char* out) noexcept
   {
      if (!is_xml_char(cp)) return 0;

      if (cp < 0x80) {
         out[0] = static_cast<char>(cp);
         return 1;
      }
      if (cp < 0x800) {
         out[0] = static_cast<char>(0xC0 | (cp >> 6));
         out[1] = static_cast<char>(0x80 | (cp & 0x3F));
         return 2;
      }
      if (cp < 0x10000) {
         out[0] = static_cast<char>(0xE0 | (cp >> 12));
         out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
         out[2] = static_cast<char>(0x80 | (cp & 0x3F));
         return 3;
      }
      out[0] = static_cast<char>(0xF0 | (cp >> 18));
      out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out[3] = static_cast<char>(0x80 | (cp & 0x3F));
      return 4;
   }

   // Decodes a reference beginning at '*it'. On success advances it past the
   // terminating ';' and appends the replacement text to out.
   //
   // Only the five predefined entities and numeric character references are
   // recognised. Any other general entity is undeclared as far as this parser
   // is concerned (we skip rather than expand the internal DTD subset), and is
   // rejected rather than silently passed through.
   inline bool decode_reference(const char*& it, const char* end, std::string& out) noexcept
   {
      if (it >= end || *it != '&') return false;

      const char* p = it + 1;
      if (p >= end) return false;

      if (*p == '#') {
         ++p;
         if (p >= end) return false;

         int base = 10;
         // Lowercase 'x' ONLY. XML 1.0 [66] CharRef is
         //   '&#' [0-9]+ ';' | '&#x' [0-9a-fA-F]+ ';'
         // so "&#X41;" is not well-formed. W3C case not-wf-sa-093 tests exactly
         // this. Accepting 'X' here silently admits malformed documents.
         if (*p == 'x') {
            base = 16;
            ++p;
         }

         const char* digits_begin = p;
         uint64_t cp = 0;
         constexpr uint64_t overflow_guard = 0x110000; // clamp; anything above is invalid anyway

         while (p < end && *p != ';') {
            uint64_t d;
            const char c = *p;
            if (c >= '0' && c <= '9') {
               d = uint64_t(c - '0');
            }
            else if (base == 16 && c >= 'a' && c <= 'f') {
               d = uint64_t(c - 'a' + 10);
            }
            else if (base == 16 && c >= 'A' && c <= 'F') {
               d = uint64_t(c - 'A' + 10);
            }
            else {
               return false; // non-digit inside the reference
            }
            // Saturate instead of wrapping so huge inputs stay invalid.
            if (cp <= overflow_guard) {
               cp = cp * uint64_t(base) + d;
            }
            ++p;
         }

         if (p >= end || p == digits_begin) return false; // no ';' or no digits

         char buf[4]{};
         const size_t n = cp > overflow_guard ? 0 : encode_utf8(char32_t(cp), buf);
         if (n == 0) return false; // not a legal XML character

         out.append(buf, n);
         it = p + 1;
         return true;
      }

      // Named entity: read up to ';'.
      const char* name_begin = p;
      while (p < end && *p != ';') {
         ++p;
      }
      if (p >= end) return false; // unterminated

      const std::string_view name{name_begin, size_t(p - name_begin)};

      char replacement = '\0';
      if (name == "amp") {
         replacement = '&';
      }
      else if (name == "lt") {
         replacement = '<';
      }
      else if (name == "gt") {
         replacement = '>';
      }
      else if (name == "quot") {
         replacement = '"';
      }
      else if (name == "apos") {
         replacement = '\'';
      }
      else {
         return false; // undeclared general entity
      }

      out.push_back(replacement);
      it = p + 1;
      return true;
   }

   namespace detail
   {
      // Grows the buffer and appends, matching the dump/append pattern used by
      // the other Glaze writers. Routes growth through ensure_space so bounded
      // buffers (std::array, std::span) report buffer_overflow instead of
      // being resized unconditionally, which does not compile for them.
      template <class B>
      void append_raw(std::string_view s, is_context auto& ctx, B& b, auto& ix)
      {
         if (!ensure_space(ctx, b, ix + s.size())) [[unlikely]] {
            return;
         }
         std::memcpy(b.data() + ix, s.data(), s.size());
         ix += s.size();
      }

      // Prettify only: '\n' followed by indentation_width * ctx.indent_depth spaces.
      // Same ensure_space growth pattern as append_raw above.
      template <auto Opts, class B>
      void append_indent(is_context auto& ctx, B& b, auto& ix)
      {
         const size_t n = size_t(check_indentation_width(Opts)) * ctx.indent_depth;
         if (!ensure_space(ctx, b, ix + 1 + n)) [[unlikely]] {
            return;
         }
         *(b.data() + ix) = '\n';
         ++ix;
         if (n) {
            std::memset(b.data() + ix, ' ', n);
            ix += n;
         }
      }
   }

   // Escapes character data. '>' is escaped unconditionally, which is stricter
   // than required but guarantees ']]>' can never appear literally.
   template <class B>
   void escape_text(std::string_view s, is_context auto& ctx, B& b, auto& ix)
   {
      for (const char c : s) {
         switch (c) {
         case '&':
            detail::append_raw("&amp;", ctx, b, ix);
            break;
         case '<':
            detail::append_raw("&lt;", ctx, b, ix);
            break;
         case '>':
            detail::append_raw("&gt;", ctx, b, ix);
            break;
         default:
            detail::append_raw(std::string_view{&c, 1}, ctx, b, ix);
            break;
         }
      }
   }

   // Escapes an attribute value. Additionally escapes '"' (the quote we emit)
   // and the three whitespace characters, because attribute-value
   // normalization would otherwise turn them into spaces on re-read.
   template <class B>
   void escape_attribute(std::string_view s, is_context auto& ctx, B& b, auto& ix)
   {
      for (const char c : s) {
         switch (c) {
         case '&':
            detail::append_raw("&amp;", ctx, b, ix);
            break;
         case '<':
            detail::append_raw("&lt;", ctx, b, ix);
            break;
         case '>':
            detail::append_raw("&gt;", ctx, b, ix);
            break;
         case '"':
            detail::append_raw("&quot;", ctx, b, ix);
            break;
         case '\t':
            detail::append_raw("&#x9;", ctx, b, ix);
            break;
         case '\n':
            detail::append_raw("&#xA;", ctx, b, ix);
            break;
         case '\r':
            detail::append_raw("&#xD;", ctx, b, ix);
            break;
         default:
            detail::append_raw(std::string_view{&c, 1}, ctx, b, ix);
            break;
         }
      }
   }

   struct ns_binding final
   {
      std::string prefix{};
      std::string uri{};
   };

   struct xml_context : context
   {
      // Names of currently open elements, outermost first. Owned copies,
      // because the input buffer may not outlive parsing in the streaming case.
      std::vector<std::string> element_stack{};

      // Flat stack of prefix bindings paired with scope markers. ns_scope_marks
      // records the size of ns_bindings when each scope was opened, so closing a
      // scope is a single truncation.
      std::vector<ns_binding> ns_bindings{};
      std::vector<size_t> ns_scope_marks{};

      // NOT named depth(): glz::context has a `uint32_t depth` FIELD, and a
      // derived member function of the same name hides it, which breaks both
      // glz::is_context<xml_context> and the shared glz::depth_guard used by
      // every other format reader.
      size_t element_depth() const noexcept { return element_stack.size(); }

      // Prettify (writer only). indent_depth is the current nesting depth used
      // to compute how many spaces precede a child element -- write_wrapped_element
      // increments it for the duration of a value's body, so indentation_width *
      // indent_depth is always the depth of whatever is being written right now.
      //
      // wrote_element_child is a single-frame accumulator: the loop sites that
      // emit child elements (the object writer's element pass, write_sequence,
      // and the map writer) set it to true immediately before writing each child.
      // write_wrapped_element saves/resets it before writing a value's body and
      // reads it back after, to decide whether that value's body consisted of
      // child elements (indent before the closing tag) or was plain text/scalar
      // content (no added whitespace, so text values are never altered).
      size_t indent_depth = 0;
      bool wrote_element_child = false;

      std::string_view current_element() const noexcept
      {
         return element_stack.empty() ? std::string_view{} : std::string_view{element_stack.back()};
      }

      bool push_element(std::string_view name) noexcept
      {
         if (element_stack.size() >= max_recursive_depth_limit) [[unlikely]] {
            error = error_code::exceeded_max_recursive_depth;
            return false;
         }
         element_stack.emplace_back(name);
         return true;
      }

      bool pop_element(std::string_view name) noexcept
      {
         if (element_stack.empty()) [[unlikely]] {
            error = error_code::syntax_error;
            return false;
         }
         if (element_stack.back() != name) [[unlikely]] {
            error = error_code::syntax_error;
            return false;
         }
         element_stack.pop_back();
         return true;
      }

      void push_ns_scope() noexcept { ns_scope_marks.push_back(ns_bindings.size()); }

      void pop_ns_scope() noexcept
      {
         if (ns_scope_marks.empty()) return;
         ns_bindings.resize(ns_scope_marks.back());
         ns_scope_marks.pop_back();
      }

      bool bind_prefix(std::string_view prefix, std::string_view uri) noexcept
      {
         ns_bindings.emplace_back(ns_binding{std::string{prefix}, std::string{uri}});
         return true;
      }

      // Innermost binding wins, so the search runs back to front.
      std::string_view resolve_prefix(std::string_view prefix) const noexcept
      {
         for (auto it = ns_bindings.rbegin(); it != ns_bindings.rend(); ++it) {
            if (it->prefix == prefix) {
               return it->uri;
            }
         }
         return {};
      }
   };

   // ---------------------------------------------------------------------
   // Scanning primitives
   //
   // These are the low-level, non-reflective XML grammar productions (S,
   // Comment, PI, XMLDecl, STag/ETag, AttValue, CharData, CDSect) plus
   // skip_element, the "consume and discard one element subtree" algorithm
   // shared by glz::skip_value<XML> (xml/skip.hpp) and, indirectly through
   // it, xml/read.hpp's object reader. They live here rather than in
   // read.hpp so that skip.hpp -- which only ever needs this layer, never
   // the reflection-based from<XML, T> machinery -- can depend on common.hpp
   // alone instead of pulling in all of read.hpp (and transitively
   // json/read.hpp and file/file_ops.hpp).
   // ---------------------------------------------------------------------

   struct attribute
   {
      std::string name;
      std::string value;
   };

   struct start_tag
   {
      std::string name;
      std::vector<attribute> attributes;
      bool self_closing{};
   };

   // Advances past any run of XML whitespace (S ::= (#x20 | #x9 | #xD | #xA)+).
   inline void skip_whitespace(const char*& it, const char* end) noexcept
   {
      while (it < end && is_whitespace(*it)) {
         ++it;
      }
   }

   // Advances past a leading UTF-8 byte order mark, if present. Always
   // succeeds -- absence of a BOM is not an error.
   inline bool skip_bom(const char*& it, const char* end) noexcept
   {
      constexpr std::string_view bom = "\xEF\xBB\xBF";
      if (size_t(end - it) >= bom.size() && std::string_view{it, bom.size()} == bom) {
         it += bom.size();
      }
      return true;
   }

   // Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
   // i.e. '--' may not occur anywhere in the body, and therefore a comment may
   // not end in '--->' either (the extra '-' would form a forbidden '--' pair
   // with the one before it).
   inline bool parse_comment(const char*& it, const char* end, xml_context& ctx) noexcept
   {
      constexpr std::string_view open = "<!--";
      if (size_t(end - it) < open.size() || std::string_view{it, open.size()} != open) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      const char* p = it + open.size();
      while (p < end) {
         if (*p == '-' && (p + 1) < end && p[1] == '-') {
            if ((p + 2) >= end) {
               ctx.error = error_code::unexpected_end;
               return false;
            }
            if (p[2] == '>') {
               it = p + 3;
               return true;
            }
            ctx.error = error_code::syntax_error; // '--' not immediately followed by '>'
            return false;
         }
         ++p;
      }
      ctx.error = error_code::unexpected_end;
      return false;
   }

   // PI ::= '<?' PITarget (S (Char* - (Char* '?>' Char*)))? '?>'
   // PITarget ::= Name - (('X' | 'x') ('M' | 'm') ('L' | 'l'))
   inline bool parse_pi(const char*& it, const char* end, xml_context& ctx) noexcept
   {
      constexpr std::string_view open = "<?";
      if (size_t(end - it) < open.size() || std::string_view{it, open.size()} != open) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      const char* p = it + open.size();
      const char* const target_begin = p;
      while (p < end) {
         char32_t cp{};
         const size_t n = decode_utf8(p, end, cp);
         if (n == 0 || !is_name_char(cp)) break;
         p += n;
      }

      const std::string_view target{target_begin, size_t(p - target_begin)};
      if (!validate_name(target)) {
         ctx.error = error_code::syntax_error;
         return false;
      }
      if (target.size() == 3 && (target[0] == 'x' || target[0] == 'X') && (target[1] == 'm' || target[1] == 'M') &&
          (target[2] == 'l' || target[2] == 'L')) {
         ctx.error = error_code::syntax_error; // 'xml' (any case) is a reserved PI target
         return false;
      }

      if (p >= end) {
         ctx.error = error_code::unexpected_end;
         return false;
      }
      if (*p != '?') {
         if (!is_whitespace(*p)) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         skip_whitespace(p, end);
      }

      while (true) {
         if (p >= end) {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         if (*p == '?' && (p + 1) < end && p[1] == '>') {
            it = p + 2;
            return true;
         }
         ++p;
      }
   }

   // XMLDecl ::= '<?xml' VersionInfo EncodingDecl? SDDecl? S? '?>'
   // The three pseudo-attributes, when present, must appear in exactly this
   // order; only 'version' is mandatory.
   inline bool parse_xml_declaration(const char*& it, const char* end, xml_context& ctx) noexcept
   {
      constexpr std::string_view open = "<?xml";
      if (size_t(end - it) < open.size() || std::string_view{it, open.size()} != open) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      const char* p = it + open.size();
      if (p >= end || !is_whitespace(*p)) {
         ctx.error = error_code::syntax_error;
         return false;
      }
      skip_whitespace(p, end);

      const auto starts_with = [&](std::string_view lit) noexcept -> bool {
         return size_t(end - p) >= lit.size() && std::string_view{p, lit.size()} == lit;
      };

      const auto parse_eq = [&]() noexcept -> bool {
         skip_whitespace(p, end);
         if (p >= end || *p != '=') {
            ctx.error = error_code::syntax_error;
            return false;
         }
         ++p;
         skip_whitespace(p, end);
         return true;
      };

      const auto parse_quoted = [&](std::string_view& value) noexcept -> bool {
         if (p >= end || (*p != '"' && *p != '\'')) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         const char quote = *p;
         ++p;
         const char* const value_begin = p;
         while (p < end && *p != quote) {
            ++p;
         }
         if (p >= end) {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         value = std::string_view{value_begin, size_t(p - value_begin)};
         ++p; // past closing quote
         return true;
      };

      // VersionInfo -- mandatory, must be the first pseudo-attribute.
      constexpr std::string_view version_lit = "version";
      if (!starts_with(version_lit)) {
         ctx.error = error_code::syntax_error;
         return false;
      }
      p += version_lit.size();
      if (!parse_eq()) return false;
      std::string_view version;
      if (!parse_quoted(version)) return false;
      if (version != "1.0" && version != "1.1") {
         ctx.error = error_code::syntax_error;
         return false;
      }
      skip_whitespace(p, end);

      // EncodingDecl -- optional. EncName ::= [A-Za-z] ([A-Za-z0-9._] | '-')*
      constexpr std::string_view encoding_lit = "encoding";
      if (starts_with(encoding_lit)) {
         p += encoding_lit.size();
         if (!parse_eq()) return false;
         std::string_view encoding;
         if (!parse_quoted(encoding)) return false;

         const auto is_alpha = [](char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
         if (encoding.empty() || !is_alpha(encoding[0])) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         for (size_t i = 1; i < encoding.size(); ++i) {
            const char c = encoding[i];
            const bool ok = is_alpha(c) || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            if (!ok) {
               ctx.error = error_code::syntax_error;
               return false;
            }
         }
         skip_whitespace(p, end);
      }

      // SDDecl -- optional, must follow EncodingDecl (if present).
      constexpr std::string_view standalone_lit = "standalone";
      if (starts_with(standalone_lit)) {
         p += standalone_lit.size();
         if (!parse_eq()) return false;
         std::string_view standalone;
         if (!parse_quoted(standalone)) return false;
         if (standalone != "yes" && standalone != "no") {
            ctx.error = error_code::syntax_error;
            return false;
         }
         skip_whitespace(p, end);
      }

      if (!starts_with("?>")) {
         ctx.error = error_code::syntax_error;
         return false;
      }
      p += 2;
      it = p;
      return true;
   }

   // Walks the buffer with decode_utf8 and reports the byte offset of the
   // first malformed sequence.
   inline bool validate_utf8(std::string_view s, size_t& bad_offset) noexcept
   {
      const char* it = s.data();
      const char* const end = it + s.size();
      while (it < end) {
         char32_t cp{};
         const size_t n = decode_utf8(it, end, cp);
         if (n == 0) {
            bad_offset = size_t(it - s.data());
            return false;
         }
         it += n;
      }
      return true;
   }

   // Prolog ::= XMLDecl? Misc* (doctypedecl Misc*)?
   // where Misc ::= Comment | PI | S. The internal DTD subset (doctypedecl) is
   // added by a later task; for now parse_prolog simply stops at whatever '<'
   // follows, leaving it positioned there.
   //
   // parse_pi and parse_xml_declaration both start with '<?', so the
   // declaration is only ever attempted here, at offset zero -- never inside
   // the Misc* loop below.
   inline bool parse_prolog(const char*& it, const char* end, xml_context& ctx) noexcept
   {
      skip_bom(it, end);

      constexpr std::string_view xml_decl_open = "<?xml";
      if (size_t(end - it) >= xml_decl_open.size() && std::string_view{it, xml_decl_open.size()} == xml_decl_open &&
          (it + xml_decl_open.size()) < end && is_whitespace(it[xml_decl_open.size()])) {
         if (!parse_xml_declaration(it, end, ctx)) {
            return false;
         }
      }

      while (it < end) {
         if (is_whitespace(*it)) {
            skip_whitespace(it, end);
            continue;
         }
         if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
            if (!parse_comment(it, end, ctx)) return false;
            continue;
         }
         if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
            if (!parse_pi(it, end, ctx)) return false;
            continue;
         }
         break; // the document element (or, later, a doctypedecl) starts here
      }
      return true;
   }

   namespace detail
   {
      // Reads a maximal run of NameChar starting at 'p'. Does not itself verify
      // that the result is a well-formed Name (i.e. that the first character is
      // a NameStartChar) -- callers pass the result through validate_name.
      inline std::string_view read_name_span(const char*& p, const char* end) noexcept
      {
         const char* const begin = p;
         while (p < end) {
            char32_t cp{};
            const size_t n = decode_utf8(p, end, cp);
            if (n == 0 || !is_name_char(cp)) break;
            p += n;
         }
         return std::string_view{begin, size_t(p - begin)};
      }
   }

   // AttValue ::= '"' ([^<&"] | Reference)* '"' | "'" ([^<&'] | Reference)* "'"
   // XML 1.0 3.3.3 attribute-value normalization: a literal tab, LF, or CR is
   // replaced by a single space (a literal CRLF collapses to one space, not
   // two); a character reference to one of those same characters is NOT
   // normalized -- it is inserted verbatim by decode_reference below.
   inline bool parse_attribute_value(const char*& it, const char* end, xml_context& ctx, std::string& out) noexcept
   {
      if (it >= end || (*it != '"' && *it != '\'')) {
         ctx.error = error_code::syntax_error;
         return false;
      }
      const char quote = *it;
      const char* p = it + 1;
      out.clear();

      while (true) {
         if (p >= end) {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         const char c = *p;
         if (c == quote) {
            ++p;
            it = p;
            return true;
         }
         if (c == '<') {
            ctx.error = error_code::syntax_error;
            return false;
         }
         if (c == '&') {
            if (!decode_reference(p, end, out)) {
               ctx.error = error_code::syntax_error;
               return false;
            }
            continue;
         }
         if (c == '\r') {
            ++p;
            if (p < end && *p == '\n') {
               ++p;
            }
            out.push_back(' ');
            continue;
         }
         if (c == '\n' || c == '\t') {
            out.push_back(' ');
            ++p;
            continue;
         }
         out.push_back(c);
         ++p;
      }
   }

   // STag ::= '<' Name (S Attribute)* S? '>' | EmptyElemTag ::= '<' Name (S Attribute)* S? '/>'
   // Attribute ::= Name Eq AttValue
   //
   // Each Attribute must be preceded by whitespace separating it from the
   // element name or the previous attribute -- '<book id="7"role="x">' is not
   // well-formed even though both attributes are individually valid.
   inline bool parse_start_tag(const char*& it, const char* end, xml_context& ctx, start_tag& out) noexcept
   {
      if (it >= end || *it != '<') {
         ctx.error = error_code::syntax_error;
         return false;
      }
      const char* p = it + 1;
      const std::string_view name = detail::read_name_span(p, end);
      if (!validate_name(name)) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      out.name = name;
      out.attributes.clear();
      out.self_closing = false;

      while (true) {
         const char* const before_ws = p;
         skip_whitespace(p, end);
         const bool had_whitespace = p != before_ws;

         if (p >= end) {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         if (*p == '>') {
            ++p;
            break;
         }
         if (*p == '/') {
            ++p;
            if (p >= end || *p != '>') {
               ctx.error = error_code::syntax_error;
               return false;
            }
            ++p;
            out.self_closing = true;
            break;
         }

         if (!had_whitespace) {
            ctx.error = error_code::syntax_error; // no S separating this attribute from the previous token
            return false;
         }

         const std::string_view attr_name = detail::read_name_span(p, end);
         if (!validate_name(attr_name)) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         for (const auto& existing : out.attributes) {
            if (existing.name == attr_name) {
               ctx.error = error_code::duplicate_key;
               return false;
            }
         }

         skip_whitespace(p, end);
         if (p >= end || *p != '=') {
            ctx.error = error_code::syntax_error;
            return false;
         }
         ++p;
         skip_whitespace(p, end);

         std::string value;
         if (!parse_attribute_value(p, end, ctx, value)) {
            return false;
         }

         out.attributes.emplace_back(attribute{std::string{attr_name}, std::move(value)});
      }

      it = p;
      if (!out.self_closing) {
         if (!ctx.push_element(out.name)) {
            return false;
         }
      }
      return true;
   }

   // ETag ::= '</' Name S? '>'
   // Neither attributes nor a trailing '/' are permitted here; both fall out
   // naturally because anything other than whitespace before '>' is rejected.
   inline bool parse_end_tag(const char*& it, const char* end, xml_context& ctx, std::string& name) noexcept
   {
      if (size_t(end - it) < 2 || it[0] != '<' || it[1] != '/') {
         ctx.error = error_code::syntax_error;
         return false;
      }
      const char* p = it + 2;
      const std::string_view name_view = detail::read_name_span(p, end);
      if (!validate_name(name_view)) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      skip_whitespace(p, end);
      if (p >= end || *p != '>') {
         ctx.error = error_code::syntax_error;
         return false;
      }
      ++p;

      if (!ctx.pop_element(name_view)) {
         return false;
      }

      name = name_view;
      it = p;
      return true;
   }

   // Opening delimiter of a CDATA section (CDSect ::= '<![CDATA[' CData ']]>'),
   // shared by every site that must recognise one: parse_cdata (which parses
   // the whole section), parse_char_data (which must know when a leading '<'
   // opens a CDATA section rather than markup), and any dispatch site that
   // decides between element-dispatch and character data.
   inline constexpr std::string_view cdata_open_tag = "<![CDATA[";

   // CData ::= (Char* - (Char* ']]>' Char*))
   // CDSect ::= '<![CDATA[' CData ']]>'
   //
   // Bytes are copied verbatim -- no reference expansion, and ']]' is only
   // forbidden immediately before '>'. Per XML 1.0 2.11, line-ending
   // normalization (a literal CRLF or lone CR becomes LF) is a preprocessing
   // step applied across the whole document before parsing, so it still
   // applies here even though CDATA otherwise suppresses markup recognition.
   inline bool parse_cdata(const char*& it, const char* end, xml_context& ctx, std::string& out) noexcept
   {
      constexpr std::string_view open = cdata_open_tag;
      if (size_t(end - it) < open.size() || std::string_view{it, open.size()} != open) {
         ctx.error = error_code::syntax_error;
         return false;
      }

      const char* p = it + open.size();
      while (true) {
         if (p >= end) {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         if (*p == ']' && (p + 2) < end && p[1] == ']' && p[2] == '>') {
            it = p + 3;
            return true;
         }
         if (*p == '\r') {
            out.push_back('\n');
            ++p;
            if (p < end && *p == '\n') {
               ++p;
            }
            continue;
         }

         char32_t cp{};
         const size_t n = decode_utf8(p, end, cp);
         if (n == 0 || !is_xml_char(cp)) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         out.append(p, n);
         p += n;
      }
   }

   // CharData ::= [^<&]* - ([^<&]* ']]>' [^<&]*)
   // content ::= CharData? ((element | Reference | CDSect | PI | Comment) CharData?)*
   //
   // Handles the subset relevant to a text node: CDATA sections merge
   // seamlessly into the surrounding text, references expand, and a literal
   // line ending normalizes (XML 1.0 2.11). Normalization applies only to
   // literal bytes -- a character reference such as '&#xD;' names an actual
   // carriage return and must survive untouched, mirroring the attribute-value
   // asymmetry in parse_attribute_value above (decode_reference writes
   // straight into 'out', bypassing the '\r' handling below). Stops, without
   // consuming, at any '<' that does not open a CDATA section, leaving the
   // caller to dispatch on tag, comment, PI, etc.
   inline bool parse_char_data(const char*& it, const char* end, xml_context& ctx, std::string& out) noexcept
   {
      out.clear();

      while (it < end) {
         const char c = *it;

         if (c == '<') {
            if (size_t(end - it) >= cdata_open_tag.size() &&
                std::string_view{it, cdata_open_tag.size()} == cdata_open_tag) {
               if (!parse_cdata(it, end, ctx, out)) {
                  return false;
               }
               continue;
            }
            return true; // markup begins here -- leave it on '<'
         }

         if (c == '&') {
            if (!decode_reference(it, end, out)) {
               ctx.error = error_code::syntax_error;
               return false;
            }
            continue;
         }

         if (c == '\r') {
            out.push_back('\n');
            ++it;
            if (it < end && *it == '\n') {
               ++it;
            }
            continue;
         }

         if (c == ']' && (it + 2) < end && it[1] == ']' && it[2] == '>') {
            ctx.error = error_code::syntax_error; // bare ']]>' forbidden in character data
            return false;
         }

         char32_t cp{};
         const size_t n = decode_utf8(it, end, cp);
         if (n == 0 || !is_xml_char(cp)) {
            ctx.error = error_code::syntax_error;
            return false;
         }
         out.append(it, n);
         it += n;
      }

      return true;
   }

   // Consumes exactly one element subtree -- start tag through matching end
   // tag -- discarding everything read. This is the algorithm behind
   // glz::skip_value<XML> (xml/skip.hpp), which forwards to it directly; the
   // object reader in xml/read.hpp uses skip_value<XML>::op for its own
   // "skip one unknown child" need (error_on_unknown_keys off), matching the
   // pattern every sibling format follows.
   //
   // Recursion depth is already bounded by parse_start_tag's own call to
   // ctx.push_element, which enforces max_recursive_depth_limit before each
   // nested level is entered, so a separate depth_guard here would be
   // redundant.
   inline void skip_element(const char*& it, const char* end, xml_context& ctx) noexcept
   {
      start_tag tag;
      if (!parse_start_tag(it, end, ctx, tag)) {
         return;
      }
      if (tag.self_closing) {
         return;
      }

      while (true) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }

         // Route through parse_char_data unconditionally so that a CDATA
         // section starting the content run (e.g. an unknown element skipped
         // in lenient mode, "<nope><![CDATA[...]]></nope>") is consumed as
         // character data rather than mistaken for a child element and
         // hard-failing the whole document. parse_char_data leaves 'it'
         // untouched, at '<', when that '<' opens something else.
         std::string chunk;
         if (!parse_char_data(it, end, ctx, chunk)) {
            return;
         }

         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         // parse_char_data only ever stops, without consuming, at a '<' that
         // does not open a CDATA section, so 'it' is guaranteed to sit at '<'
         // here.
         if (size_t(end - it) >= 2 && it[1] == '/') {
            std::string name;
            parse_end_tag(it, end, ctx, name);
            return;
         }
         if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
            if (!parse_comment(it, end, ctx)) {
               return;
            }
            continue;
         }
         if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
            if (!parse_pi(it, end, ctx)) {
               return;
            }
            continue;
         }
         skip_element(it, end, ctx);
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }
      }
   }
}

namespace glz
{
   template <>
   struct format_context<XML>
   {
      using type = xml::xml_context;
   };
}
