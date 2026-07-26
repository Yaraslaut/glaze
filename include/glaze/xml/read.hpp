// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/read.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/file/file_ops.hpp"
#include "glaze/json/read.hpp"
#include "glaze/util/bit_array.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/xml/common.hpp"

namespace glz::xml
{
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
      constexpr std::string_view open = "<![CDATA[";
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
      constexpr std::string_view cdata_open = "<![CDATA[";

      while (it < end) {
         const char c = *it;

         if (c == '<') {
            if (size_t(end - it) >= cdata_open.size() && std::string_view{it, cdata_open.size()} == cdata_open) {
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

   // True iff every character in s is XML whitespace. An empty string counts
   // as whitespace-only, so a zero-length run of char data between two tags
   // is discarded the same way "  " would be.
   inline bool is_whitespace_only(std::string_view s) noexcept
   {
      for (const char c : s) {
         if (!is_whitespace(c)) {
            return false;
         }
      }
      return true;
   }

   // Reads a maximal run of NameChar starting right after '<', WITHOUT
   // consuming anything or touching ctx -- a look-ahead the object reader
   // uses to decide which member a child element belongs to before
   // committing to parse_start_tag (which pushes the name onto
   // ctx.element_stack and must only run once dispatch is settled). Returns
   // an empty view if `it` is not at '<'; the caller's subsequent
   // parse_start_tag call is what actually reports a malformed name.
   inline std::string_view peek_element_name(const char* it, const char* end) noexcept
   {
      if (it >= end || *it != '<') {
         return {};
      }
      const char* p = it + 1;
      return detail::read_name_span(p, end);
   }

   // Consumes CharData interleaved with Comment/PI content until the
   // element's closing tag is reached (left unconsumed, at '<', for the
   // caller to parse via parse_end_tag). A nested element is not valid
   // content for a leaf scalar -- there is no member to bind it to -- and is
   // rejected rather than silently absorbed.
   inline bool parse_leaf_content(const char*& it, const char* end, xml_context& ctx, std::string& out) noexcept
   {
      out.clear();
      while (true) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         if (*it != '<') {
            std::string chunk;
            if (!parse_char_data(it, end, ctx, chunk)) {
               return false;
            }
            out += chunk;
            continue;
         }
         if (size_t(end - it) >= 2 && it[1] == '/') {
            return true; // caller parses the end tag
         }
         if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
            if (!parse_comment(it, end, ctx)) {
               return false;
            }
            continue;
         }
         if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
            if (!parse_pi(it, end, ctx)) {
               return false;
            }
            continue;
         }
         ctx.error = error_code::syntax_error; // a scalar leaf cannot contain a child element
         return false;
      }
   }

   // Consumes exactly one element subtree -- start tag through matching end
   // tag -- discarding everything read. Used both for skipping a child
   // element the object reader does not recognise (unknown key with
   // error_on_unknown_keys off) and as the algorithm behind
   // glz::skip_value<XML> (xml/skip.hpp). skip.hpp depends on this file for
   // the scanning primitives above, so this file cannot also depend on
   // skip.hpp without a circular include -- both call sites therefore share
   // this one function directly rather than bouncing through skip_value<XML>.
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
         if (*it == '<') {
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
            continue;
         }
         std::string chunk;
         if (!parse_char_data(it, end, ctx, chunk)) {
            return;
         }
      }
   }
}

namespace glz::xml::detail
{
   // A reflected object: the type matched by the object from<XML, T>
   // specialization below. Mirrors xml_reflected_object in xml/write.hpp,
   // kept as an independent concept (rather than shared) so that read.hpp
   // does not have to include write.hpp.
   template <class T>
   concept reflected_object = (glaze_object_t<T> || reflectable<T>) && !custom_read<T>;

   // Compile-time index of the "#text" member, or reflect<T>::size if T has
   // none -- mirrors has_text_member<T>() in xml/write.hpp, but returns the
   // index (not just presence) since the object reader needs to reach the
   // member directly when accumulating character data.
   template <class T>
   consteval size_t text_member_index() noexcept
   {
      for (size_t i = 0; i < reflect<T>::size; ++i) {
         if (is_text_key(reflect<T>::keys[i])) {
            return i;
         }
      }
      return reflect<T>::size;
   }

