// Glaze Library
// For the license information refer to glaze.hpp

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "glaze/json/schema.hpp"
#include "glaze/xml.hpp"
#include "ut/ut.hpp"

using namespace ut;

namespace
{
   // Substring assertion keeps these tests robust against incidental
   // whitespace and attribute-order changes in the generated schema.
   bool has(std::string_view haystack, std::string_view needle)
   {
      return haystack.find(needle) != std::string_view::npos;
   }
}

struct sch_book
{
   std::string title{};
   int year{};
};

template <>
struct glz::meta<sch_book>
{
   using T = sch_book;
   static constexpr auto value = object("title", &T::title, "year", &T::year);
   static constexpr std::string_view root_name = "book";
};

struct sch_wide
{
   bool b{};
   int8_t i8{};
   int16_t i16{};
   int32_t i32{};
   int64_t i64{};
   uint8_t u8{};
   uint16_t u16{};
   uint32_t u32{};
   uint64_t u64{};
   float f{};
   double d{};
   std::string s{};
};

template <>
struct glz::meta<sch_wide>
{
   using T = sch_wide;
   static constexpr auto value =
      object("b", &T::b, "i8", &T::i8, "i16", &T::i16, "i32", &T::i32, "i64", &T::i64, "u8", &T::u8, "u16", &T::u16,
             "u32", &T::u32, "u64", &T::u64, "f", &T::f, "d", &T::d, "s", &T::s);
};

struct sch_card
{
   std::optional<int> maybe{};
   std::vector<std::string> many{};
   int required{};
};

template <>
struct glz::meta<sch_card>
{
   using T = sch_card;
   static constexpr auto value = object("maybe", &T::maybe, "many", &T::many, "required", &T::required);
};

struct sch_attrs
{
   int id{};
   std::string role{};
   std::string text{};
};

template <>
struct glz::meta<sch_attrs>
{
   using T = sch_attrs;
   static constexpr auto value = object("@id", &T::id, "@role", &T::role, "#text", &T::text);
   static constexpr std::string_view root_name = "user";
};

suite xsd_basics = [] {
   "schema_is_a_wellformed_xsd_document"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_book>().value_or("<error>");
      expect(has(xsd, R"(<?xml version="1.0" encoding="UTF-8"?>)")) << xsd;
      expect(has(xsd, R"(xmlns:xs="http://www.w3.org/2001/XMLSchema")")) << xsd;
      expect(has(xsd, "<xs:schema")) << xsd;
      expect(has(xsd, "</xs:schema>")) << xsd;
   };

   "schema_is_parseable_by_our_own_reader"_test = [] {
      // The generator must never emit something we would ourselves reject.
      const auto xsd = glz::write_xml_schema<sch_book>().value_or("<error>");
      glz::generic g{};
      const auto ec = glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, xsd);
      expect(!ec) << glz::format_error(ec, xsd);
   };

   "root_element_uses_meta_root_name"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_book>().value_or("<error>");
      expect(has(xsd, R"(<xs:element name="book")")) << xsd;
   };

   "child_elements_declared_with_types"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_book>().value_or("<error>");
      expect(has(xsd, "<xs:sequence>")) << xsd;
      expect(has(xsd, R"(<xs:element name="title" type="xs:string")")) << xsd;
      expect(has(xsd, R"(<xs:element name="year" type="xs:int")")) << xsd;
   };

   "scalar_type_mapping"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_wide>().value_or("<error>");
      expect(has(xsd, R"(name="b" type="xs:boolean")")) << xsd;
      expect(has(xsd, R"(name="i8" type="xs:byte")")) << xsd;
      expect(has(xsd, R"(name="i16" type="xs:short")")) << xsd;
      expect(has(xsd, R"(name="i32" type="xs:int")")) << xsd;
      expect(has(xsd, R"(name="i64" type="xs:long")")) << xsd;
      expect(has(xsd, R"(name="u8" type="xs:unsignedByte")")) << xsd;
      expect(has(xsd, R"(name="u16" type="xs:unsignedShort")")) << xsd;
      expect(has(xsd, R"(name="u32" type="xs:unsignedInt")")) << xsd;
      expect(has(xsd, R"(name="u64" type="xs:unsignedLong")")) << xsd;
      expect(has(xsd, R"(name="f" type="xs:float")")) << xsd;
      expect(has(xsd, R"(name="d" type="xs:double")")) << xsd;
      expect(has(xsd, R"(name="s" type="xs:string")")) << xsd;
   };

   "cardinality"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_card>().value_or("<error>");
      // optional -> minOccurs="0"
      expect(has(xsd, R"(name="maybe" type="xs:int" minOccurs="0")")) << xsd;
      // vector -> unbounded, and absent is legal
      expect(has(xsd, R"(name="many" type="xs:string" minOccurs="0" maxOccurs="unbounded")")) << xsd;
      // plain member -> no occurrence attributes (XSD defaults to exactly one)
      expect(has(xsd, R"(<xs:element name="required" type="xs:int"/>)")) << xsd;
   };

   "attributes_and_simple_content"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_attrs>().value_or("<error>");
      // A #text member forces simpleContent with an extension base.
      expect(has(xsd, "<xs:simpleContent>")) << xsd;
      expect(has(xsd, R"(<xs:extension base="xs:string">)")) << xsd;
      expect(has(xsd, R"(<xs:attribute name="id" type="xs:int")")) << xsd;
      expect(has(xsd, R"(<xs:attribute name="role" type="xs:string")")) << xsd;
      // Attributes must not appear as elements.
      expect(!has(xsd, R"(<xs:element name="id")")) << xsd;
      expect(!has(xsd, R"(<xs:element name="@id")")) << xsd;
      // The sigil never leaks into the schema.
      expect(!has(xsd, "@")) << xsd;
      expect(!has(xsd, "#text")) << xsd;
   };

   "buffer_overload"_test = [] {
      std::string buf;
      const auto ec = glz::write_xml_schema<sch_book>(buf);
      expect(!ec);
      expect(has(buf, "<xs:schema"));
   };
};

