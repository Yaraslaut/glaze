// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstddef>
#include <string_view>

#include "glaze/core/context.hpp"
#include "glaze/xml/common.hpp"

namespace glz::xml
{
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
}
