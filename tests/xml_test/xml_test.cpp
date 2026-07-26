// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/xml.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

suite entities_and_escaping = [] {
   auto decode_all = [](std::string_view s) -> std::pair<bool, std::string> {
      std::string out;
      const char* it = s.data();
      const char* const end = it + s.size();
      while (it < end) {
         if (*it == '&') {
            if (!glz::xml::decode_reference(it, end, out)) return {false, out};
         }
         else {
            out.push_back(*it++);
         }
      }
      return {true, out};
   };

   "predefined_entities"_test = [decode_all] {
      expect(decode_all("&amp;") == std::pair{true, std::string{"&"}});
      expect(decode_all("&lt;") == std::pair{true, std::string{"<"}});
      expect(decode_all("&gt;") == std::pair{true, std::string{">"}});
      expect(decode_all("&quot;") == std::pair{true, std::string{"\""}});
      expect(decode_all("&apos;") == std::pair{true, std::string{"'"}});
      expect(decode_all("a&amp;b") == std::pair{true, std::string{"a&b"}});
      expect(decode_all("&lt;&gt;") == std::pair{true, std::string{"<>"}});
   };

   "numeric_character_references"_test = [decode_all] {
      expect(decode_all("&#65;") == std::pair{true, std::string{"A"}});
      expect(decode_all("&#x41;") == std::pair{true, std::string{"A"}});
      expect(decode_all("&#233;") == std::pair{true, std::string{"\xC3\xA9"}});
      expect(decode_all("&#x20AC;") == std::pair{true, std::string{"\xE2\x82\xAC"}});
      expect(decode_all("&#128512;") == std::pair{true, std::string{"\xF0\x9F\x98\x80"}});
      expect(decode_all("&#x9;") == std::pair{true, std::string{"\t"}});
   };

   "rejected_references"_test = [decode_all] {
      // Undeclared general entity — we do not expand the internal DTD subset.
      expect(decode_all("&foo;").first == false);
      // Malformed syntax.
      expect(decode_all("&").first == false);
      expect(decode_all("&amp").first == false); // missing ';'
      expect(decode_all("&;").first == false); // empty name
      expect(decode_all("&#;").first == false); // empty number
      expect(decode_all("&#x;").first == false);
      expect(decode_all("&#").first == false); // truncated right after '#'
      expect(decode_all("&#x").first == false); // truncated right after 'x'
      expect(decode_all("&#zz;").first == false);
      expect(decode_all("&#1a;").first == false);
      // Code points that are not legal XML characters.
      expect(decode_all("&#0;").first == false); // NUL
      expect(decode_all("&#xB;").first == false); // vertical tab
      expect(decode_all("&#xD800;").first == false); // surrogate
      expect(decode_all("&#xDFFF;").first == false); // surrogate
      expect(decode_all("&#xFFFE;").first == false); // noncharacter
      expect(decode_all("&#xFFFF;").first == false); // noncharacter
      expect(decode_all("&#x110000;").first == false); // out of range
      // Overflow must not wrap into a valid code point.
      expect(decode_all("&#99999999999999999999;").first == false);
      expect(decode_all("&#xFFFFFFFFFFFFFFFF;").first == false);
      // XML 1.0 [66] spells the hex marker '&#x' in lowercase only; the
      // uppercase form is not well-formed. W3C conformance case not-wf-sa-093.
      expect(decode_all("&#X41;").first == false);
      expect(decode_all("&#X58;").first == false);
      // Hex DIGITS remain case-insensitive -- only the marker is not.
      expect(decode_all("&#xAB;") == std::pair{true, std::string{"\xC2\xAB"}});
      expect(decode_all("&#xab;") == std::pair{true, std::string{"\xC2\xAB"}});
   };

   "encode_utf8"_test = [] {
      auto enc = [](char32_t cp) {
         char buf[4]{};
         const auto n = glz::xml::encode_utf8(cp, buf);
         return std::string{buf, n};
      };
      expect(enc(U'A') == "A");
      expect(enc(U'é') == "\xC3\xA9");
      expect(enc(U'€') == "\xE2\x82\xAC");
      expect(enc(U'\U0001F600') == "\xF0\x9F\x98\x80");
      expect(enc(char32_t(0xD800)).empty());
      expect(enc(char32_t(0x110000)).empty());
   };

   "escape_text"_test = [] {
      auto esc = [](std::string_view s) {
         std::string b;
         size_t ix = 0;
         glz::xml::xml_context ctx{};
         glz::xml::escape_text(s, ctx, b, ix);
         b.resize(ix);
         return b;
      };
      expect(esc("plain") == "plain");
      expect(esc("a&b") == "a&amp;b");
      expect(esc("a<b") == "a&lt;b");
      expect(esc("a>b") == "a&gt;b");
      // Quotes need no escaping in text content.
      expect(esc("say \"hi\"") == "say \"hi\"");
      expect(esc("'") == "'");
      // ']]>' must never appear literally in character data.
      expect(esc("a]]>b") == "a]]&gt;b");
      expect(esc("") == "");
   };

   "escape_attribute"_test = [] {
      auto esc = [](std::string_view s) {
         std::string b;
         size_t ix = 0;
         glz::xml::xml_context ctx{};
         glz::xml::escape_attribute(s, ctx, b, ix);
         b.resize(ix);
         return b;
      };
      expect(esc("plain") == "plain");
      expect(esc("a&b") == "a&amp;b");
      expect(esc("a<b") == "a&lt;b");
      expect(esc("a>b") == "a&gt;b");
      expect(esc("say \"hi\"") == "say &quot;hi&quot;");
      // Whitespace must survive attribute-value normalization on re-read,
      // so it is written as character references rather than literally.
      expect(esc("a\tb") == "a&#x9;b");
      expect(esc("a\nb") == "a&#xA;b");
      expect(esc("a\rb") == "a&#xD;b");
      expect(esc(" keep spaces ") == " keep spaces ");
   };

   "escape_roundtrip"_test = [decode_all] {
      for (std::string_view s : {"a&b", "a<b", "a]]>b", "\"q\"", "é€"}) {
         std::string b;
         size_t ix = 0;
         glz::xml::xml_context ctx{};
         glz::xml::escape_text(s, ctx, b, ix);
         b.resize(ix);
         const auto [ok, decoded] = decode_all(b);
         expect(ok) << "failed to decode escaped form of " << s;
         expect(decoded == std::string{s}) << "roundtrip mismatch for " << s;
      }
   };
};

