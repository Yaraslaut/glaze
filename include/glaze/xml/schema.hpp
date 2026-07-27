// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/core/schema.hpp" // glz::schema, glz::detail::defined_formats
#include "glaze/core/write_chars.hpp" // glz::write_chars -- numeric facet values
#include "glaze/util/for_each.hpp"
#include "glaze/util/variant.hpp"
#include "glaze/xml/common.hpp"
#include "glaze/xml/opts.hpp"
#include "glaze/xml/read.hpp" // xml::detail::text_member_index
#include "glaze/xml/write.hpp" // xml::resolve_root_name, xml::detail::xml_reflected_object

namespace glz
{
   // XML-only schema metadata for a single member: concepts glz::schema cannot
   // express, because they describe the shape of the *document* (attribute vs.
   // element form, ID/IDREF linkage) rather than value constraints shared with
   // JSON Schema.
   struct xml_schema_field final
   {
      std::optional<std::string_view> xsd_type{}; // e.g. "xs:ID", "xs:IDREF", "xs:NMTOKEN"
      std::optional<bool> as_attribute{}; // force attribute form, overriding the '@' sigil
   };

   // Per-type XML-only schema metadata. Optional -- absent for most types.
   // Left undefined (unlike glz::json_schema<T>, which defaults to an empty
   // body) so that `requires { xml_schema<T>{}; }` can detect whether a
   // specialization exists, exactly as glz::to/glz::from are declared
   // incomplete in glaze/forward.hpp.
   //
   // A specialization declares one glz::xml_schema_field member per struct
   // member it wants to annotate -- matched by member name against
   // glz::meta<T>, the same way glz::json_schema<T> members are matched (see
   // xml::detail::has_member_entry, shared by both) -- plus, optionally,
   // `static constexpr std::string_view target_namespace` and
   // `static constexpr std::string_view namespace_prefix`.
   //
   // Division of responsibility: glz::json_schema<T> supplies annotations
   // shared with JSON Schema (descriptions, numeric/string facets);
   // glz::xml_schema<T> supplies only what glz::schema cannot express. The two
   // are disjoint by construction, so there is no precedence puzzle in the
   // common case. Where both could express the same concept -- currently,
   // only the base XSD type of a member -- glz::xml_schema wins; see
   // xml::detail::xml_type_override.
   template <class T>
   struct xml_schema;
}

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
      // unwrapped) member type: an explicit override (Task 22's
      // xml_schema<T>::xsd_type, resolution step 1) if given, else a
      // sanitized named-type reference for structs and enums, else the
      // built-in scalar mapping (resolution step 3 -- step 2, the
      // glz::json_schema<T> format substitution, is resolved by
      // write_facet_base_type before reaching here).
      template <class D, class B>
      void write_type_ref(is_context auto& ctx, B& b, auto& ix, std::optional<std::string_view> override_type = {})
      {
         if (override_type) {
            append_raw(*override_type, ctx, b, ix);
         }
         else if constexpr (named_xsd_type<D>) {
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

      // ------------------------------------------------------------------
      // Task 21: mapping glz::schema facets (Task 18's shared JSON/XSD
      // metadata type) onto XSD restrictions. The same glz::json_schema<T>
      // (or in-struct glaze_json_schema) specialization that feeds
      // glz::write_json_schema also drives this -- see glz::json_schema_t
      // and glz::json_schema_type in core/meta.hpp for the detection
      // machinery, reused verbatim rather than re-implemented here.
      //
      // Task 22 reuses the member-matching half of that machinery
      // (has_member_entry/member_entry_index below) for glz::xml_schema<T>
      // too, since both are "reflected annotation aggregate, matched by
      // member name against T" lookups differing only in which annotation
      // type is being searched.

      // Shared by glz::json_schema<T> and glz::xml_schema<T>: does member I
      // of T (matched by reflected key name) have a corresponding entry in
      // some other reflected annotation aggregate SchemaT (json_schema_type<T>
      // or xml_schema<T>)?
      template <class SchemaT, class T, size_t I>
      consteval bool has_member_entry()
      {
         constexpr auto schema_size = reflect<SchemaT>::size;
         if constexpr (schema_size > 0) {
            constexpr auto key = reflect<T>::keys[I];
            constexpr auto& schema_keys = reflect<SchemaT>::keys;
            for (size_t i = 0; i < schema_size; ++i) {
               if (schema_keys[i] == key) return true;
            }
         }
         return false;
      }

      // The index of member I's key within SchemaT's own keys. Only valid --
      // and only ever called -- when has_member_entry<SchemaT, T, I>() is true.
      template <class SchemaT, class T, size_t I>
         requires(has_member_entry<SchemaT, T, I>())
      consteval size_t member_entry_index()
      {
         constexpr auto key = reflect<T>::keys[I];
         constexpr auto& schema_keys = reflect<SchemaT>::keys;
         constexpr auto schema_size = reflect<SchemaT>::size;
         for (size_t i = 0; i < schema_size; ++i) {
            if (schema_keys[i] == key) return i;
         }
         return schema_size; // unreachable: has_member_entry<SchemaT, T, I>() guarantees a match
      }

      // Whether member I of T has a corresponding glz::schema entry.
      template <class T, size_t I>
      consteval bool has_member_schema()
      {
         if constexpr (glz::json_schema_t<T>) {
            return has_member_entry<json_schema_type<T>, T, I>();
         }
         return false;
      }

      // The index of member I's key within json_schema_type<T>::keys. Only
      // valid -- and only ever called -- when has_member_schema<T, I>() is
      // true.
      template <class T, size_t I>
         requires(has_member_schema<T, I>())
      consteval size_t member_schema_index()
      {
         return member_entry_index<json_schema_type<T>, T, I>();
      }

      // Copies out the glz::schema authored for member I of T. Mirrors the
      // lookup in glaze/json/schema.hpp's to_json_schema<T>::op, right down
      // to the "static const instance, copy the member out" pattern -- glz::schema
      // holds heap-allocated (boxed) fields, so it cannot be built at compile time.
      template <class T, size_t I>
         requires(has_member_schema<T, I>())
      schema get_member_schema()
      {
         static constexpr auto idx = member_schema_index<T, I>();
         static const auto schema_v = json_schema_type<T>{};
         return get<idx>(to_tie(schema_v));
      }

      // ------------------------------------------------------------------
      // Task 22: glz::xml_schema<T> lookup and resolution.

      // Whether T has a glz::xml_schema<T> specialization at all. The primary
      // template is left undefined (see glz::xml_schema's declaration), so
      // this is false whenever xml_schema<T> is an incomplete type -- an
      // ordinary SFINAE-in-a-requires-expression check, the same technique
      // glz::write_supported/read_supported use for glz::to/glz::from.
      template <class T>
      concept has_xml_schema = requires { glz::xml_schema<T>{}; };

      // Whether member I of T has a corresponding glz::xml_schema_field entry.
      template <class T, size_t I>
      consteval bool has_member_xml_schema()
      {
         if constexpr (has_xml_schema<T>) {
            return has_member_entry<glz::xml_schema<T>, T, I>();
         }
         return false;
      }

      // The index of member I's key within glz::xml_schema<T>'s own keys.
      // Only valid -- and only ever called -- when
      // has_member_xml_schema<T, I>() is true.
      template <class T, size_t I>
         requires(has_member_xml_schema<T, I>())
      consteval size_t member_xml_schema_index()
      {
         return member_entry_index<glz::xml_schema<T>, T, I>();
      }

      // Copies out the glz::xml_schema_field authored for member I of T.
      // Unlike glz::schema, xml_schema_field holds only
      // std::optional<std::string_view>/std::optional<bool> -- both literal
      // types -- so, unlike get_member_schema, this runs entirely at compile
      // time; no boxed-field runtime-copy workaround is needed.
      template <class T, size_t I>
         requires(has_member_xml_schema<T, I>())
      consteval xml_schema_field get_member_xml_schema_field()
      {
         constexpr auto idx = member_xml_schema_index<T, I>();
         constexpr auto v = glz::xml_schema<T>{};
         return get<idx>(to_tie(v));
      }

      // Step 1 of the resolution order (xml_schema<T> xsd_type, if present).
      // Steps 2 (glz::json_schema<T> format substitution) and 3
      // (xsd_type_of<Member>()/named-type default) are resolved where they
      // already lived -- see write_facet_base_type and write_type_ref -- this
      // only ever supplies the highest-priority override, since xml_schema
      // wins whenever both could express the same concept.
      template <class T, size_t I>
      consteval std::optional<std::string_view> xml_type_override()
      {
         if constexpr (has_member_xml_schema<T, I>()) {
            constexpr auto field = get_member_xml_schema_field<T, I>();
            if constexpr (field.xsd_type.has_value()) {
               return field.xsd_type;
            }
         }
         return std::nullopt;
      }

      // Whether member I's attribute-vs-element form is forced by
      // xml_schema<T>::as_attribute, overriding the '@' sigil that ordinarily
      // decides it. There is no corresponding as_element: forcing an
      // attribute-keyed member ("@id") to element form would require a
      // glz::xml_schema<T> member literally named "@id", which is not a valid
      // C++ identifier and so can never be written -- the same trap
      // substitution_group was removed for (see docs/xml-schema.md).
      template <class T, size_t I>
      consteval bool xsd_effective_is_attribute()
      {
         constexpr auto key = reflect<T>::keys[I];
         if constexpr (has_member_xml_schema<T, I>()) {
            constexpr auto field = get_member_xml_schema_field<T, I>();
            if constexpr (field.as_attribute.has_value()) {
               return *field.as_attribute;
            }
            else {
               return xml::is_attribute_key(key);
            }
         }
         else {
            return xml::is_attribute_key(key);
         }
      }

      // Facets that only make sense inside an <xs:restriction> -- their
      // presence is what forces a member from a bare type="..." attribute
      // to an inline <xs:simpleType>.
      inline bool has_restriction_facets(const schema& s) noexcept
      {
         return bool(s.minimum) || bool(s.maximum) || bool(s.exclusiveMinimum) || bool(s.exclusiveMaximum) ||
                bool(s.minLength) || bool(s.maxLength) || s.pattern.has_value() || bool(s.enumeration);
      }

      // The XSD built-in type for a defined_formats value, or empty when
      // XSD has no matching built-in (the caller falls back to xs:string).
      constexpr std::string_view xsd_format_builtin(glz::detail::defined_formats fmt) noexcept
      {
         using enum glz::detail::defined_formats;
         switch (fmt) {
         case datetime:
            return "xs:dateTime";
         case date:
            return "xs:date";
         case time:
            return "xs:time";
         case duration:
            return "xs:duration";
         case uri:
         case uri_reference:
            return "xs:anyURI";
         default:
            return {};
         }
      }

      // A best-effort xs:pattern approximation for a defined_formats value
      // that has no XSD built-in type. These are intentionally loose --
      // the point is to give the reader *something* to validate against,
      // not to fully replicate JSON Schema's format semantics.
      constexpr std::string_view format_pattern_approximation(glz::detail::defined_formats fmt) noexcept
      {
         using enum glz::detail::defined_formats;
         switch (fmt) {
         case email:
         case idn_email:
            return R"([^@\s]+@[^@\s]+)";
         case hostname:
         case idn_hostname:
            return R"([^\s]+)";
         case ipv4:
            return R"(\d{1,3}(\.\d{1,3}){3})";
         case ipv6:
            return R"([0-9a-fA-F:]+)";
         case uuid:
            return R"([0-9a-fA-F-]{36})";
         default:
            return R"(.*)";
         }
      }

      // Writes a schema_number's held alternative (int64_t/uint64_t/double) as
      // raw digits -- numbers cannot contain XML metacharacters, so no escaping
      // is needed, only the same ensure_space growth every writer here uses.
      template <class B>
      void write_schema_number(const schema::schema_number& n, is_context auto& ctx, B& b, auto& ix)
      {
         if (!ensure_space(ctx, b, ix + 64 + write_padding_bytes)) [[unlikely]] {
            return;
         }
         std::visit([&](auto&& v) { write_chars::op<glz::opts{}>(v, ctx, b, ix); }, *n);
      }

      template <class B>
      void write_u64(uint64_t v, is_context auto& ctx, B& b, auto& ix)
      {
         if (!ensure_space(ctx, b, ix + 32 + write_padding_bytes)) [[unlikely]] {
            return;
         }
         write_chars::op<glz::opts{}>(v, ctx, b, ix);
      }

      // Renders a schema_any (defaultValue/constant) as an *attribute* value.
      template <class B>
      void write_schema_any_attr(const schema::schema_any& v, is_context auto& ctx, B& b, auto& ix)
      {
         std::visit(
            [&](auto&& val) {
               using VT = std::decay_t<decltype(val)>;
               if constexpr (std::same_as<VT, std::monostate>) {
                  // No lexical representation; callers skip emitting the attribute entirely.
               }
               else if constexpr (std::same_as<VT, bool>) {
                  append_raw(val ? "true" : "false", ctx, b, ix);
               }
               else if constexpr (std::same_as<VT, std::string_view>) {
                  xml::escape_attribute(val, ctx, b, ix);
               }
               else {
                  if (!ensure_space(ctx, b, ix + 64 + write_padding_bytes)) [[unlikely]] {
                     return;
                  }
                  write_chars::op<glz::opts{}>(val, ctx, b, ix);
               }
            },
            v);
      }

      // Renders a schema_any (constant, in appinfo) as *text* content.
      template <class B>
      void write_schema_any_text(const schema::schema_any& v, is_context auto& ctx, B& b, auto& ix)
      {
         std::visit(
            [&](auto&& val) {
               using VT = std::decay_t<decltype(val)>;
               if constexpr (std::same_as<VT, std::monostate>) {
                  append_raw("null", ctx, b, ix);
               }
               else if constexpr (std::same_as<VT, bool>) {
                  append_raw(val ? "true" : "false", ctx, b, ix);
               }
               else if constexpr (std::same_as<VT, std::string_view>) {
                  xml::escape_text(val, ctx, b, ix);
               }
               else {
                  if (!ensure_space(ctx, b, ix + 64 + write_padding_bytes)) [[unlikely]] {
                     return;
                  }
                  write_chars::op<glz::opts{}>(val, ctx, b, ix);
               }
            },
            v);
      }

      // default="..." attribute, from defaultValue. Skipped for a monostate
      // (JSON "null") value: XSD's default attribute has no lexical form for it.
      template <class B>
      void write_default_attr(const schema::schema_any& v, is_context auto& ctx, B& b, auto& ix)
      {
         if (std::holds_alternative<std::monostate>(v)) {
            return;
         }
         append_raw(" default=\"", ctx, b, ix);
         write_schema_any_attr(v, ctx, b, ix);
         append_raw("\"", ctx, b, ix);
      }

      // Whether any facet with no XSD equivalent is present (multipleOf,
      // uniqueItems, constant, readOnly/writeOnly, min/maxProperties,
      // min/maxContains, deprecated -- and, for non-array members,
      // min/maxItems, which only maps onto element cardinality for arrays).
      // These have nowhere else to go, so they are folded into <xs:appinfo>
      // rather than silently dropped.
      inline bool has_appinfo_content(const schema& s, bool format_needs_fallback, bool is_array) noexcept
      {
         return format_needs_fallback || bool(s.multipleOf) || s.uniqueItems.has_value() || bool(s.constant) ||
                s.readOnly.has_value() || s.writeOnly.has_value() || bool(s.minProperties) || bool(s.maxProperties) ||
                bool(s.minContains) || bool(s.maxContains) || s.deprecated.has_value() ||
                (!is_array && (bool(s.minItems) || bool(s.maxItems)));
      }

      template <bool IsArray, class B>
      void write_appinfo_body(const schema& s, bool format_needs_fallback, is_context auto& ctx, B& b, auto& ix)
      {
         append_raw("<xs:appinfo>", ctx, b, ix);
         bool wrote_any = false;
         auto sep = [&] {
            if (wrote_any) append_raw("; ", ctx, b, ix);
            wrote_any = true;
         };
         auto kv_text = [&](std::string_view key, std::string_view text) {
            sep();
            append_raw(key, ctx, b, ix);
            append_raw("=", ctx, b, ix);
            xml::escape_text(text, ctx, b, ix);
         };
         auto kv_bool = [&](std::string_view key, bool v) {
            sep();
            append_raw(key, ctx, b, ix);
            append_raw(v ? "=true" : "=false", ctx, b, ix);
         };
         auto kv_u64 = [&](std::string_view key, uint64_t v) {
            sep();
            append_raw(key, ctx, b, ix);
            append_raw("=", ctx, b, ix);
            write_u64(v, ctx, b, ix);
         };
         auto kv_number = [&](std::string_view key, const schema::schema_number& n) {
            sep();
            append_raw(key, ctx, b, ix);
            append_raw("=", ctx, b, ix);
            write_schema_number(n, ctx, b, ix);
         };

         if (format_needs_fallback) {
            kv_text("format", get_enum_name(*s.format));
         }
         if (s.multipleOf) kv_number("multipleOf", s.multipleOf);
         if (s.uniqueItems) kv_bool("uniqueItems", *s.uniqueItems);
         if (s.constant) {
            sep();
            append_raw("constant=", ctx, b, ix);
            write_schema_any_text(*s.constant, ctx, b, ix);
         }
         if (s.readOnly) kv_bool("readOnly", *s.readOnly);
         if (s.writeOnly) kv_bool("writeOnly", *s.writeOnly);
         if (s.minProperties) kv_u64("minProperties", *s.minProperties);
         if (s.maxProperties) kv_u64("maxProperties", *s.maxProperties);
         if (s.minContains) kv_u64("minContains", *s.minContains);
         if (s.maxContains) kv_u64("maxContains", *s.maxContains);
         if (s.deprecated) kv_bool("deprecated", *s.deprecated);
         if constexpr (!IsArray) {
            if (s.minItems) kv_u64("minItems", *s.minItems);
            if (s.maxItems) kv_u64("maxItems", *s.maxItems);
         }

         append_raw("</xs:appinfo>", ctx, b, ix);
      }

      // The base type used for a faceted member's type="..." attribute (no
      // restriction facets) or its <xs:restriction base="...">. Implements
      // the full Task 22 resolution order: an xml_schema<T> xsd_type override
      // (step 1) wins over glz::json_schema<T>'s format substitution (step 2,
      // since a JSON Schema `format` describes the same underlying string
      // more precisely than xs:string alone can), which in turn wins over the
      // ordinary named-type/builtin resolution (step 3).
      template <class Inner, class B>
      void write_facet_base_type(const schema& s, bool format_has_builtin,
                                 std::optional<std::string_view> type_override, is_context auto& ctx, B& b, auto& ix)
      {
         if (type_override) {
            append_raw(*type_override, ctx, b, ix);
         }
         else if (s.format) {
            if (format_has_builtin) {
               append_raw(xsd_format_builtin(*s.format), ctx, b, ix);
            }
            else {
               append_raw("xs:string", ctx, b, ix);
            }
         }
         else {
            write_type_ref<Inner>(ctx, b, ix);
         }
      }

      // Children of <xs:restriction base="...">: every restriction-only
      // facet, plus a pattern approximation when `format` has no XSD
      // built-in (an explicit user `pattern` always wins).
      template <class B>
      void write_restriction_body(const schema& s, std::string_view format_pattern, is_context auto& ctx, B& b,
                                  auto& ix)
      {
         if (s.minimum) {
            append_raw("<xs:minInclusive value=\"", ctx, b, ix);
            write_schema_number(s.minimum, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.maximum) {
            append_raw("<xs:maxInclusive value=\"", ctx, b, ix);
            write_schema_number(s.maximum, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.exclusiveMinimum) {
            append_raw("<xs:minExclusive value=\"", ctx, b, ix);
            write_schema_number(s.exclusiveMinimum, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.exclusiveMaximum) {
            append_raw("<xs:maxExclusive value=\"", ctx, b, ix);
            write_schema_number(s.exclusiveMaximum, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.minLength) {
            append_raw("<xs:minLength value=\"", ctx, b, ix);
            write_u64(*s.minLength, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.maxLength) {
            append_raw("<xs:maxLength value=\"", ctx, b, ix);
            write_u64(*s.maxLength, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.pattern) {
            append_raw("<xs:pattern value=\"", ctx, b, ix);
            xml::escape_attribute(*s.pattern, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         else if (!format_pattern.empty()) {
            append_raw("<xs:pattern value=\"", ctx, b, ix);
            xml::escape_attribute(format_pattern, ctx, b, ix);
            append_raw("\"/>", ctx, b, ix);
         }
         if (s.enumeration) {
            for (const auto& e : *s.enumeration) {
               append_raw("<xs:enumeration value=\"", ctx, b, ix);
               xml::escape_attribute(e, ctx, b, ix);
               append_raw("\"/>", ctx, b, ix);
            }
         }
      }

      // Lazily resolves an array member's element type: range_value_t is
      // concept-constrained and would hard-fail if evaluated against a
      // non-range D, so the NTTP keeps that instantiation from ever
      // happening for the (far more common) non-array case.
      template <class D, bool = writable_array_t<D>>
      struct facet_range_value
      {
         using type = D;
      };

      template <class D>
      struct facet_range_value<D, true>
      {
         using type = std::remove_cvref_t<range_value_t<D>>;
      };

      template <class D>
      using facet_range_value_t = typename facet_range_value<D>::type;

      // Renders a non-variant member that carries a glz::schema entry.
      // Cardinality (minOccurs/maxOccurs) still follows the member's C++
      // shape (array/nullable/plain), same as the unfaceted write_xsd_element,
      // except that minItems/maxItems override an array's default bounds.
      template <class D, class B>
      void write_xsd_faceted_scalar_element(std::string_view name, const schema& s,
                                            std::optional<std::string_view> type_override, is_context auto& ctx, B& b,
                                            auto& ix)
      {
         constexpr bool is_array = writable_array_t<D>;
         constexpr bool is_opt = nullable_like<D>;
         using Inner = std::conditional_t<is_array, facet_range_value_t<D>, schema_unwrap_nullable_t<D>>;

         const bool format_has_builtin = bool(s.format) && !xsd_format_builtin(*s.format).empty();
         const bool format_needs_fallback = bool(s.format) && !format_has_builtin;
         const std::string_view format_pattern =
            format_needs_fallback ? format_pattern_approximation(*s.format) : std::string_view{};
         const bool has_restriction = has_restriction_facets(s) || format_needs_fallback;
         const bool doc_needed = s.title.has_value() || s.description.has_value();
         const bool appinfo_needed = has_appinfo_content(s, format_needs_fallback, is_array);
         const bool needs_body = doc_needed || appinfo_needed || has_restriction;

         append_raw("<xs:element name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\"", ctx, b, ix);

         if constexpr (is_array) {
            append_raw(" minOccurs=\"", ctx, b, ix);
            if (s.minItems) {
               write_u64(*s.minItems, ctx, b, ix);
            }
            else {
               append_raw("0", ctx, b, ix);
            }
            append_raw("\" maxOccurs=\"", ctx, b, ix);
            if (s.maxItems) {
               write_u64(*s.maxItems, ctx, b, ix);
            }
            else {
               append_raw("unbounded", ctx, b, ix);
            }
            append_raw("\"", ctx, b, ix);
         }
         else if constexpr (is_opt) {
            append_raw(" minOccurs=\"0\"", ctx, b, ix);
         }

         if (!has_restriction) {
            append_raw(" type=\"", ctx, b, ix);
            write_facet_base_type<Inner>(s, format_has_builtin, type_override, ctx, b, ix);
            append_raw("\"", ctx, b, ix);

            if constexpr (!is_array) {
               if (s.defaultValue) {
                  write_default_attr(*s.defaultValue, ctx, b, ix);
               }
            }
         }

         if (!needs_body) {
            append_raw("/>", ctx, b, ix);
            return;
         }
         append_raw(">", ctx, b, ix);

         if (doc_needed || appinfo_needed) {
            append_raw("<xs:annotation>", ctx, b, ix);
            if (s.title) {
               append_raw("<xs:documentation>", ctx, b, ix);
               xml::escape_text(*s.title, ctx, b, ix);
               append_raw("</xs:documentation>", ctx, b, ix);
            }
            if (s.description) {
               append_raw("<xs:documentation>", ctx, b, ix);
               xml::escape_text(*s.description, ctx, b, ix);
               append_raw("</xs:documentation>", ctx, b, ix);
            }
            if (appinfo_needed) {
               write_appinfo_body<is_array>(s, format_needs_fallback, ctx, b, ix);
            }
            append_raw("</xs:annotation>", ctx, b, ix);
         }

         if (has_restriction) {
            append_raw("<xs:simpleType><xs:restriction base=\"", ctx, b, ix);
            write_facet_base_type<Inner>(s, format_has_builtin, type_override, ctx, b, ix);
            append_raw("\">", ctx, b, ix);
            write_restriction_body(s, format_pattern, ctx, b, ix);
            append_raw("</xs:restriction></xs:simpleType>", ctx, b, ix);
         }

         append_raw("</xs:element>", ctx, b, ix);
      }

      template <class ValT, class B>
      void write_xsd_attribute(std::string_view name, is_context auto& ctx, B& b, auto& ix,
                               std::optional<std::string_view> type_override = {})
      {
         using V = schema_unwrap_nullable_t<ValT>;
         append_raw("<xs:attribute name=\"", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw("\" type=\"", ctx, b, ix);
         write_type_ref<V>(ctx, b, ix, type_override);
         if constexpr (nullable_like<ValT>) {
            append_raw("\" use=\"optional\"/>", ctx, b, ix);
         }
         else {
            append_raw("\" use=\"required\"/>", ctx, b, ix);
         }
      }

      template <class ValT, class B>
      void write_xsd_element(std::string_view name, is_context auto& ctx, B& b, auto& ix,
                             std::optional<std::string_view> type_override = {})
      {
         using D = std::remove_cvref_t<ValT>;
         if constexpr (is_variant<D>) {
            write_xsd_variant_element<D>(name, ctx, b, ix);
         }
         else if constexpr (writable_map_t<D>) {
            // A map's keys are runtime values, unlike a struct's compile-time
            // member names, so XSD cannot enumerate the children write_xml
            // will actually emit: one child element per entry, named after
            // its key (see xml/write.hpp's writable_map_t writer). Falling
            // through to xsd_type_of<D>() here would describe a document
            // write_xml never produces (a scalar "xs:string") and xmllint
            // would then reject our own writer's output. An inline
            // complexType permitting any element, any number of times, is the
            // honest representation.
            append_raw("<xs:element name=\"", ctx, b, ix);
            append_raw(name, ctx, b, ix);
            append_raw(
               "\"><xs:complexType><xs:sequence>"
               "<xs:any processContents=\"skip\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"
               "</xs:sequence></xs:complexType></xs:element>",
               ctx, b, ix);
         }
         else {
            append_raw("<xs:element name=\"", ctx, b, ix);
            append_raw(name, ctx, b, ix);
            append_raw("\" type=\"", ctx, b, ix);
            if constexpr (writable_array_t<D>) {
               write_type_ref<std::remove_cvref_t<range_value_t<D>>>(ctx, b, ix, type_override);
               append_raw("\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>", ctx, b, ix);
            }
            else if constexpr (nullable_like<D>) {
               write_type_ref<schema_unwrap_nullable_t<D>>(ctx, b, ix, type_override);
               append_raw("\" minOccurs=\"0\"/>", ctx, b, ix);
            }
            else {
               write_type_ref<D>(ctx, b, ix, type_override);
               append_raw("\"/>", ctx, b, ix);
            }
         }
      }

      // Entry point from write_xsd_complex_type_body for members rendered as
      // elements: dispatches to the faceted renderer above for a member with
      // a glz::schema entry, or to the ordinary write_xsd_element otherwise.
      // A faceted variant member falls back to the ordinary <xs:choice>
      // rendering -- no facet in the table has a meaning per-alternative of a
      // choice. The name is always sigil-stripped: a member normally
      // sigil-marked "@attr" but forced to element form by xml_schema<T> (see
      // xsd_effective_is_attribute) must not carry the '@' into its element
      // name.
      template <class T, size_t I, class B>
      void write_xsd_member_element(is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr auto key = reflect<T>::keys[I];
         static constexpr auto name = xml::strip_sigil(key);
         static constexpr auto type_override = xml_type_override<T, I>();
         if constexpr (has_member_schema<T, I>()) {
            using ValT = field_t<T, I>;
            using D = std::remove_cvref_t<ValT>;
            const schema s = get_member_schema<T, I>();
            if constexpr (is_variant<D>) {
               write_xsd_variant_element<D>(name, ctx, b, ix);
            }
            else {
               write_xsd_faceted_scalar_element<D>(name, s, type_override, ctx, b, ix);
            }
         }
         else {
            using ValT = field_t<T, I>;
            write_xsd_element<ValT>(name, ctx, b, ix, type_override);
         }
      }

      // Entry point from write_xsd_complex_type_body for members rendered as
      // attributes -- both ordinary "@name" members and members forced to
      // attribute form by xml_schema<T>::as_attribute. The name is always
      // sigil-stripped (a no-op when there was no '@' to begin with).
      template <class T, size_t I, class B>
      void write_xsd_member_attribute(is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr auto key = reflect<T>::keys[I];
         static constexpr auto name = xml::strip_sigil(key);
         static constexpr auto type_override = xml_type_override<T, I>();
         using ValT = field_t<T, I>;
         write_xsd_attribute<ValT>(name, ctx, b, ix, type_override);
      }

      // Whether T has a "#text" member that coexists with at least one
      // element member (a non-attribute member other than "#text" itself).
      // XSD's <xs:simpleContent> can only ever carry character data plus
      // attributes -- it has no way to also permit child elements -- so a
      // type with both text *and* element members needs a mixed="true"
      // complexType instead (see write_xsd_complex_type_body and its caller
      // emit_named_complex_type, which must add mixed="true" to the opening
      // tag). False whenever T has no "#text" member at all, or its only
      // other members are attributes, in which case the ordinary
      // simpleContent/extension rendering is correct and unaffected.
      template <class T>
      consteval bool text_type_is_mixed()
      {
         constexpr size_t N = reflect<T>::size;
         constexpr size_t text_idx = text_member_index<T>();
         if constexpr (text_idx >= N) {
            return false;
         }
         else {
            bool found = false;
            for_each<N>([&]<size_t I>() {
               if constexpr (I != text_idx) {
                  if constexpr (!xsd_effective_is_attribute<T, I>()) {
                     found = true;
                  }
               }
            });
            return found;
         }
      }

      // Emits the body of a <xs:complexType> for a reflected object T (the
      // part between the opening tag -- named or anonymous -- and its
      // closing tag), partitioning members by key sigil exactly as the XML
      // writer does: attributes ("@name"), a single "#text" member (switches
      // to simpleContent, or to a mixed="true" sequence when text_type_is_mixed
      // -- see above), and everything else (child elements in a sequence) --
      // except that xml_schema<T>::as_attribute (see xsd_effective_is_attribute)
      // can move a member from the element partition into the attribute
      // partition independently of its sigil.
      template <class T, class B>
      void write_xsd_complex_type_body(is_context auto& ctx, B& b, auto& ix)
      {
         static constexpr size_t N = reflect<T>::size;
         static constexpr size_t text_idx = text_member_index<T>();
         static constexpr bool has_text = text_idx < N;

         if constexpr (has_text && text_type_is_mixed<T>()) {
            // Mixed content: "#text" coexists with at least one element
            // member. simpleContent genuinely cannot express this (xmllint:
            // "Element content is not allowed, because the content type is a
            // simple type definition"), so the caller opens this as a
            // mixed="true" complexType instead of a plain named one; a mixed
            // complexType implicitly permits character data anywhere its
            // content model allows an element, so only the element members
            // (not the text itself) need listing here.
            append_raw("<xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               if constexpr (I != text_idx && !xsd_effective_is_attribute<T, I>()) {
                  write_xsd_member_element<T, I>(ctx, b, ix);
               }
            });

            append_raw("</xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               if constexpr (I != text_idx && xsd_effective_is_attribute<T, I>()) {
                  write_xsd_member_attribute<T, I>(ctx, b, ix);
               }
            });
         }
         else if constexpr (has_text) {
            using TextValT = field_t<T, text_idx>;
            append_raw("<xs:simpleContent><xs:extension base=\"", ctx, b, ix);
            append_raw(xsd_type_of<schema_unwrap_nullable_t<TextValT>>(), ctx, b, ix);
            append_raw("\">", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               if constexpr (I != text_idx && xsd_effective_is_attribute<T, I>()) {
                  write_xsd_member_attribute<T, I>(ctx, b, ix);
               }
            });

            append_raw("</xs:extension></xs:simpleContent>", ctx, b, ix);
         }
         else {
            append_raw("<xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (!xml::is_text_key(key) && !xsd_effective_is_attribute<T, I>()) {
                  write_xsd_member_element<T, I>(ctx, b, ix);
               }
            });

            append_raw("</xs:sequence>", ctx, b, ix);

            for_each<N>([&]<size_t I>() {
               static constexpr auto key = reflect<T>::keys[I];
               if constexpr (!xml::is_text_key(key) && xsd_effective_is_attribute<T, I>()) {
                  write_xsd_member_attribute<T, I>(ctx, b, ix);
               }
            });
         }
      }

      // <xs:complexType name="..."> ... </xs:complexType> -- or
      // <xs:complexType name="..." mixed="true"> when T has a "#text" member
      // that coexists with element members (see text_type_is_mixed): the
      // mixed attribute must be on this opening tag, so it is decided here
      // rather than inside write_xsd_complex_type_body, which only fills the
      // body between the tags this function writes.
      template <class T, class B>
      void emit_named_complex_type(xml::xml_context& ctx, B& b, size_t& ix)
      {
         append_raw("<xs:complexType name=\"", ctx, b, ix);
         append_raw(sanitized_name_v<T>, ctx, b, ix);
         if constexpr (text_type_is_mixed<T>()) {
            append_raw("\" mixed=\"true\">", ctx, b, ix);
         }
         else {
            append_raw("\">", ctx, b, ix);
         }
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
      // Task 22: <xs:schema> namespace declaration, driven by
      // glz::xml_schema<T>::target_namespace/namespace_prefix.

      // Whether T's xml_schema<T> (if any) declares a target namespace.
      template <class T>
      concept has_target_namespace = requires { glz::xml_schema<T>::target_namespace; };

      // The namespace prefix to bind the target namespace to: xml_schema<T>'s
      // own namespace_prefix if given, else "tns".
      template <class T>
      consteval std::string_view xsd_namespace_prefix()
      {
         if constexpr (requires { glz::xml_schema<T>::namespace_prefix; }) {
            return glz::xml_schema<T>::namespace_prefix;
         }
         else {
            return "tns";
         }
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
      if constexpr (xml::detail::has_target_namespace<V>) {
         // A target namespace requires stating the form defaults: local
         // elements are namespace-qualified (elementFormDefault="qualified"),
         // local attributes are not (attributeFormDefault="unqualified") --
         // the conventional W3C XSD recommendation.
         constexpr std::string_view tns = xml_schema<V>::target_namespace;
         constexpr std::string_view prefix = xml::detail::xsd_namespace_prefix<V>();
         xml::detail::append_raw("<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"", ctx,
                                 buffer, ix);
         xml::escape_attribute(tns, ctx, buffer, ix);
         xml::detail::append_raw("\" xmlns:", ctx, buffer, ix);
         xml::detail::append_raw(prefix, ctx, buffer, ix);
         xml::detail::append_raw("=\"", ctx, buffer, ix);
         xml::escape_attribute(tns, ctx, buffer, ix);
         xml::detail::append_raw("\" elementFormDefault=\"qualified\" attributeFormDefault=\"unqualified\">\n", ctx,
                                 buffer, ix);
      }
      else {
         xml::detail::append_raw("<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n", ctx, buffer, ix);
      }

      std::vector<xml::detail::named_type_entry<B>> order{};
      xml::detail::collect_named_types<V, B>(order);
      for (const auto& entry : order) {
         entry.emit(ctx, buffer, ix);
         xml::detail::append_raw("\n", ctx, buffer, ix);
      }

      xml::detail::append_raw("  <xs:element name=\"", ctx, buffer, ix);
      xml::detail::append_raw(root_name, ctx, buffer, ix);
      xml::detail::append_raw("\" type=\"", ctx, buffer, ix);
      // Named types (structs/enums) reference their top-level XSD definition by
      // name; anything else (a scalar, or a container/nullable/variant root --
      // none of which get a named definition of their own, see
      // xml::detail::named_xsd_type) must resolve to a built-in XSD type
      // instead. Emitting sanitized_name_v<V> unconditionally here previously
      // produced schemas like `type="int"` for a scalar root, which xmllint
      // rejects because no such type definition exists.
      if constexpr (xml::detail::named_xsd_type<V>) {
         xml::detail::append_raw(xml::detail::sanitized_name_v<V>, ctx, buffer, ix);
      }
      else {
         xml::detail::append_raw(xml::xsd_type_of<V>(), ctx, buffer, ix);
      }
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
