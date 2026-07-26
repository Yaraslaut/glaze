// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/xml.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

#include "ut/ut.hpp"

using namespace ut;

suite format_registration = [] {
   "xml_format_id"_test = [] {
      expect(glz::XML == 460u);
      // Must not collide with a neighbouring format id.
      expect(glz::XML != glz::YAML);
      expect(glz::XML != glz::STENCIL);
   };

   "set_xml_applies_format"_test = [] {
      constexpr auto o = glz::set_xml<glz::opts{}>();
      expect(o.format == glz::XML);
   };

   "xml_opts_defaults"_test = [] {
      constexpr glz::xml::xml_opts o{};
      expect(o.format == glz::XML);
      expect(o.error_on_unknown_keys == true);
      expect(o.error_on_missing_keys == false);
      expect(o.skip_null_members == true);
      expect(o.write_declaration == true);
      expect(o.prettify == false);
      expect(o.indentation_width == uint8_t(3));
      expect(o.validate_names == true);
   };

   "xml_opts_is_structural"_test = [] {
      // Regression guard: xml_opts must remain usable as a non-type template
      // parameter. Adding a std::string_view member would break this.
      constexpr auto probe = []<auto Opts>() { return Opts.format; };
      expect(probe.template operator()<glz::xml::xml_opts{}>() == glz::XML);
   };

   "duplicate_key_error_code_exists"_test = [] {
      constexpr auto ec = glz::error_code::duplicate_key;
      expect(ec != glz::error_code::none);
      expect(ec != glz::error_code::unknown_key);
      expect(ec != glz::error_code::missing_key);
   };
};