suite xml_context_tests = [] {
   "element_stack_nesting"_test = [] {
      glz::xml::xml_context ctx{};
      expect(ctx.element_depth() == size_t(0));
      expect(ctx.push_element("root"));
      expect(ctx.element_depth() == size_t(1));
      expect(ctx.current_element() == "root");
      expect(ctx.push_element("child"));
      expect(ctx.current_element() == "child");
      expect(ctx.pop_element("child"));
      expect(ctx.current_element() == "root");
      expect(ctx.pop_element("root"));
      expect(ctx.element_depth() == size_t(0));
   };

   "element_stack_mismatch_rejected"_test = [] {
      glz::xml::xml_context ctx{};
      expect(ctx.push_element("a"));
      expect(ctx.push_element("b"));
      // </a> while <b> is open is the classic well-formedness violation.
      expect(!ctx.pop_element("a"));
   };

   "pop_on_empty_stack_rejected"_test = [] {
      glz::xml::xml_context ctx{};
      expect(!ctx.pop_element("a"));
   };

   "depth_limit_enforced"_test = [] {
      glz::xml::xml_context ctx{};
      bool hit_limit = false;
      for (size_t i = 0; i < glz::max_recursive_depth_limit + 2; ++i) {
         if (!ctx.push_element("e")) {
            hit_limit = true;
            break;
         }
      }
      expect(hit_limit) << "deeply nested input must be rejected, not overflow";
      expect(ctx.error == glz::error_code::exceeded_max_recursive_depth);
   };

   "namespace_scoping"_test = [] {
      glz::xml::xml_context ctx{};
      ctx.push_ns_scope();
      expect(ctx.bind_prefix("a", "urn:a"));
      expect(ctx.resolve_prefix("a") == "urn:a");

      ctx.push_ns_scope();
      expect(ctx.bind_prefix("b", "urn:b"));
      // Outer binding still visible from the inner scope.
      expect(ctx.resolve_prefix("a") == "urn:a");
      expect(ctx.resolve_prefix("b") == "urn:b");

      ctx.pop_ns_scope();
      // Inner binding gone once its scope closes.
      expect(ctx.resolve_prefix("b").empty());
      expect(ctx.resolve_prefix("a") == "urn:a");

      ctx.pop_ns_scope();
      expect(ctx.resolve_prefix("a").empty());
   };

   "namespace_shadowing"_test = [] {
      glz::xml::xml_context ctx{};
      ctx.push_ns_scope();
      expect(ctx.bind_prefix("p", "urn:outer"));
      ctx.push_ns_scope();
      expect(ctx.bind_prefix("p", "urn:inner"));
      expect(ctx.resolve_prefix("p") == "urn:inner");
      ctx.pop_ns_scope();
      expect(ctx.resolve_prefix("p") == "urn:outer");
   };

   "unbound_prefix_resolves_empty"_test = [] {
      glz::xml::xml_context ctx{};
      ctx.push_ns_scope();
      expect(ctx.resolve_prefix("nope").empty());
   };

   "format_context_specialized"_test = [] {
      static_assert(std::same_as<glz::format_context_t<glz::XML>, glz::xml::xml_context>);
      expect(true);
   };

   "xml_context_satisfies_is_context"_test = [] {
      // Regression guard. glz::context exposes a `uint32_t depth` FIELD, and
      // glz::is_context requires `{ ctx.depth } -> std::same_as<uint32_t&>`.
      // Declaring a member FUNCTION named depth() on the derived type hides
      // that field, silently breaking both this concept and glz::depth_guard,
      // which every other format reader relies on for recursion protection.
      static_assert(glz::is_context<glz::xml::xml_context>);
      glz::xml::xml_context ctx{};
      {
         glz::depth_guard guard{ctx};
         expect(ctx.depth == uint32_t(1));
      }
      expect(ctx.depth == uint32_t(0));
   };
};

