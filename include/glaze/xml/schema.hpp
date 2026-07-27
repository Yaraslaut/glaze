// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/xml/common.hpp"
#include "glaze/xml/opts.hpp"
#include "glaze/xml/read.hpp" // xml::detail::text_member_index
#include "glaze/xml/write.hpp" // xml::resolve_root_name

namespace glz::xml
{
   // The built-in XSD type name for a C++ scalar. Dispatches on the exact
   // (decayed) type rather than on size/signedness, because a size-based
   // dispatch would also match plain `char` (typically a distinct type from
   // both `int8_t`/`signed char` and `uint8_t`/`unsigned char`) and wrongly
   // classify it as a byte-sized integer instead of falling through to
   // xs:string.
   template <class T>
   constexpr std::string_view xsd_type_of() noexcept
   {
      using V = std::remove_cvref_t<T>;
      if constexpr (std::same_as<V, bool>) {
         return "xs:boolean";
      }
      else if constexpr (std::same_as<V, int8_t>) {
         return "xs:byte";
      }
      else if constexpr (std::same_as<V, int16_t>) {
         return "xs:short";
      }
      else if constexpr (std::same_as<V, int32_t>) {
         return "xs:int";
      }
      else if constexpr (std::same_as<V, int64_t>) {
         return "xs:long";
      }
      else if constexpr (std::same_as<V, uint8_t>) {
         return "xs:unsignedByte";
      }
      else if constexpr (std::same_as<V, uint16_t>) {
         return "xs:unsignedShort";
      }
      else if constexpr (std::same_as<V, uint32_t>) {
         return "xs:unsignedInt";
      }
      else if constexpr (std::same_as<V, uint64_t>) {
         return "xs:unsignedLong";
      }
      else if constexpr (std::same_as<V, float>) {
         return "xs:float";
      }
      else if constexpr (std::same_as<V, double>) {
         return "xs:double";
      }
      else {
         // Strings and anything else not covered above (e.g. plain `char`,
         // which is a distinct type from int8_t/uint8_t on the platforms
         // Glaze targets) are treated as text.
         return "xs:string";
      }
   }

   namespace detail
   {
      // Strips one level of nullable wrapping (std::optional<T> -> T) for the
      // purpose of picking the underlying XSD scalar type. Unlike the JSON
      // schema generator's unwrap_nullable, this does not need to recurse
      // through nested nullables -- Glaze's XML members are never
      // optional<optional<T>> in practice, and the brief only requires one
      // level.
      template <class T>
      struct schema_unwrap_nullable
      {
         using type = T;
      };

      template <nullable_like T>
      struct schema_unwrap_nullable<T>
      {
         using type = std::remove_cvref_t<decltype(*std::declval<T&>())>;
      };

      template <class T>
      using schema_unwrap_nullable_t = typename schema_unwrap_nullable<T>::type;

      // <xs:attribute name="..." type="..." use="optional|required"/>
      template <class ValT, class B>
      void write_xsd_attribute(std::string_view name, is_context auto& ctx, B& b, auto& ix)
      {
         using V = schema_unwrap_nullable_t<ValT>;
         append_raw("<xs:attribute name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\" type=\"", ctx, b, ix);
         append_raw(xsd_type_of<V>(), ctx, b, ix);
         if constexpr (nullable_like<ValT>) {
            append_raw("\" use=\"optional\"/>", ctx, b, ix);
         }
         else {
            append_raw("\" use=\"required\"/>", ctx, b, ix);
         }
      }