suite name_classification = [] {
   "name_start_char_ascii"_test = [] {
      expect(glz::xml::is_name_start_char(U'a'));
      expect(glz::xml::is_name_start_char(U'Z'));
      expect(glz::xml::is_name_start_char(U'_'));
      expect(glz::xml::is_name_start_char(U':'));
      // Digits, hyphen and period may continue a name but never start one.
      expect(!glz::xml::is_name_start_char(U'0'));
      expect(!glz::xml::is_name_start_char(U'-'));
      expect(!glz::xml::is_name_start_char(U'.'));
      expect(!glz::xml::is_name_start_char(U' '));
      expect(!glz::xml::is_name_start_char(U'@'));
      expect(!glz::xml::is_name_start_char(U'#'));
   };

   "name_char_ascii"_test = [] {
      expect(glz::xml::is_name_char(U'a'));
      expect(glz::xml::is_name_char(U'0'));
      expect(glz::xml::is_name_char(U'-'));
      expect(glz::xml::is_name_char(U'.'));
      expect(glz::xml::is_name_char(U':'));
      expect(!glz::xml::is_name_char(U' '));
      expect(!glz::xml::is_name_char(U'/'));
      expect(!glz::xml::is_name_char(U'<'));
   };

   "name_start_char_boundaries"_test = [] {
      // Exact edges of the XML 1.0 5th ed. NameStartChar ranges. Written as
      // explicit code points rather than glyph literals: several of these are
      // invisible or zero-width, which makes literal source unreviewable.
      expect(!glz::xml::is_name_start_char(char32_t(0x00B7))); // MIDDLE DOT: NameChar only
      expect(glz::xml::is_name_start_char(char32_t(0x00C0)));
      expect(!glz::xml::is_name_start_char(char32_t(0x00D7))); // MULTIPLICATION SIGN: excluded
      expect(glz::xml::is_name_start_char(char32_t(0x00D8)));
      expect(!glz::xml::is_name_start_char(char32_t(0x00F7))); // DIVISION SIGN: excluded
      expect(glz::xml::is_name_start_char(char32_t(0x00F8)));
      expect(glz::xml::is_name_start_char(char32_t(0x02FF)));
      expect(!glz::xml::is_name_start_char(char32_t(0x0300))); // combining marks: NameChar only
      expect(!glz::xml::is_name_start_char(char32_t(0x036F)));
      expect(glz::xml::is_name_start_char(char32_t(0x0370))); // GREEK CAPITAL LETTER HETA
      expect(glz::xml::is_name_start_char(char32_t(0x037D)));
      expect(!glz::xml::is_name_start_char(char32_t(0x037E))); // GREEK QUESTION MARK: excluded
      expect(glz::xml::is_name_start_char(char32_t(0x037F)));
      expect(glz::xml::is_name_start_char(char32_t(0x200C))); // ZWNJ
      expect(glz::xml::is_name_start_char(char32_t(0x200D))); // ZWJ
      expect(!glz::xml::is_name_start_char(char32_t(0x200E))); // LRM: excluded
      expect(glz::xml::is_name_start_char(char32_t(0x3001))); // start of [#x3001-#xD7FF]
      expect(glz::xml::is_name_start_char(char32_t(0xD7FF)));
      expect(!glz::xml::is_name_start_char(char32_t(0xD800))); // surrogate
      expect(glz::xml::is_name_start_char(char32_t(0x10000)));
      expect(glz::xml::is_name_start_char(char32_t(0xEFFFF)));
      expect(!glz::xml::is_name_start_char(char32_t(0xF0000))); // above the final range
      // Upper edges and the four ranges the original boundary set never
      // touched. Without these, a transposed hex digit in any of them passes
      // every other assertion in this suite.
      expect(!glz::xml::is_name_start_char(char32_t(0x037E))); // GREEK QUESTION MARK
      expect(glz::xml::is_name_start_char(char32_t(0x037F)));
      expect(glz::xml::is_name_start_char(char32_t(0x1FFF)));
      expect(!glz::xml::is_name_start_char(char32_t(0x2000)));
      expect(!glz::xml::is_name_start_char(char32_t(0x206F)));
      expect(glz::xml::is_name_start_char(char32_t(0x2070)));
      expect(glz::xml::is_name_start_char(char32_t(0x218F)));
      expect(!glz::xml::is_name_start_char(char32_t(0x2190)));
      expect(!glz::xml::is_name_start_char(char32_t(0x2BFF)));
      expect(glz::xml::is_name_start_char(char32_t(0x2C00)));
      expect(glz::xml::is_name_start_char(char32_t(0x2FEF)));
      expect(!glz::xml::is_name_start_char(char32_t(0x2FF0)));
      expect(!glz::xml::is_name_start_char(char32_t(0xF8FF)));
      expect(glz::xml::is_name_start_char(char32_t(0xF900)));
      expect(glz::xml::is_name_start_char(char32_t(0xFDCF)));
      expect(!glz::xml::is_name_start_char(char32_t(0xFDD0)));
      expect(!glz::xml::is_name_start_char(char32_t(0xFDEF)));
      expect(glz::xml::is_name_start_char(char32_t(0xFDF0)));
      expect(glz::xml::is_name_start_char(char32_t(0xFFFD)));
      expect(!glz::xml::is_name_start_char(char32_t(0xFFFE)));
   };

   "name_char_extra_ranges"_test = [] {
      // NameChar adds these on top of NameStartChar.
      expect(glz::xml::is_name_char(char32_t(0x002D))); // '-'
      expect(glz::xml::is_name_char(char32_t(0x002E))); // '.'
      expect(glz::xml::is_name_char(char32_t(0x00B7))); // MIDDLE DOT
      expect(glz::xml::is_name_char(char32_t(0x0300))); // combining grave
      expect(glz::xml::is_name_char(char32_t(0x036F)));
      expect(glz::xml::is_name_char(char32_t(0x203F))); // UNDERTIE
      expect(glz::xml::is_name_char(char32_t(0x2040))); // CHARACTER TIE
      expect(!glz::xml::is_name_char(char32_t(0x2041)));
      // A NameStartChar is always also a NameChar -- 0x0370 is in
      // [#x370-#x37D], so it must be accepted by BOTH predicates.
      expect(glz::xml::is_name_char(char32_t(0x0370)));
      expect(glz::xml::is_name_char(char32_t(0x3001)));
      // Not a name character under either production.
      expect(!glz::xml::is_name_char(char32_t(0x0020)));
      expect(!glz::xml::is_name_char(char32_t(0x00D7)));
   };

   "is_xml_char"_test = [] {
      expect(glz::xml::is_xml_char(U'\t'));
      expect(glz::xml::is_xml_char(U'\n'));
      expect(glz::xml::is_xml_char(U'\r'));
      expect(glz::xml::is_xml_char(U' '));
      // Control characters other than tab/LF/CR are forbidden outright.
      expect(!glz::xml::is_xml_char(U'\0'));
      expect(!glz::xml::is_xml_char(U'\x01'));
      expect(!glz::xml::is_xml_char(U'\x0B'));
      expect(!glz::xml::is_xml_char(U'\x0C'));
      expect(!glz::xml::is_xml_char(U'\x1F'));
      // Surrogates are never valid XML characters.
      expect(!glz::xml::is_xml_char(char32_t(0xD800)));
      expect(!glz::xml::is_xml_char(char32_t(0xDFFF)));
      expect(glz::xml::is_xml_char(char32_t(0xE000))); // first private-use code point
      expect(!glz::xml::is_xml_char(char32_t(0xFFFE)));
      expect(!glz::xml::is_xml_char(char32_t(0xFFFF)));
      expect(glz::xml::is_xml_char(char32_t(0x10FFFF)));
      expect(!glz::xml::is_xml_char(char32_t(0x110000)));
   };

   "validate_name"_test = [] {
      expect(glz::xml::validate_name("a"));
      expect(glz::xml::validate_name("book"));
      expect(glz::xml::validate_name("_x"));
      expect(glz::xml::validate_name("ns:tag"));
      expect(glz::xml::validate_name("a-b.c9"));
      expect(glz::xml::validate_name("\xC3\xA9lement")); // UTF-8 "élement"
      expect(!glz::xml::validate_name(""));
      expect(!glz::xml::validate_name("1a"));
      expect(!glz::xml::validate_name("-a"));
      expect(!glz::xml::validate_name("a b"));
      expect(!glz::xml::validate_name("a/b"));
      expect(!glz::xml::validate_name("@id"));
      expect(!glz::xml::validate_name("#text"));
      expect(!glz::xml::validate_name("\xFF\xFE")); // malformed UTF-8
   };

   "validate_ncname"_test = [] {
      // NCName is Name minus the colon.
      expect(glz::xml::validate_ncname("tag"));
      expect(glz::xml::validate_ncname("_x9"));
      expect(!glz::xml::validate_ncname("ns:tag"));
      expect(!glz::xml::validate_ncname(":tag"));
      expect(!glz::xml::validate_ncname(""));
      expect(!glz::xml::validate_ncname("1a"));
   };

   "decode_utf8"_test = [] {
      auto decode = [](std::string_view s) {
         char32_t cp{};
         const auto n = glz::xml::decode_utf8(s.data(), s.data() + s.size(), cp);
         return std::pair{n, cp};
      };
      expect(decode("A") == std::pair{size_t(1), U'A'});
      expect(decode("\xC3\xA9") == std::pair{size_t(2), U'é'});
      expect(decode("\xE2\x82\xAC") == std::pair{size_t(3), U'€'});
      expect(decode("\xF0\x9F\x98\x80") == std::pair{size_t(4), U'\U0001F600'});
      // Malformed sequences return 0.
      expect(decode("\xFF").first == size_t(0)); // invalid lead byte
      expect(decode("\xC3").first == size_t(0)); // truncated
      expect(decode("\xC3\x28").first == size_t(0)); // bad continuation
      expect(decode("\xC0\x80").first == size_t(0)); // overlong NUL
      expect(decode("\xE0\x80\x80").first == size_t(0)); // overlong
      expect(decode("\xED\xA0\x80").first == size_t(0)); // encoded surrogate
      expect(decode("\xF4\x90\x80\x80").first == size_t(0)); // > U+10FFFF
      expect(decode("").first == size_t(0));
   };

   "classification_is_constexpr"_test = [] {
      static_assert(glz::xml::is_name_start_char(U'a'));
      static_assert(!glz::xml::is_name_start_char(U'0'));
      static_assert(glz::xml::validate_name("book"));
      static_assert(!glz::xml::validate_name("1a"));
      expect(true);
   };
};

int main() {}