// Writes a bare scalar with the declaration suppressed, so the test asserts on
// the scalar's lexical form alone.
template <class T>
static std::string write_scalar(T&& v)
{
   return glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(std::forward<T>(v)).value_or("<error>");
}

enum struct color { red, green, blue };

template <>
struct glz::meta<color>
{
   static constexpr auto value = enumerate(color::red, color::green, color::blue);
};

suite scalar_writing = [] {
   "write_bool"_test = [] {
      // XSD boolean lexical space is "true"/"false", never 1/0.
      expect(write_scalar(true) == "<root>true</root>");
      expect(write_scalar(false) == "<root>false</root>");
   };

   "write_integers"_test = [] {
      expect(write_scalar(0) == "<root>0</root>");
      expect(write_scalar(42) == "<root>42</root>");
      expect(write_scalar(-7) == "<root>-7</root>");
      expect(write_scalar(int64_t{-9223372036854775807LL - 1}) == "<root>-9223372036854775808</root>");
      expect(write_scalar(uint64_t{18446744073709551615ULL}) == "<root>18446744073709551615</root>");
   };

   "write_floats"_test = [] {
      expect(write_scalar(1.5) == "<root>1.5</root>");
      expect(write_scalar(0.0) == "<root>0</root>");
      expect(write_scalar(-2.25) == "<root>-2.25</root>");
   };

   "write_special_floats_use_xsd_lexical_space"_test = [] {
      // XSD 1.0 spells these "NaN", "INF", "-INF" for xs:float / xs:double.
      // Emitting JSON's "null" here would collapse three distinct values and
      // produce documents that fail the xs:double schema generated in Phase 5.
      expect(write_scalar(std::numeric_limits<double>::quiet_NaN()) == "<root>NaN</root>");
      expect(write_scalar(std::numeric_limits<double>::infinity()) == "<root>INF</root>");
      expect(write_scalar(-std::numeric_limits<double>::infinity()) == "<root>-INF</root>");
      expect(write_scalar(std::numeric_limits<float>::quiet_NaN()) == "<root>NaN</root>");
      expect(write_scalar(-std::numeric_limits<float>::infinity()) == "<root>-INF</root>");
   };

   "write_strings_are_escaped"_test = [] {
      expect(write_scalar(std::string{"hello"}) == "<root>hello</root>");
      expect(write_scalar(std::string{"a&b"}) == "<root>a&amp;b</root>");
      expect(write_scalar(std::string{"a<b"}) == "<root>a&lt;b</root>");
      expect(write_scalar(std::string{"]]>"}) == "<root>]]&gt;</root>");
      expect(write_scalar(std::string{""}) == "<root></root>");
   };

   "write_enum_as_name"_test = [] { expect(write_scalar(color::green) == "<root>green</root>"); };

   "write_declaration_default_on"_test = [] {
      const auto s = glz::write_xml(42).value_or("<error>");
      expect(s == "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root>42</root>") << s;
   };

   "write_xml_accepts_non_resizable_buffers"_test = [] {
      // write_xml advertises the output_buffer concept, which admits
      // std::array. Calling .resize() unconditionally broke that contract and
      // failed to compile for every sibling-format-compatible buffer type.
      // 512 bytes gives the integer writer's scratch-space headroom (see
      // write_padding_bytes) room to work, matching the convention used by
      // the other formats' bounded-buffer tests (e.g. csv_bounded_buffer_test.cpp).
      std::array<char, 512> arr{};
      const auto ec = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(42, arr);
      expect(!ec) << "std::array buffer must be supported";
      expect(std::string_view{arr.data(), ec.count} == "<root>42</root>");

      // A buffer too small to hold the output must report overflow, not
      // truncate silently or write out of bounds.
      std::array<char, 4> tiny{};
      const auto ec2 = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(42, tiny);
      expect(bool(ec2)) << "undersized buffer must error";
   };

   "invalid_root_name_is_rejected_before_writing"_test = [] {
      // A name that is not a valid XML Name must be an error, never malformed
      // output -- Glaze must not emit a document it would itself reject.
      for (const std::string_view bad : {"1bad", "-x", "a b", "a/b", "", "@id", "#text"}) {
         const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(42, bad);
         expect(!r.has_value()) << "must reject root name: " << bad;
         if (!r.has_value()) {
            expect(r.error() == glz::error_code::syntax_error);
         }
      }
      // Valid names still work, including one with a namespace prefix.
      expect(glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(42, "ns:doc").value_or("") ==
             "<ns:doc>42</ns:doc>");
   };
};

