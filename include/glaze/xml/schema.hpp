// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/variant.hpp"
#include "glaze/xml/common.hpp"
#include "glaze/xml/opts.hpp"
#include "glaze/xml/read.hpp" // xml::detail::text_member_index
#include "glaze/xml/write.hpp" // xml::resolve_root_name, xml::detail::xml_reflected_object

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

      // ------------------------------------------------------------------
      // Name sanitization: glz::name_v<T> is the exact declared meta name
      // for a struct/enum with glz::meta, but falls back to a compiler
      // pretty-printed type name (e.g. "std::vector<user>") for anything
      // without one. Such names contain characters that are legal XML Name
      // characters (':') or fail xml::is_name_char entirely ('<', '>', ',',
      // ' ') and so are not valid XSD NCNames. We replace every character
      // that is not both a valid xml::is_name_char AND not ':' (NCName
      // excludes the colon that Name otherwise allows) with '_', and, since
      // digits/'-'/'.' are valid NameChars but not valid NameStartChars,
      // force the first character to '_' if it would otherwise leave the
      // name starting with one of those. The result is always a valid
      // NCName -- asserted below rather than merely assumed.
      template <size_t N>
      consteval std::array<char, N + 1> sanitize_name_chars(std::string_view raw) noexcept
      {
         std::array<char, N + 1> out{};
         for (size_t i = 0; i < N; ++i) {
            const auto cp = static_cast<char32_t>(static_cast<unsigned char>(raw[i]));
            const bool ok = cp != U':' && xml::is_name_char(cp);
            out[i] = ok ? raw[i] : '_';
         }
         if constexpr (N > 0) {
            const auto first_cp = static_cast<char32_t>(static_cast<unsigned char>(out[0]));
            if (!xml::is_name_start_char(first_cp)) {
               out[0] = '_';
            }
         }
         out[N] = '\0';
         return out;
      }

      template <class T>
      inline constexpr auto sanitized_name_storage = sanitize_name_chars<glz::name_v<T>.size()>(glz::name_v<T>);

      template <class T>
      inline constexpr std::string_view sanitized_name_v = [] {
         constexpr std::string_view s{sanitized_name_storage<T>.data(), glz::name_v<T>.size()};
         static_assert(xml::validate_ncname(s), "sanitized XSD type name is not a valid NCName");
         return s;
      }();

      // Is T itself something that gets its own top-level named XSD type
      // definition (an <xs:complexType name="..."> or <xs:simpleType
      // name="...">)? Scalars, variants, and container/nullable wrappers do
      // not -- they are always resolved to a reference at their point of use.
      template <class T>
      concept named_xsd_type = xml_reflected_object<T> || glaze_enum_t<T>;

      // Resolves the XSD "type" attribute value for a (already nullable/array
      // unwrapped) member type: a sanitized named-type reference for structs
      // and enums, the built-in scalar mapping for everything else.
      template <class D, class B>
      void write_type_ref(is_context auto& ctx, B& b, auto& ix)
      {
         if constexpr (named_xsd_type<D>) {
            append_raw(sanitized_name_v<D>, ctx, b, ix);
         }
         else {
            append_raw(xsd_type_of<D>(), ctx, b, ix);
         }
      }

      // XSD cannot express "exactly one of these, all sharing the same
      // element name" (see xml/write.hpp's variant writer: every alternative
      // of a variant member is written under the *same* element name,
      // distinguished only by the shape of its content). We approximate this
      // with <xs:choice>, giving each alternative its own element name (its
      // sanitized type name) so the choice's particles are distinguishable --
      // an accepted, documented limitation (see docs/xml-schema.md).
      template <class D, class B>
      void write_xsd_variant_element(std::string_view name, is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr size_t N = std::variant_size_v<D>;
         append_raw("<xs:element name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\"><xs:complexType><xs:choice>", ctx, b, ix);
         for_each<N>([&]<size_t I>() {
            using Alt = std::remove_cvref_t<std::variant_alternative_t<I, D>>;
            append_raw("<xs:element name=\"", ctx, b, ix);
            append_raw(sanitized_name_v<Alt>, ctx, b, ix);
            append_raw("\" type=\"", ctx, b, ix);
            write_type_ref<Alt>(ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         });
         append_raw("</xs:choice></xs:complexType></xs:element>", ctx, b, ix);
      }

      template <class ValT, class B>
      void write_xsd_attribute(std::string_view name, is_context auto& ctx, B& b, auto& ix)
      {
         using V = schema_unwrap_nullable_t<ValT>;
         append_raw("<xs:attribute name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\" type=\"", ctx, b, ix);
         write_type_ref<V>(ctx, b, ix);
         if constexpr (nullable_like<ValT>) {
            append_raw("\" use=\"optional\"/>", ctx, b, ix);
         }
         else {
            append_raw("\" use=\"required\"/>", ctx, b, ix);
         }
      }

      template <class ValT, class B>
      void write_xsd_element(std::string_view name, is_context auto& ctx, B& b, auto& ix)
      {
         using D = std::remove_cvref_t<ValT>;
         if constexpr (is_variant<D>) {
            write_xsd_variant_element<D>(name, ctx, b, ix);
         }
         else {
            append_raw("<xs:element name=\"", ctx, b, ix);
            append_raw(name, ctx, b, ix);
            append_raw("\" type=\"", ctx, b, ix);
            if constexpr (writable_array_t<D>) {
               write_type_ref<std::remove_cvref_t<range_value_t<D>>>(ctx, b, ix);
               append_raw("\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>", ctx, b, ix);
            }
            else if constexpr (nullable_like<D>) {
               write_type_ref<schema_unwrap_nullable_t<D>>(ctx, b, ix);
               append_raw("\" minOccurs=\"0\"/>", ctx, b, ix);
            }
            else {
               write_type_ref<D>(ctx, b, ix);
               append_raw("\"/>", ctx, b, ix);
            }
         }
      }

      // Emits the body of a <xs:complexType> for a reflected object T (the
      // part between the opening tag -- named or anonymous -- and its
      // closing tag), partitioning members by key sigil exactly as the XML
      // writer does: attributes ("@name"), a single "#text" member (switches
      // to simpleContent), and everything else (child elements in a
      // sequence).
      template <class T, class B>
      void write_xsd_complex_type_body(is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr size_t N = reflect<T>::size;
         static constexpr size_t text_idx = text_member_index<T>();
         static constexpr bool has_text = text_idx < N;

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
      }

      // <xs:complexType name="..."> ... </xs:complexType>
      template <class T, class B>
      void emit_named_complex_type(xml::xml_context& ctx, B& b, size_t& ix)
      {
         append_raw("<xs:complexType name=\"", ctx, b, ix);
         append_raw(sanitized_name_v<T>, ctx, b, ix);
         append_raw("\">", ctx, b, ix);
         write_xsd_complex_type_body<T>(ctx, b, ix);
         append_raw("</xs:complexType>", ctx, b, ix);
      }

      // <xs:simpleType name="..."> ... </xs:simpleType> -- an enum becomes a
      // string restriction enumerating each declared enumerator name, in
      // declaration order.
      template <class T, class B>
      void emit_named_simple_type(xml::xml_context& ctx, B& b, size_t& ix)
      {
         static constexpr size_t N = reflect<T>::size;

         append_raw("<xs:simpleType name=\"", ctx, b, ix);
         append_raw(sanitized_name_v<T>, ctx, b, ix);
         append_raw("\"><xs:restriction base=\"xs:string\">", ctx, b, ix);

         for_each<N>([&]<size_t I>() {
            append_raw("<xs:enumeration value=\"", ctx, b, ix);
            append_raw(reflect<T>::keys[I], ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         });

         append_raw("</xs:restriction></xs:simpleType>", ctx, b, ix);
      }

      // ------------------------------------------------------------------
      // Collect pass: walks T transitively (through structs, enums, arrays,
      // nullables, and variant alternatives) and gathers every distinct
      // struct/enum type reachable from it -- including T itself -- into an
      // ordered list keyed by its sanitized glz::name_v. A type already
      // present is neither re-added nor re-descended-into, which is what
      // makes a self-referential type (e.g. one holding a vector of itself)
      // terminate: the entry for T is appended to `order` *before* T's
      // members are inspected, so a member that leads back to T finds it
      // already present and stops immediately.
      template <class B>
      struct named_type_entry
      {
         std::string_view name{};
         void (*emit)(xml::xml_context&, B&, size_t&){};
      };

      template <class T, class B>
      void collect_named_types(std::vector<named_type_entry<B>>& order)
      {
         using V = std::remove_cvref_t<T>;

         if constexpr (nullable_like<V>) {
            collect_named_types<schema_unwrap_nullable_t<V>, B>(order);
         }
         else if constexpr (writable_array_t<V>) {
            collect_named_types<std::remove_cvref_t<range_value_t<V>>, B>(order);
         }
         else if constexpr (is_variant<V>) {
            static constexpr size_t N = std::variant_size_v<V>;
            for_each<N>([&]<size_t I>() {
               collect_named_types<std::remove_cvref_t<std::variant_alternative_t<I, V>>, B>(order);
            });
         }
         else if constexpr (glaze_enum_t<V>) {
            const std::string_view name = sanitized_name_v<V>;
            for (const auto& entry : order) {
               if (entry.name == name) return;
            }
            order.push_back(named_type_entry<B>{name, &emit_named_simple_type<V, B>});
         }
         else if constexpr (xml_reflected_object<V>) {
            const std::string_view name = sanitized_name_v<V>;
            for (const auto& entry : order) {
               if (entry.name == name) return;
            }
            // Reserve this type's slot before descending into its members so
            // a cycle back to V (directly or through another type) sees it
            // as already collected and stops.
            order.push_back(named_type_entry<B>{name, &emit_named_complex_type<V, B>});

            static constexpr size_t N = reflect<V>::size;
            for_each<N>([&]<size_t I>() { collect_named_types<field_t<V, I>, B>(order); });
         }
         // else: a scalar -- nothing to collect.
      }
   }
}

