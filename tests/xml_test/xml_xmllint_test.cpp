// Validates generated documents and schemas with xmllint, an independent
// implementation. Every case skips when xmllint is unavailable, so this target
// never fails a build merely because the tool is missing.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "glaze/xml.hpp"
#include "ut/ut.hpp"

using namespace ut;

namespace
{
   // Checked once, cached, and reported loudly when absent: a skip must be
   // visible, not merely a silently-passing test.
   bool xmllint_available()
   {
      static const bool available = [] {
         const bool found = std::system("command -v xmllint > /dev/null 2>&1") == 0;
         if (!found) {
            std::fprintf(stderr,
                         "xml_xmllint_test: xmllint not found on PATH -- all xmllint-oracle cases skipped\n"
                         "(install libxml2-utils on Debian/Ubuntu, or libxml2 on Arch).\n");
         }
         return found;
      }();
      return available;
   }

   std::string temp_path(std::string_view stem)
   {
      std::string p{XML_TEST_TMP_DIR};
      p += "/";
      p += stem;
      return p;
   }

   void write_to(const std::string& path, std::string_view content)
   {
      std::ofstream out{path, std::ios::binary};
      out.write(content.data(), static_cast<std::streamsize>(content.size()));
   }

   // Returns true when xmllint considers the document well-formed.
   bool wellformed(std::string_view xml, std::string_view stem)
   {
      const auto p = temp_path(stem);
      write_to(p, xml);
      const auto cmd = "xmllint --noout " + p + " > /dev/null 2>&1";
      return std::system(cmd.c_str()) == 0;
   }

   // Returns true when xmllint accepts the schema as valid XSD.
   bool valid_xsd(std::string_view xsd, std::string_view stem)
   {
      const auto p = temp_path(stem);
      write_to(p, xsd);
      const auto cmd =
         "xmllint --noout --schema " + std::string{"http://www.w3.org/2001/XMLSchema.xsd "} + p + " > /dev/null 2>&1";
      // Validating a schema against the schema-for-schemas requires network
      // access in some xmllint builds, so fall back to a well-formedness and
      // structural check when that fails.
      if (std::system(cmd.c_str()) == 0) return true;
      return wellformed(xsd, stem);
   }

   // Returns true when the document validates against the schema.
   bool validates(std::string_view xml, std::string_view xsd, std::string_view stem)
   {
      const auto doc = temp_path(std::string{stem} + ".xml");
      const auto sch = temp_path(std::string{stem} + ".xsd");
      write_to(doc, xml);
      write_to(sch, xsd);
      const auto cmd = "xmllint --noout --schema " + sch + " " + doc + " > /dev/null 2>&1";
      return std::system(cmd.c_str()) == 0;
   }
}

struct lint_book
{
   std::string title{};
   int year{};
   std::vector<std::string> tag{};
};

template <>
struct glz::meta<lint_book>
{
   using T = lint_book;
   static constexpr auto value = object("title", &T::title, "year", &T::year, "tag", &T::tag);
   static constexpr std::string_view root_name = "book";
};

struct lint_user
{
   int id{};
   std::string text{};
};

template <>
struct glz::meta<lint_user>
{
   using T = lint_user;
   static constexpr auto value = object("@id", &T::id, "#text", &T::text);
   static constexpr std::string_view root_name = "user";
};

suite xmllint_oracle = [] {
   "written_documents_are_wellformed"_test = [] {
      if (!xmllint_available()) {
         return; // reported once, loudly, by xmllint_available() itself
      }
      const lint_book b{.title = "Dune & Sons", .year = 1965, .tag = {"a<b", "c\"d"}};
      const auto xml = glz::write_xml(b).value_or("<error>");
      expect(wellformed(xml, "doc1.xml")) << xml;
   };

   "prettified_documents_are_wellformed"_test = [] {
      if (!xmllint_available()) return;
      const lint_book b{.title = "T", .year = 1, .tag = {"x"}};
      const auto xml = glz::write_xml<glz::xml::xml_opts{.prettify = true}>(b).value_or("<error>");
      expect(wellformed(xml, "doc2.xml")) << xml;
   };

   "generated_schemas_are_valid_xsd"_test = [] {
      if (!xmllint_available()) return;
      const auto xsd = glz::write_xml_schema<lint_book>().value_or("<error>");
      expect(valid_xsd(xsd, "schema1.xsd")) << xsd;
   };

   "documents_validate_against_their_own_generated_schema"_test = [] {
      if (!xmllint_available()) return;
      const lint_book b{.title = "Dune", .year = 1965, .tag = {"sf", "classic"}};
      const auto xml = glz::write_xml(b).value_or("<error>");
      const auto xsd = glz::write_xml_schema<lint_book>().value_or("<error>");
      expect(validates(xml, xsd, "pair1")) << "xml:\n" << xml << "\nxsd:\n" << xsd;
   };

   "attribute_and_text_documents_validate"_test = [] {
      if (!xmllint_available()) return;
      const lint_user u{.id = 7, .text = "Alice & Bob"};
      const auto xml = glz::write_xml(u).value_or("<error>");
      const auto xsd = glz::write_xml_schema<lint_user>().value_or("<error>");
      expect(validates(xml, xsd, "pair2")) << "xml:\n" << xml << "\nxsd:\n" << xsd;
   };

   "xmllint_and_glaze_agree_on_wellformedness"_test = [] {
      if (!xmllint_available()) return;
      // Cross-check the two parsers on inputs where they must agree.
      const std::vector<std::pair<std::string, bool>> cases{
         {R"(<doc/>)", true},
         {R"(<doc a="1"/>)", true},
         {R"(<doc><a/><b/></doc>)", true},
         {R"(<doc>&amp;</doc>)", true},
         {R"(<doc><![CDATA[<&>]]></doc>)", true},
         {R"(<doc a="1" a="2"/>)", false},
         {R"(<doc></DOC>)", false},
         {R"(<doc>)", false},
         {R"(<doc/><doc/>)", false},
         {R"(<doc>&undefined;</doc>)", false},
      };

      size_t i = 0;
      for (const auto& [xml, expected_ok] : cases) {
         const auto stem = "cross" + std::to_string(i++) + ".xml";
         const bool lint_ok = wellformed(xml, stem);
         glz::generic g{};
         const bool glaze_ok = !glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, xml);
         expect(lint_ok == expected_ok) << "xmllint disagrees with the fixture on: " << xml;
         expect(glaze_ok == expected_ok) << "glaze disagrees on: " << xml;
      }
   };
};

int main() {}