struct xml_user
{
   int id{};
   std::string role{};
   std::string text{};
};

template <>
struct glz::meta<xml_user>
{
   using T = xml_user;
   static constexpr auto value = object("@id", &T::id, "@role", &T::role, "#text", &T::text);
   static constexpr std::string_view root_name = "user";
};

struct xml_book
{
   std::string title{};
   int year{};
};

template <>
struct glz::meta<xml_book>
{
   using T = xml_book;
   static constexpr auto value = object("title", &T::title, "year", &T::year);
};

struct xml_nested
{
   xml_book book{};
   std::string note{};
};

template <>
struct glz::meta<xml_nested>
{
   using T = xml_nested;
   static constexpr auto value = object("book", &T::book, "note", &T::note);
};

suite object_writing = [] {
   "attributes_and_text"_test = [] {
      const xml_user u{.id = 7, .role = "admin", .text = "Alice"};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(u).value_or("<error>");
      // root_name comes from glz::meta, so the tag is <user>, not <root>.
      expect(s == "<user id=\"7\" role=\"admin\">Alice</user>") << s;
   };

   "child_elements"_test = [] {
      const xml_book b{.title = "Dune", .year = 1965};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(b, "book").value_or("<error>");
      expect(s == "<book><title>Dune</title><year>1965</year></book>") << s;
   };

   "nested_structs"_test = [] {
      const xml_nested n{.book = {.title = "Dune", .year = 1965}, .note = "classic"};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(n, "n").value_or("<error>");
      expect(s == "<n><book><title>Dune</title><year>1965</year></book><note>classic</note></n>") << s;
   };

   "attribute_values_are_attribute_escaped"_test = [] {
      const xml_user u{.id = 1, .role = "a\"b&c", .text = "x<y"};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(u).value_or("<error>");
      expect(s == "<user id=\"1\" role=\"a&quot;b&amp;c\">x&lt;y</user>") << s;
   };

   "runtime_root_name_overrides_default_but_not_meta"_test = [] {
      const xml_book b{.title = "T", .year = 1};
      const auto s1 = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(b, "custom").value_or("<error>");
      expect(s1.starts_with("<custom>")) << s1;

      // glz::meta<xml_user>::root_name wins over the runtime argument.
      const xml_user u{.id = 1, .role = "r", .text = "t"};
      const auto s2 = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(u, "ignored").value_or("<error>");
      expect(s2.starts_with("<user ")) << s2;
   };

   "invalid_root_name_is_rejected"_test = [] {
      const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(42, "1bad");
      expect(!r.has_value());
      expect(r.error() == glz::error_code::syntax_error);
   };

   "sigil_helpers"_test = [] {
      static_assert(glz::xml::is_attribute_key("@id"));
      static_assert(!glz::xml::is_attribute_key("id"));
      static_assert(glz::xml::is_text_key("#text"));
      static_assert(!glz::xml::is_text_key("#other"));
      static_assert(glz::xml::strip_sigil("@id") == "id");
      static_assert(glz::xml::strip_sigil("id") == "id");
      expect(true);
   };

   "attribute_names_are_validated_too"_test = [] {
      // Attribute and element names share the XML Name production. The guard
      // itself is a static_assert in the writer (a bad name fails to compile),
      // so this pins the underlying predicate the guard relies on.
      static_assert(glz::xml::validate_name(glz::xml::strip_sigil("@id")));
      static_assert(!glz::xml::validate_name(glz::xml::strip_sigil("@1bad")));
      static_assert(glz::xml::validate_name(glz::xml::strip_sigil("@xml:lang")));
      expect(true);
   };
};

