// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
}
