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
#include <vector>

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
      // the other Glaze writers.
      template <class B>
      void append_raw(std::string_view s, B& b, auto& ix)
      {
         if (ix + s.size() > b.size()) {
            b.resize((std::max)(b.size() * 2, ix + s.size()));
         }
         std::memcpy(b.data() + ix, s.data(), s.size());
         ix += s.size();
      }
   }

   // Escapes character data. '>' is escaped unconditionally, which is stricter
   // than required but guarantees ']]>' can never appear literally.
   template <class B>
   void escape_text(std::string_view s, B& b, auto& ix)
   {
      for (const char c : s) {
         switch (c) {
         case '&':
            detail::append_raw("&amp;", b, ix);
            break;
         case '<':
            detail::append_raw("&lt;", b, ix);
            break;
         case '>':
            detail::append_raw("&gt;", b, ix);
            break;
         default:
            detail::append_raw(std::string_view{&c, 1}, b, ix);
            break;
         }
      }
   }

   // Escapes an attribute value. Additionally escapes '"' (the quote we emit)
   // and the three whitespace characters, because attribute-value
   // normalization would otherwise turn them into spaces on re-read.
   template <class B>
   void escape_attribute(std::string_view s, B& b, auto& ix)
   {
      for (const char c : s) {
         switch (c) {
         case '&':
            detail::append_raw("&amp;", b, ix);
            break;
         case '<':
            detail::append_raw("&lt;", b, ix);
            break;
         case '>':
            detail::append_raw("&gt;", b, ix);
            break;
         case '"':
            detail::append_raw("&quot;", b, ix);
            break;
         case '\t':
            detail::append_raw("&#x9;", b, ix);
            break;
         case '\n':
            detail::append_raw("&#xA;", b, ix);
            break;
         case '\r':
            detail::append_raw("&#xD;", b, ix);
            break;
         default:
            detail::append_raw(std::string_view{&c, 1}, b, ix);
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
}

namespace glz
{
   template <>
   struct format_context<XML>
   {
      using type = xml::xml_context;
   };
}