struct attr_child
{
   int a{};
   std::string v{};
};

template <>
struct glz::meta<attr_child>
{
   using T = attr_child;
   static constexpr auto value = object("@a", &T::a, "v", &T::v);
};

struct attr_outer
{
   attr_child child{};
};

template <>
struct glz::meta<attr_outer>
{
   using T = attr_outer;
   static constexpr auto value = object("child", &T::child);
};

struct attrs_only
{
   int x{};
   int y{};
};

template <>
struct glz::meta<attrs_only>
{
   using T = attrs_only;
   static constexpr auto value = object("@x", &T::x, "@y", &T::y);
};

struct empty_obj
{
   [[maybe_unused]] int unused_{};
};

template <>
struct glz::meta<empty_obj>
{
   static constexpr auto value = object();
};

suite tag_ownership_edge_cases = [] {
   "nested_object_with_its_own_attributes"_test = [] {
      // The recursive element-pass call site, NOT the root one. This is where
      // split responsibility for closing '>' is most likely to break.
      const attr_outer o{.child = {.a = 5, .v = "hi"}};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(o, "outer").value_or("<error>");
      expect(s == R"(<outer><child a="5"><v>hi</v></child></outer>)") << s;
   };

   "attributes_only_object"_test = [] {
      const attrs_only o{.x = 1, .y = 2};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(o, "ao").value_or("<error>");
      expect(s == R"(<ao x="1" y="2"></ao>)") << s;
   };

   "empty_object"_test = [] {
      const empty_obj o{};
      const auto s = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(o, "eo").value_or("<error>");
      expect(s == "<eo></eo>") << s;
   };
};

struct xml_shelf
{
   std::string name{};
   std::vector<int> tag{};
};

template <>
struct glz::meta<xml_shelf>
{
   using T = xml_shelf;
   static constexpr auto value = object("name", &T::name, "tag", &T::tag);
};

struct xml_library
{
   std::vector<xml_book> book{};
};

template <>
struct glz::meta<xml_library>
{
   using T = xml_library;
   static constexpr auto value = object("book", &T::book);
};

suite container_writing = [] {
   "vector_of_scalars_repeats_the_member_tag"_test = [] {
      const xml_shelf s{.name = "a", .tag = {1, 2, 3}};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(s, "shelf").value_or("<error>");
      expect(out == "<shelf><name>a</name><tag>1</tag><tag>2</tag><tag>3</tag></shelf>") << out;
   };

   "empty_vector_emits_nothing"_test = [] {
      const xml_shelf s{.name = "a", .tag = {}};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(s, "shelf").value_or("<error>");
      expect(out == "<shelf><name>a</name></shelf>") << out;
   };

   "single_element_vector_still_emits_one_tag"_test = [] {
      const xml_shelf s{.name = "a", .tag = {9}};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(s, "shelf").value_or("<error>");
      expect(out == "<shelf><name>a</name><tag>9</tag></shelf>") << out;
   };

   "vector_of_structs"_test = [] {
      const xml_library lib{.book = {{.title = "A", .year = 1}, {.title = "B", .year = 2}}};
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(lib, "library").value_or("<error>");
      expect(out ==
             "<library><book><title>A</title><year>1</year></book>"
             "<book><title>B</title><year>2</year></book></library>")
         << out;
   };

   "map_keys_become_element_names"_test = [] {
      const std::map<std::string, int> m{{"alpha", 1}, {"beta", 2}};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(m, "m").value_or("<error>");
      expect(out == "<m><alpha>1</alpha><beta>2</beta></m>") << out;
   };

   "map_key_that_is_not_a_valid_name_is_rejected"_test = [] {
      // Map keys are runtime values, so this is a runtime check, unlike struct
      // member keys which are validated at compile time.
      const std::map<std::string, int> m{{"1bad", 1}};
      const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(m, "m");
      expect(!r.has_value()) << "invalid element names must never be emitted";
      if (!r.has_value()) {
         expect(r.error() == glz::error_code::syntax_error);
      }
   };

   "nested_vector_inside_vector_of_structs"_test = [] {
      const std::vector<xml_shelf> shelves{{.name = "s1", .tag = {1}}, {.name = "s2", .tag = {2, 3}}};
      std::string out;
      const auto ec = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(shelves, out, "shelf");
      // A bare top-level sequence would produce multiple root elements.
      expect(bool(ec)) << "top-level sequence must be rejected: " << out;
   };
};

