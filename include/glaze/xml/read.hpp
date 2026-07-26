// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/core/context.hpp"
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
}
