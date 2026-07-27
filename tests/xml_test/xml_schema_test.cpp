// Glaze Library
// For the license information refer to glaze.hpp

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

int main() {}