struct nested_seq_holder
{
   std::vector<std::vector<int>> m{};
};

template <>
struct glz::meta<nested_seq_holder>
{
   using T = nested_seq_holder;
   static constexpr auto value = object("m", &T::m);
};

suite nested_container_rejection = [] {
   "nested_sequence_is_rejected_not_silently_fused"_test = [] {
      // Writing the bodies back to back is well-formed XML but fuses the items:
      // {{1,2},{3,4}} would become <m>12</m><m>34</m>, unrecoverable on read.
      // XML has no name for the inner items, so this must error.
      const nested_seq_holder h{.m = {{1, 2}, {3, 4}}};
      const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, "s");
      expect(!r.has_value()) << "nested sequence must error, not silently fuse items";
      if (!r.has_value()) {
         expect(r.error() == glz::error_code::syntax_error);
      }
   };

   "map_of_sequences_is_rejected"_test = [] {
      const std::map<std::string, std::vector<int>> m{{"a", {1, 2, 3}}};
      const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(m, "m");
      expect(!r.has_value()) << "map-of-sequence must error, not fuse into <a>123</a>";
   };

   "single_level_sequences_still_work"_test = [] {
      // Regression guard: the named-item path through an object member is
      // unaffected and must keep working.
      const xml_shelf s{.name = "a", .tag = {1, 2, 3}};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(s, "shelf").value_or("<error>");
      expect(out == "<shelf><name>a</name><tag>1</tag><tag>2</tag><tag>3</tag></shelf>") << out;
   };

   "failed_serialization_does_not_close_the_tag"_test = [] {
      // A tag whose body failed to serialize must not be closed: <m></m> would
      // be indistinguishable from a legitimately empty element. Mirrors the
      // JSON writer's guard (json/write.hpp:200,314,436).
      const nested_seq_holder h{.m = {{1, 2}, {3, 4}}};
      std::string buffer;
      const auto ec = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, buffer, "s");
      expect(bool(ec)) << "nested sequence must still error";
      expect(!buffer.starts_with("<s><m></m></s>")) << "must not emit a well-formed but bogus element: " << buffer;
   };
};

struct xml_opt_holder
{
   std::optional<int> maybe{};
   std::string always{};
};

template <>
struct glz::meta<xml_opt_holder>
{
   using T = xml_opt_holder;
   static constexpr auto value = object("maybe", &T::maybe, "always", &T::always);
};

struct xml_variant_holder
{
   std::variant<int, std::string> v{};
};

template <>
struct glz::meta<xml_variant_holder>
{
   using T = xml_variant_holder;
   static constexpr auto value = object("v", &T::v);
};

struct xml_mixed
{
   std::string text{};
   std::string child{};
};

template <>
struct glz::meta<xml_mixed>
{
   using T = xml_mixed;
   static constexpr auto value = object("#text", &T::text, "child", &T::child);
};