      // <xs:element name="..." type="..."/> with cardinality attributes for
      // nullable_like (minOccurs="0") and sequence (minOccurs="0"
      // maxOccurs="unbounded") members. A plain member gets no occurrence
      // attributes at all -- XSD defaults to exactly one.
      template <class ValT, class B>
      void write_xsd_element(std::string_view name, is_context auto& ctx, B& b, auto& ix)
      {
         append_raw("<xs:element name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\" type=\"", ctx, b, ix);
         if constexpr (writable_array_t<ValT>) {
            append_raw(xsd_type_of<std::remove_cvref_t<range_value_t<ValT>>>(), ctx, b, ix);
            append_raw("\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>", ctx, b, ix);
         }
         else if constexpr (nullable_like<ValT>) {
            append_raw(xsd_type_of<schema_unwrap_nullable_t<ValT>>(), ctx, b, ix);
            append_raw("\" minOccurs=\"0\"/>", ctx, b, ix);
         }
         else {
            append_raw(xsd_type_of<ValT>(), ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
      }

      // Emits the <xs:complexType> body for a reflected object T, partitioning
      // members by key sigil exactly as the XML writer does: attributes
      // ("@name"), a single "#text" member (switches to simpleContent), and
      // everything else (child elements in a sequence).
      template <class T, class B>
      void write_xsd_complex_type(is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr size_t N = reflect<T>::size;
         static constexpr size_t text_idx = text_member_index<T>();
         static constexpr bool has_text = text_idx < N;

         append_raw("<xs:complexType>", ctx, b, ix);

         if constexpr (has_text) {
            using TextValT = field_t<T, text_idx>;
            append_raw("<xs:simpleContent><xs:extension base=\"", ctx, b, ix);
            append_raw(xsd_type_of<schema_unwrap_nullable_t<TextValT>>(), ctx, b, ix);
            append_raw("\">", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (xml::is_attribute_key(key)) {
                  using ValT = field_t<T, I>;
                  write_xsd_attribute<ValT>(xml::strip_sigil(key), ctx, b, ix);
               }
            });

            append_raw("</xs:extension></xs:simpleContent>", ctx, b, ix);
         }
         else {
            append_raw("<xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (!xml::is_attribute_key(key) && !xml::is_text_key(key)) {
                  using ValT = field_t<T, I>;
                  write_xsd_element<ValT>(key, ctx, b, ix);
               }
            });

            append_raw("</xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (xml::is_attribute_key(key)) {
                  using ValT = field_t<T, I>;
                  write_xsd_attribute<ValT>(xml::strip_sigil(key), ctx, b, ix);
               }
            });
         }

         append_raw("</xs:complexType>", ctx, b, ix);
      }
   }
}

namespace glz
{
   // Generates a W3C XML Schema (XSD) document describing the shape write_xml
   // would produce for T: a top-level <xs:element> named by
   // xml::resolve_root_name<T>, wrapping a single <xs:complexType> built from
   // T's reflected members (see xml::detail::write_xsd_complex_type).
   template <class T, auto Opts = xml::xml_opts{}, class Buffer>
   [[nodiscard]] error_ctx write_xml_schema(Buffer&& buffer)
   {
      using V = std::decay_t<T>;

      xml::xml_context ctx{};
      size_t ix = 0;

      const auto root_name = xml::resolve_root_name<V>("root");
      if constexpr (xml::check_validate_names(Opts)) {
         if (!xml::validate_name(root_name)) [[unlikely]] {
            return error_ctx{0, error_code::syntax_error, "root name is not a valid XML Name"};
         }
      }

      xml::detail::append_raw("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", ctx, buffer, ix);
      xml::detail::append_raw("<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n", ctx, buffer, ix);
      xml::detail::append_raw("  <xs:element name=\"", ctx, buffer, ix);
      xml::detail::append_raw(root_name, ctx, buffer, ix);
      xml::detail::append_raw("\">\n    ", ctx, buffer, ix);

      xml::detail::write_xsd_complex_type<V>(ctx, buffer, ix);

      xml::detail::append_raw("\n  </xs:element>\n</xs:schema>", ctx, buffer, ix);

      if (bool(ctx.error)) [[unlikely]] {
         return error_ctx{ix, ctx.error, ctx.custom_error_message};
      }
      buffer_traits<std::remove_cvref_t<Buffer>>::finalize(buffer, ix);
      return error_ctx{ix, error_code::none, ctx.custom_error_message};
   }

   template <class T, auto Opts = xml::xml_opts{}>
   [[nodiscard]] glz::expected<std::string, error_ctx> write_xml_schema()
   {
      std::string buffer{};
      const error_ctx ec = write_xml_schema<T, Opts>(buffer);
      if (bool(ec)) [[unlikely]] {
         return glz::unexpected(ec);
      }
      return {buffer};
   }
}