enum struct sch_color { red, green, blue };

template <>
struct glz::meta<sch_color>
{
   static constexpr auto value = enumerate(sch_color::red, sch_color::green, sch_color::blue);
   static constexpr std::string_view name = "sch_color";
};

struct sch_outer
{
   sch_book book{};
   sch_color color{};
};

template <>
struct glz::meta<sch_outer>
{
   using T = sch_outer;
   static constexpr auto value = object("book", &T::book, "color", &T::color);
   static constexpr std::string_view root_name = "outer";
};

struct sch_variant
{
   std::variant<int, std::string> v{};
};

template <>
struct glz::meta<sch_variant>
{
   using T = sch_variant;
   static constexpr auto value = object("v", &T::v);
};

// Recursive type: the generator must terminate by emitting a named type and
// referring to it, rather than inlining forever.
struct sch_node
{
   std::string name{};
   std::vector<sch_node> child{};
};

template <>
struct glz::meta<sch_node>
{
   using T = sch_node;
   static constexpr auto value = object("name", &T::name, "child", &T::child);
   static constexpr std::string_view root_name = "node";
};

suite xsd_composite = [] {
   "nested_struct_becomes_a_named_complextype"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_outer>().value_or("<error>");
      // The nested type is defined once at top level...
      expect(has(xsd, R"(<xs:complexType name="sch_book">)")) << xsd;
      // ...and referenced by the member.
      expect(has(xsd, R"(<xs:element name="book" type="sch_book")")) << xsd;
   };

   "enum_becomes_a_restriction"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_outer>().value_or("<error>");
      expect(has(xsd, R"(<xs:simpleType name="sch_color">)")) << xsd;
      expect(has(xsd, R"(<xs:restriction base="xs:string">)")) << xsd;
      expect(has(xsd, R"(<xs:enumeration value="red"/>)")) << xsd;
      expect(has(xsd, R"(<xs:enumeration value="green"/>)")) << xsd;
      expect(has(xsd, R"(<xs:enumeration value="blue"/>)")) << xsd;
      expect(has(xsd, R"(<xs:element name="color" type="sch_color")")) << xsd;
   };

   "variant_becomes_a_choice"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_variant>().value_or("<error>");
      expect(has(xsd, "<xs:choice>")) << xsd;
      expect(has(xsd, R"(type="xs:int")")) << xsd;
      expect(has(xsd, R"(type="xs:string")")) << xsd;
   };

   "recursive_type_terminates"_test = [] {
      // If this hangs or blows the stack, the named-type indirection is missing.
      const auto xsd = glz::write_xml_schema<sch_node>().value_or("<error>");
      expect(has(xsd, R"(<xs:complexType name="sch_node">)")) << xsd;
      expect(has(xsd, R"(<xs:element name="child" type="sch_node")")) << xsd;
      expect(has(xsd, R"(maxOccurs="unbounded")")) << xsd;
   };

   "each_named_type_is_defined_exactly_once"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_outer>().value_or("<error>");
      size_t count = 0;
      for (size_t pos = 0; (pos = xsd.find(R"(<xs:complexType name="sch_book">)", pos)) != std::string::npos; ++pos) {
         ++count;
      }
      expect(count == size_t(1)) << "duplicate type definitions make the schema invalid: " << xsd;
   };

   "composite_schema_is_still_parseable"_test = [] {
      for (const auto& xsd : {glz::write_xml_schema<sch_outer>().value_or("<error>"),
                              glz::write_xml_schema<sch_variant>().value_or("<error>"),
                              glz::write_xml_schema<sch_node>().value_or("<error>")}) {
         glz::generic g{};
         const auto ec = glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, xsd);
         expect(!ec) << glz::format_error(ec, xsd);
      }
   };
};

