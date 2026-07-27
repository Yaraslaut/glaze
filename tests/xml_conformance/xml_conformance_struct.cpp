// The struct-mapping side of XML conformance: documents drawn from the W3C
// suite read into real C++ types, asserting values land correctly rather than
// merely that parsing succeeded.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "glaze/xml.hpp"
#include "ut/ut.hpp"

using namespace ut;

struct conf_doc
{
   std::string a{};
   std::string text{};
};

template <>
struct glz::meta<conf_doc>
{
   using T = conf_doc;
   static constexpr auto value = object("@a", &T::a, "#text", &T::text);
};

struct conf_list
{
   std::vector<std::string> item{};
};

template <>
struct glz::meta<conf_list>
{
   using T = conf_list;
   static constexpr auto value = object("item", &T::item);
};

suite struct_conformance = [] {
   "entity_expansion_lands_in_the_member"_test = [] {
      conf_doc d{};
      const std::string xml = R"(<doc a="&amp;&lt;">&gt;&quot;</doc>)";
      const auto ec = glz::read_xml(d, xml);
      expect(!ec) << glz::format_error(ec, xml);
      expect(d.a == "&<");
      expect(d.text == ">\"");
   };

   "character_references_land_decoded"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{R"(<doc a="&#65;&#x42;">&#233;</doc>)"}));
      expect(d.a == "AB");
      expect(d.text == "\xC3\xA9");
   };

   "cdata_content_lands_literally"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{R"(<doc a="x"><![CDATA[a&b<c]]></doc>)"}));
      expect(d.text == "a&b<c");
   };

   "mixed_text_and_cdata_concatenate"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{R"(<doc a="x">one<![CDATA[&two]]>three</doc>)"}));
      expect(d.text == "one&twothree");
   };

   "line_endings_normalize_in_members"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{"<doc a=\"x\">a\r\nb\rc</doc>"}));
      expect(d.text == "a\nb\nc");
   };

   "attribute_normalization_lands_in_the_member"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{"<doc a=\"x\ty\nz\">t</doc>"}));
      expect(d.a == "x y z");
   };

   "repeated_elements_land_in_order"_test = [] {
      conf_list l{};
      const std::string xml = "<list><item>a</item><item>b</item><item>c</item></list>";
      expect(!glz::read_xml(l, xml));
      expect(l.item == std::vector<std::string>{"a", "b", "c"});
   };

   "comments_and_pis_do_not_pollute_text"_test = [] {
      conf_doc d{};
      expect(!glz::read_xml(d, std::string{R"(<doc a="x">one<!-- c -->two<?pi d?>three</doc>)"}));
      expect(d.text == "onetwothree");
   };

   "prolog_does_not_affect_binding"_test = [] {
      conf_doc d{};
      const std::string xml =
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<!DOCTYPE doc [<!ELEMENT doc (#PCDATA)>]>"
         "<!-- lead -->"
         R"(<doc a="v">t</doc>)";
      expect(!glz::read_xml(d, xml)) << xml;
      expect(d.a == "v");
      expect(d.text == "t");
   };

   "roundtrip_stability"_test = [] {
      const conf_doc original{.a = "a&b\"c", .text = "x<y]]>z"};
      const auto xml = glz::write_xml(original, "doc").value_or("<error>");
      conf_doc parsed{};
      const auto ec = glz::read_xml(parsed, xml);
      expect(!ec) << glz::format_error(ec, xml);
      expect(parsed.a == original.a);
      expect(parsed.text == original.text);
   };
};

int main() {}