suite nullable_variant_prettify = [] {
   "null_members_skipped_by_default"_test = [] {
      const xml_opt_holder h{.maybe = std::nullopt, .always = "x"};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, "h").value_or("<error>");
      expect(out == "<h><always>x</always></h>") << out;
   };

   "null_members_emitted_when_not_skipping"_test = [] {
      const xml_opt_holder h{.maybe = std::nullopt, .always = "x"};
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.skip_null_members = false, .write_declaration = false}>(h, "h").value_or(
            "<error>");
      // An absent value is represented by an empty element.
      expect(out == "<h><maybe></maybe><always>x</always></h>") << out;
   };

   "engaged_optional_writes_value"_test = [] {
      const xml_opt_holder h{.maybe = 5, .always = "x"};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, "h").value_or("<error>");
      expect(out == "<h><maybe>5</maybe><always>x</always></h>") << out;
   };

   "variant_writes_active_alternative"_test = [] {
      const xml_variant_holder a{.v = 42};
      expect(glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(a, "h").value_or("") == "<h><v>42</v></h>");
      const xml_variant_holder b{.v = std::string{"hi"}};
      expect(glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(b, "h").value_or("") == "<h><v>hi</v></h>");
   };

   "prettify_uses_indentation_width"_test = [] {
      const xml_book b{.title = "Dune", .year = 1965};
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false, .prettify = true}>(b, "book").value_or(
            "<error>");
      expect(out ==
             "<book>\n"
             "   <title>Dune</title>\n"
             "   <year>1965</year>\n"
             "</book>")
         << out;
   };

   "prettify_custom_indentation_width"_test = [] {
      const xml_book b{.title = "D", .year = 1};
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false, .prettify = true, .indentation_width = 2}>(
            b, "book")
            .value_or("<error>");
      expect(out ==
             "<book>\n"
             "  <title>D</title>\n"
             "  <year>1</year>\n"
             "</book>")
         << out;
   };

   "prettify_does_not_add_whitespace_to_text_content"_test = [] {
      // Indenting an element that holds text would change that text's value.
      const xml_user u{.id = 1, .role = "r", .text = "Alice"};
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false, .prettify = true}>(u).value_or("<error>");
      expect(out == "<user id=\"1\" role=\"r\">Alice</user>") << out;
   };

   "prettify_does_not_corrupt_mixed_content"_test = [] {
      // An element with BOTH #text and element children. Indenting it appends
      // the newline and indent to the text value -- #text becomes "HELLO\n   "
      // instead of "HELLO", silent corruption on read-back.
      const xml_mixed m{.text = "HELLO", .child = "c"};
      const auto compact = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(m, "m").value_or("<error>");
      const auto pretty =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false, .prettify = true}>(m, "m").value_or("<error>");
      expect(compact == "<m>HELLO<child>c</child></m>") << compact;
      expect(pretty == compact) << "mixed content must be written compactly even with prettify: " << pretty;
   };

   "declaration_precedes_prettified_root"_test = [] {
      const xml_book b{.title = "D", .year = 1};
      const auto out = glz::write_xml<glz::xml::xml_opts{.prettify = true}>(b, "book").value_or("<error>");
      expect(out.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<book>")) << out;
   };
};

struct xml_chrono_holder
{
   std::chrono::sys_time<std::chrono::seconds> at{};
   std::chrono::sys_days day{};
   std::chrono::year_month_day ymd{};
};

template <>
struct glz::meta<xml_chrono_holder>
{
   using T = xml_chrono_holder;
   static constexpr auto value = object("at", &T::at, "day", &T::day, "ymd", &T::ymd);
};

suite chrono_types = [] {
   using namespace std::chrono;

   // Durations and steady_clock/high_resolution_clock time points need no
   // XML-specific code: they are handled generically for every format
   // (including XML) by core/chrono.hpp, since they carry no calendar
   // semantics and serialize as a bare numeric count.
   "duration_writes_the_bare_count"_test = [] {
      const auto out =
         glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(milliseconds{1500}, "d").value_or("<error>");
      expect(out == "<d>1500</d>") << out;
   };

   // Calendar types delegate to the shared chrono_detail ISO 8601 writers so XML
   // matches the lexical form every other Glaze text format uses.
   "chrono_calendar_types_write_iso8601_text"_test = [] {
      const xml_chrono_holder h{.at = time_point_cast<seconds>(sys_days{2024y / 12 / 13} + 15h + 30min + 45s),
                                .day = sys_days{2024y / 12 / 13},
                                .ymd = 2024y / 12 / 13};
      const auto out = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, "h").value_or("<error>");
      // sys_days (period == days) writes date-only, matching every other format.
      expect(out == "<h><at>2024-12-13T15:30:45Z</at><day>2024-12-13</day><ymd>2024-12-13</ymd></h>") << out;
   };
};

int main() {}