struct sch_faceted
{
   int count{};
   std::string code{};
   double ratio{};
   std::string when{};
};

template <>
struct glz::meta<sch_faceted>
{
   using T = sch_faceted;
   static constexpr auto value = object("count", &T::count, "code", &T::code, "ratio", &T::ratio, "when", &T::when);
   static constexpr std::string_view root_name = "faceted";
};

// The SAME specialization feeds both the JSON Schema and the XSD generator.
template <>
struct glz::json_schema<sch_faceted>
{
   schema count{.description = "how many", .minimum = 1L, .maximum = 100L};
   schema code{.description = "an identifier", .minLength = 2, .maxLength = 8, .pattern = "[a-z]+"};
   schema ratio{.exclusiveMinimum = 0.0, .exclusiveMaximum = 1.0, .multipleOf = 0.25};
   schema when{.format = glz::detail::defined_formats::datetime};
};

suite xsd_facets = [] {
   "description_becomes_documentation"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      expect(has(xsd, "<xs:annotation>")) << xsd;
      expect(has(xsd, "<xs:documentation>how many</xs:documentation>")) << xsd;
      expect(has(xsd, "<xs:documentation>an identifier</xs:documentation>")) << xsd;
   };

   "numeric_bounds_become_facets"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      expect(has(xsd, R"(<xs:minInclusive value="1"/>)")) << xsd;
      expect(has(xsd, R"(<xs:maxInclusive value="100"/>)")) << xsd;
      expect(has(xsd, R"(<xs:minExclusive value="0"/>)")) << xsd;
      expect(has(xsd, R"(<xs:maxExclusive value="1"/>)")) << xsd;
   };

   "string_facets"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      expect(has(xsd, R"(<xs:minLength value="2"/>)")) << xsd;
      expect(has(xsd, R"(<xs:maxLength value="8"/>)")) << xsd;
      expect(has(xsd, R"(<xs:pattern value="[a-z]+"/>)")) << xsd;
   };

   "format_maps_to_a_builtin_type"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      expect(has(xsd, R"(name="when" type="xs:dateTime")")) << xsd;
   };

   "facets_with_no_xsd_equivalent_are_preserved_not_dropped"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      // XSD has no multipleOf facet, so it must survive in appinfo.
      expect(has(xsd, "<xs:appinfo>")) << xsd;
      expect(has(xsd, "multipleOf")) << xsd;
      expect(has(xsd, "0.25")) << xsd;
   };

   "a_faceted_member_uses_an_inline_simpletype"_test = [] {
      // A facet-bearing member cannot use a bare built-in type attribute; it
      // needs an inline restriction.
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      expect(has(xsd, R"(<xs:restriction base="xs:int">)")) << xsd;
      expect(has(xsd, R"(<xs:restriction base="xs:string">)")) << xsd;
   };

   "faceted_schema_is_still_parseable"_test = [] {
      const auto xsd = glz::write_xml_schema<sch_faceted>().value_or("<error>");
      glz::generic g{};
      const auto ec = glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, xsd);
      expect(!ec) << glz::format_error(ec, xsd);
   };

   "json_schema_generation_is_unaffected"_test = [] {
      // The shared glz::json_schema specialization must still drive JSON Schema
      // exactly as before.
      const auto js = glz::write_json_schema<sch_faceted>().value_or("");
      expect(has(js, "how many")) << js;
      expect(has(js, "multipleOf")) << js;
   };
};

int main() {}