   // A minimal, JSON-format local opts value used only to delegate scalar
   // text into the JSON number/boolean grammar (see assign_scalar_text
   // below). It is deliberately NOT the caller's own Opts (an
   // xml::xml_opts), which has no `null_terminated` (or other JSON-specific)
   // member the JSON reader requires; null_terminated is false because the
   // text handed in is a temporary std::string slice, not the original
   // document buffer.
   inline constexpr glz::opts json_scalar_opts{.format = JSON, .null_terminated = false};

   // Converts already-extracted, already-unescaped text into a scalar value.
   // Strings are assigned directly. Everything else reuses the JSON reader's
   // own scalar grammar: on the wire, an XML attribute value or element text
   // node is unquoted raw text, which happens to be exactly what a JSON
   // number or boolean literal looks like, so `from<JSON, M>` can read it
   // directly -- this is the "JSON scalar-from-string path" the object
   // reader's attribute binding reuses so `int id` accepts `id="7"`.
   template <class M, class Ctx>
   inline void assign_scalar_text(M& value, Ctx& ctx, std::string_view text) noexcept
   {
      if constexpr (string_t<M>) {
         value = text;
      }
      else if constexpr (boolean_like<M>) {
         if (text == "true" || text == "1") {
            value = true;
         }
         else if (text == "false" || text == "0") {
            value = false;
         }
         else {
            ctx.error = error_code::syntax_error;
         }
      }
      else if constexpr (num_t<M>) {
         // XSD 1.0 lexical space for xs:float / xs:double spells the special
         // values "NaN", "INF" and "-INF" (see xml/write.hpp's num_t
         // to<XML,T>::op) -- recognise them before falling back to the JSON
         // number grammar, which has no such literals.
         if constexpr (std::floating_point<M>) {
            if (text == "NaN") {
               value = std::numeric_limits<M>::quiet_NaN();
               return;
            }
            if (text == "INF") {
               value = std::numeric_limits<M>::infinity();
               return;
            }
            if (text == "-INF") {
               value = -std::numeric_limits<M>::infinity();
               return;
            }
         }
         const char* p = text.data();
         const char* e = p + text.size();
         from<JSON, M>::template op<json_scalar_opts>(value, ctx, p, e);
         // With null_terminated=false, the JSON number reader signals
         // `end_reached` (a benign non-error -- see core/read.hpp's
         // finalize_read_context) rather than `none` when the number runs
         // exactly to the end of the buffer handed to it, which it always
         // does here since `text` contains nothing but the number. Normally
         // finalize_read_context clears that signal once the top-level read
         // finishes; delegating into the JSON reader directly like this
         // bypasses that step, so it is cleared explicitly here instead.
         if (ctx.error == error_code::end_reached) {
            ctx.error = error_code::none;
         }
         if (!bool(ctx.error) && p != e) [[unlikely]] {
            ctx.error = error_code::syntax_error; // trailing characters after the number
         }
      }
      else if constexpr (is_named_enum<M>) {
         constexpr auto NN = reflect<M>::size;
         if constexpr (NN == 1) {
            static constexpr auto key = glz::get<0>(reflect<M>::keys);
            if (text == key) {
               value = glz::get<0>(reflect<M>::values);
            }
            else {
               ctx.error = error_code::unexpected_enum;
            }
         }
         else {
            static constexpr auto HashInfo = hash_info<M>;
            const auto index = decode_hash_with_size<XML, M, HashInfo, HashInfo.type>::op(
               text.data(), text.data() + text.size(), text.size());
            if (index >= NN) [[unlikely]] {
               ctx.error = error_code::unexpected_enum;
               return;
            }
            visit<NN>(
               [&]<size_t I>() {
                  static constexpr auto key = glz::get<I>(reflect<M>::keys);
                  if (text == key) [[likely]] {
                     value = glz::get<I>(reflect<M>::values);
                  }
                  else {
                     ctx.error = error_code::unexpected_enum;
                  }
               },
               index);
         }
      }
      else if constexpr (std::is_enum_v<M>) {
         // Raw enum (without glz::meta) -- underlying numeric representation.
         std::underlying_type_t<M> underlying{};
         assign_scalar_text(underlying, ctx, text);
         value = static_cast<M>(underlying);
      }
      else {
         ctx.error = error_code::syntax_error; // unsupported scalar member type
      }
   }

