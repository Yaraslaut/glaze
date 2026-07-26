// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/xml.hpp"

#include <cstdint>

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

int main() {}
