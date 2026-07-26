// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstdint>

#include "glaze/core/opts.hpp"

namespace glz::xml
{
   enum struct opts_internal : uint32_t {
      none = 0,
      inside_element = 1 << 0, // Currently writing inside an open element
      attribute_pass = 1 << 1, // Writing the attribute portion of a start tag
   };

   // NOTE: every member must be a structural type. std::string_view is NOT
   // structural and must never be added here — the root element name is
   // resolved via glz::meta<T>::root_name or a runtime argument instead.
   struct xml_opts
   {
      uint32_t format = XML;
      bool error_on_unknown_keys{true};
      bool error_on_missing_keys{false};
      bool skip_null_members{true};
      bool write_declaration{true}; // <?xml version="1.0" encoding="UTF-8"?>
      bool prettify{false};
      uint8_t indentation_width{3};
      bool validate_names{true};

      uint32_t internal{};
   };

   consteval bool check_inside_element(auto&& o) { return o.internal & uint32_t(opts_internal::inside_element); }

   consteval bool check_attribute_pass(auto&& o) { return o.internal & uint32_t(opts_internal::attribute_pass); }

   template <auto Opts>
   constexpr auto inside_element_on()
   {
      auto ret = Opts;
      ret.internal |= uint32_t(opts_internal::inside_element);
      return ret;
   }

   template <auto Opts>
   constexpr auto attribute_pass_on()
   {
      auto ret = Opts;
      ret.internal |= uint32_t(opts_internal::attribute_pass);
      return ret;
   }

   template <auto Opts>
   constexpr auto attribute_pass_off()
   {
      auto ret = Opts;
      ret.internal &= ~uint32_t(opts_internal::attribute_pass);
      return ret;
   }

   consteval bool check_write_declaration(auto&& o)
   {
      if constexpr (requires { o.write_declaration; }) {
         return o.write_declaration;
      }
      else {
         return true;
      }
   }

   consteval uint8_t check_indentation_width(auto&& o)
   {
      if constexpr (requires { o.indentation_width; }) {
         return o.indentation_width;
      }
      else {
         return 3;
      }
   }

   consteval bool check_prettify(auto&& o)
   {
      if constexpr (requires { o.prettify; }) {
         return o.prettify;
      }
      else {
         return false;
      }
   }

   consteval bool check_validate_names(auto&& o)
   {
      if constexpr (requires { o.validate_names; }) {
         return o.validate_names;
      }
      else {
         return true;
      }
   }
}
