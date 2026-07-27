// Glaze Library
// For the license information refer to glaze.hpp

// Format-neutral schema metadata. Lives in core/ rather than json/ because
// both the JSON Schema generator (glaze/json/schema.hpp) and the XSD generator
// (glaze/xml/schema.hpp) consume glz::schema.
//
// The JSON-Schema structural keywords near the bottom of glz::schema (type,
// ref, properties, items, defs, oneOf, anyOf, required, prefixItems,
// additionalProperties) are written by the JSON generator and ignored by the
// XSD generator. They are kept on the one struct deliberately: splitting them
// into a base class would break designated-initializer syntax, which is the
// documented way users author schema metadata.

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "glaze/forward.hpp"

namespace glz
{
   // Heap-allocated nullable wrapper. Same API as std::optional<T>, but stores the value
   // out-of-line (8 bytes inline instead of sizeof(T) + padding) — used by schema to keep
   // sparsely populated fields cheap.
   //
   // Manages the pointer directly rather than holding a std::unique_ptr<T> so that every
   // TU including schema.hpp avoids the unique_ptr / default_delete template instantiations
   // for each unique boxed T, even when glz::schema itself is never used.
   //
   // Intentionally does not support polymorphic upcast (`boxed<Derived>` → `boxed<Base>`)
   // the way std::unique_ptr does — schema types are concrete and never polymorphic.
   template <class T>
   struct boxed final
   {
      using value_type = T;

      T* ptr{};

      boxed() noexcept = default;

      boxed(const boxed& other) : ptr(other.ptr ? new T(*other.ptr) : nullptr) {}
      boxed& operator=(const boxed& other)
      {
         if (this != &other) {
            T* new_ptr = other.ptr ? new T(*other.ptr) : nullptr;
            delete ptr;
            ptr = new_ptr;
         }
         return *this;
      }

      boxed(boxed&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
      boxed& operator=(boxed&& other) noexcept
      {
         if (this != &other) {
            delete ptr;
            ptr = other.ptr;
            other.ptr = nullptr;
         }
         return *this;
      }

      ~boxed() { delete ptr; }

      // Converting constructor: accepts any type constructible to T
      template <class U>
         requires(!std::same_as<std::decay_t<U>, boxed> && std::is_constructible_v<T, U &&>)
      boxed(U&& val) : ptr(new T(std::forward<U>(val)))
      {}

      // Converting assignment
      template <class U>
         requires(!std::same_as<std::decay_t<U>, boxed> && std::is_constructible_v<T, U &&>)
      boxed& operator=(U&& val)
      {
         T* new_ptr = new T(std::forward<U>(val));
         delete ptr;
         ptr = new_ptr;
         return *this;
      }

      explicit operator bool() const noexcept { return ptr != nullptr; }
      bool has_value() const noexcept { return ptr != nullptr; }

      T& operator*() noexcept { return *ptr; }
      const T& operator*() const noexcept { return *ptr; }
      T* operator->() noexcept { return ptr; }
      const T* operator->() const noexcept { return ptr; }

      T& value() noexcept { return *ptr; }
      const T& value() const noexcept { return *ptr; }

      void reset() noexcept
      {
         delete ptr;
         ptr = nullptr;
      }

      template <class... Args>
      T& emplace(Args&&... args)
      {
         T* new_ptr = new T(std::forward<Args>(args)...);
         delete ptr;
         ptr = new_ptr;
         return *ptr;
      }

      T value_or(T default_val) const { return ptr ? *ptr : std::move(default_val); }
   };

   // Nullable string view: 16 bytes instead of optional<string_view>'s 24 bytes.
   // Null when data == nullptr. Satisfies nullable_t so Glaze skips it with skip_null_members.
   struct sv_opt final
   {
      const char* data{};
      size_t size{};

      constexpr sv_opt() noexcept = default;
      constexpr sv_opt(const char* str) noexcept : data(str), size(str ? std::char_traits<char>::length(str) : 0) {}
      constexpr sv_opt(std::string_view sv) noexcept : data(sv.data()), size(sv.size()) {}
      constexpr sv_opt(const char* str, size_t len) noexcept : data(str), size(len) {}
      constexpr sv_opt& operator=(std::string_view sv) noexcept
      {
         data = sv.data();
         size = sv.size();
         return *this;
      }

      explicit constexpr operator bool() const noexcept { return data != nullptr; }
      constexpr bool has_value() const noexcept { return data != nullptr; }
      constexpr operator std::string_view() const noexcept { return {data, size}; }
      constexpr std::string_view operator*() const noexcept { return {data, size}; }
      constexpr std::string_view value() const noexcept { return {data, size}; }