   // Parses one leaf element -- start tag through matching end tag -- and
   // hands the accumulated body text to `convert`. Shared by every scalar
   // from<XML, T> specialization (bool, numbers, strings, enums) so the tag
   // handling lives in exactly one place. A self-closing element yields an
   // empty text.
   template <auto Opts, class Ctx, class Convert>
   inline void read_leaf_element(Ctx& ctx, const char*& it, const char* end, Convert&& convert)
   {
      xml::start_tag tag;
      if (!xml::parse_start_tag(it, end, ctx, tag)) {
         return;
      }

      std::string text;
      if (!tag.self_closing) {
         if (!xml::parse_leaf_content(it, end, ctx, text)) {
            return;
         }
         std::string end_name;
         if (!xml::parse_end_tag(it, end, ctx, end_name)) {
            return;
         }
      }

      convert(text);
   }
}

namespace glz
{
   template <>
   struct parse<XML>
   {
      template <auto Opts, class T, is_context Ctx, class It0, class It1>
      static void op(T&& value, Ctx&& ctx, It0&& it, It1 end)
      {
         size_t bad_offset = 0;
         if (!xml::validate_utf8(std::string_view{it, size_t(end - it)}, bad_offset)) [[unlikely]] {
            ctx.error = error_code::syntax_error;
            return;
         }

         if (!xml::parse_prolog(it, end, ctx)) {
            return;
         }

         if (it == end) [[unlikely]] {
            // An empty or whitespace/comment/PI-only document has no root element.
            ctx.error = error_code::unexpected_end;
            return;
         }

         from<XML, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx), it, end);
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }

         // Trailer ::= Misc* -- only whitespace, comments, and PIs may follow
         // the document (root) element; anything else, including a second
         // root element, is not well-formed.
         while (it != end) {
            if (xml::is_whitespace(*it)) {
               xml::skip_whitespace(it, end);
               continue;
            }
            if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
               if (!xml::parse_comment(it, end, ctx)) {
                  return;
               }
               continue;
            }
            if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
               if (!xml::parse_pi(it, end, ctx)) {
                  return;
               }
               continue;
            }
            ctx.error = error_code::syntax_error; // trailing content after the root element
            return;
         }
      }
   };

   template <boolean_like T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   template <num_t T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   template <string_t T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Enum with glz::meta/glz::enumerate -- reads the enum's key from element text.
   template <class T>
      requires(is_named_enum<T> && !custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Raw enum (without glz::meta) -- reads as underlying numeric type.
   template <class T>
      requires(std::is_enum_v<T> && !glaze_enum_t<T> && !meta_keys<T> && !custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Reflected object reader: parses attributes into "@name" members, text
   // into a "#text" member (if any), and child elements into everything
   // else. This is the substantial piece of Task 12 -- see the design notes
   // in the task brief and docs/superpowers/plans/2026-07-26-xml-support.md
   // (Task 12) for the exact member-binding rules this implements.
   template <class T>
      requires(xml::detail::reflected_object<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto&& value, is_context auto&& ctx, It&& it, End end)
      {
         using V = std::remove_cvref_t<decltype(value)>;
         static constexpr auto N = reflect<V>::size;
         static constexpr auto HashInfo = hash_info<V>;
         static constexpr auto text_index = xml::detail::text_member_index<V>();
         static constexpr bool has_text = text_index < N;

         xml::start_tag tag;
         if (!xml::parse_start_tag(it, end, ctx, tag)) {
            return;
         }

         decltype(auto) t = [&]() -> decltype(auto) {
            if constexpr (reflectable<V>) {
               return to_tie(value);
            }
            else {
               return nullptr;
            }
         }();

         auto member = [&]<size_t I>() -> decltype(auto) {
            if constexpr (reflectable<V>) {
               return get_member(value, get<I>(t));
            }
            else {
               return get_member(value, get<I>(reflect<V>::values));
            }
         };

         bit_array<N> assigned{};

         // Bind attributes: "@" + attr.name looked up against reflect<V>::keys.
         for (const auto& attr : tag.attributes) {
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }

            std::string key;
            key.reserve(attr.name.size() + 1);
            key += '@';
            key += attr.name;

            const auto index = decode_hash_with_size<XML, V, HashInfo, HashInfo.type>::op(
               key.data(), key.data() + key.size(), key.size());
            const bool key_matches = index < N && key == reflect<V>::keys[index];

            if (key_matches) [[likely]] {
               assigned[index] = true;
               visit<N>(
                  [&]<size_t I>() {
                     xml::detail::assign_scalar_text(member.template operator()<I>(), ctx, attr.value);
                  },
                  index);
            }
            else if constexpr (Opts.error_on_unknown_keys) {
               ctx.error = error_code::unknown_key;
               return;
            }
         }
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }

         const auto check_missing_keys = [&]() {
            if constexpr (Opts.error_on_missing_keys) {
               constexpr auto req_fields = required_fields<V, Opts>();
               if ((req_fields & assigned) != req_fields) {
                  for (size_t i = 0; i < N; ++i) {
                     if (!assigned[i] && req_fields[i]) {
                        ctx.custom_error_message = reflect<V>::keys[i];
                        break;
                     }
                  }
                  ctx.error = error_code::missing_key;
               }
            }
         };

         if (tag.self_closing) {
            check_missing_keys();
            return;
         }

         while (true) {
            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            if (*it == '<') {
               if (size_t(end - it) >= 2 && it[1] == '/') {
                  std::string end_name;
                  if (!xml::parse_end_tag(it, end, ctx, end_name)) {
                     return;
                  }
                  break;
               }
               if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
                  if (!xml::parse_comment(it, end, ctx)) {
                     return;
                  }
                  continue;
               }
               if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
                  if (!xml::parse_pi(it, end, ctx)) {
                     return;
                  }
                  continue;
               }

               const std::string_view child_name = xml::peek_element_name(it, end);
               const auto index = decode_hash_with_size<XML, V, HashInfo, HashInfo.type>::op(
                  child_name.data(), child_name.data() + child_name.size(), child_name.size());
               const bool key_matches = index < N && child_name == reflect<V>::keys[index];

               if (key_matches) [[likely]] {
                  visit<N>(
                     [&]<size_t I>() {
                        using member_t = std::remove_cvref_t<decltype(member.template operator()<I>())>;
                        if constexpr (readable_array_t<member_t> && emplace_backable<member_t>) {
                           // Repeated-sibling convention (mirrors write_sequence in
                           // xml/write.hpp): every occurrence appends one item.
                           // from<XML, item_t> parses exactly one item; it never loops.
                           assigned[I] = true;
                           auto& item = member.template operator()<I>().emplace_back();
                           using item_t = std::remove_cvref_t<decltype(item)>;
                           from<XML, item_t>::template op<Opts>(item, ctx, it, end);
                        }
                        else {
                           if (assigned[I]) {
                              ctx.error = error_code::duplicate_key;
                              return;
                           }
                           assigned[I] = true;
                           from<XML, member_t>::template op<Opts>(member.template operator()<I>(), ctx, it, end);
                        }
                     },
                     index);
                  if (bool(ctx.error)) [[unlikely]] {
                     return;
                  }
               }
               else if constexpr (Opts.error_on_unknown_keys) {
                  ctx.error = error_code::unknown_key;
                  return;
               }
               else {
                  xml::skip_element(it, end, ctx);
                  if (bool(ctx.error)) [[unlikely]] {
                     return;
                  }
               }
               continue;
            }

            std::string chunk;
            if (!xml::parse_char_data(it, end, ctx, chunk)) {
               return;
            }
            if constexpr (has_text) {
               assigned[text_index] = true;
               member.template operator()<text_index>() += chunk;
            }
            else if (!xml::is_whitespace_only(chunk)) {
               if constexpr (Opts.error_on_unknown_keys) {
                  ctx.error = error_code::unknown_key;
                  return;
               }
            }
         }

         check_missing_keys();
      }
   };

   // Convenience functions

   template <auto Opts = xml::xml_opts{}, class T, contiguous Buffer>
   [[nodiscard]] error_ctx read_xml(T&& value, Buffer&& buffer) noexcept
   {
      xml::xml_context ctx{};
      return read<set_xml<Opts>()>(std::forward<T>(value), std::forward<Buffer>(buffer), ctx);
   }

   template <auto Opts = xml::xml_opts{}, class T, is_buffer Buffer>
   [[nodiscard]] error_ctx read_file_xml(T&& value, const std::string_view file_path, Buffer&& buffer) noexcept
   {
      xml::xml_context ctx{};
      ctx.current_file = file_path;
      const auto ec = file_to_buffer(buffer, ctx.current_file);

      if (bool(ec)) {
         return {0, ec};
      }

      return read<set_xml<Opts>()>(std::forward<T>(value), buffer, ctx);
   }

   template <auto Opts = xml::xml_opts{}, class T>
   [[nodiscard]] error_ctx read_file_xml(T&& value, const std::string_view file_path) noexcept
   {
      return read_file_xml<Opts>(value, file_path, std::string{});
   }

} // namespace glz