namespace glz
{
   // Generates a W3C XML Schema (XSD) document describing the shape write_xml
   // would produce for T. Every distinct struct and enum type reachable from
   // T (including T itself) is emitted exactly once as a top-level named
   // <xs:complexType>/<xs:simpleType>, and the document's single root
   // <xs:element> (named by xml::resolve_root_name<T>) refers to T's type by
   // name. See xml::detail::collect_named_types for how recursive types are
   // handled without inlining forever, and xml::detail::write_xsd_element for
   // per-member cardinality/type resolution.
   template <class T, auto Opts = xml::xml_opts{}, class Buffer>
   [[nodiscard]] error_ctx write_xml_schema(Buffer&& buffer)
   {
      using V = std::decay_t<T>;
      using B = std::remove_cvref_t<Buffer>;

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

      std::vector<xml::detail::named_type_entry<B>> order{};
      xml::detail::collect_named_types<V, B>(order);
      for (const auto& entry : order) {
         entry.emit(ctx, buffer, ix);
         xml::detail::append_raw("\n", ctx, buffer, ix);
      }

      xml::detail::append_raw("  <xs:element name=\"", ctx, buffer, ix);
      xml::detail::append_raw(root_name, ctx, buffer, ix);
      xml::detail::append_raw("\" type=\"", ctx, buffer, ix);
      xml::detail::append_raw(xml::detail::sanitized_name_v<V>, ctx, buffer, ix);
      xml::detail::append_raw("\"/>\n</xs:schema>", ctx, buffer, ix);

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