      constexpr void reset() noexcept
      {
         data = {};
         size = {};
      }
   };

   namespace detail
   {
      enum struct defined_formats : uint32_t;
      struct ExtUnits final
      {
         std::optional<std::string_view> unitAscii{}; // ascii representation of the unit, e.g. "m^2" for square meters
         std::optional<std::string_view>
            unitUnicode{}; // unicode representation of the unit, e.g. "m²" for square meters
         constexpr bool operator==(const ExtUnits&) const noexcept = default;
      };
   }
   struct schema final
   {
      bool reflection_helper{}; // needed to support automatic reflection

      using schema_number = boxed<std::variant<int64_t, uint64_t, double>>;
      using schema_any = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string_view>;

      // meta data keywords, ref: https://www.learnjsonschema.com/2020-12/meta-data/
      sv_opt title{};
      sv_opt description{};
      boxed<schema_any> defaultValue{};
      std::optional<bool> deprecated{};
      boxed<std::vector<std::string_view>> examples{};
      std::optional<bool> readOnly{};
      std::optional<bool> writeOnly{};
      // validation keywords, ref: https://www.learnjsonschema.com/2020-12/validation/
      boxed<schema_any> constant{};
      // string only keywords
      boxed<uint64_t> minLength{};
      boxed<uint64_t> maxLength{};
      sv_opt pattern{};
      // https://www.learnjsonschema.com/2020-12/format-annotation/format/
      std::optional<detail::defined_formats> format{};
      // number only keywords
      schema_number minimum{};
      schema_number maximum{};
      schema_number exclusiveMinimum{};
      schema_number exclusiveMaximum{};
      schema_number multipleOf{};
      // object only keywords
      boxed<uint64_t> minProperties{};
      boxed<uint64_t> maxProperties{};
      // std::optional<std::map<std::string_view, std::vector<std::string_view>>> dependent_required{};
      // array only keywords
      boxed<uint64_t> minItems{};
      boxed<uint64_t> maxItems{};
      boxed<uint64_t> minContains{};
      boxed<uint64_t> maxContains{};
      std::optional<bool> uniqueItems{};
      boxed<std::vector<std::string_view>> enumeration{}; // enum

      // out of json schema specification
      boxed<detail::ExtUnits> ExtUnits{};
      std::optional<bool>
         ExtAdvanced{}; // flag to indicate that the parameter is advanced and can be hidden in default views

      // structural keywords (used internally by schema generation, not typically set by users)
      boxed<std::variant<std::string_view, std::vector<std::string_view>>> type{};
      sv_opt ref{};
      boxed<std::map<std::string_view, schema, std::less<>>> properties{};
      boxed<std::vector<schema>> prefixItems{};
      boxed<std::variant<bool, std::shared_ptr<schema>>> items{};
      boxed<std::variant<bool, std::shared_ptr<schema>>> additionalProperties{};
      boxed<std::map<std::string_view, schema, std::less<>>> defs{};
      boxed<std::vector<schema>> oneOf{};
      boxed<std::vector<schema>> anyOf{};
      boxed<std::vector<std::string_view>> required{};

      static constexpr auto schema_attributes{true}; // allowance flag to indicate metadata within glz::object(...)
   };

   namespace detail
   {

      enum struct defined_formats : uint32_t {
         datetime, //
         date, //
         time, //
         duration, //
         email, //
         idn_email, //
         hostname, //
         idn_hostname, //
         ipv4, //
         ipv6, //
         uri, //
         uri_reference, //
         iri, //
         iri_reference, //
         uuid, //
         uri_template, //
         json_pointer, //
         relative_json_pointer, //
         regex
      };
   }
}

template <>
struct glz::meta<glz::detail::defined_formats>
{
   static constexpr std::string_view name = "defined_formats";
   using enum glz::detail::defined_formats;
   static constexpr std::array keys{
      "date-time", "date",          "time", "duration",     "email",        "idn-email",
      "hostname",  "idn-hostname",  "ipv4", "ipv6",         "uri",          "uri-reference",
      "iri",       "iri-reference", "uuid", "uri-template", "json-pointer", "relative-json-pointer",
      "regex"};
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
   static constexpr std::array value{datetime, //
                                     date, //
                                     time, //
                                     duration, //
                                     email, //
                                     idn_email, //
                                     hostname, //
                                     idn_hostname, //
                                     ipv4, //
                                     ipv6, //
                                     uri, //
                                     uri_reference, //
                                     iri, //
                                     iri_reference, //
                                     uuid, //
                                     uri_template, //
                                     json_pointer, //
                                     relative_json_pointer, //
                                     regex};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
};
