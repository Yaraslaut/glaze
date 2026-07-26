// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cmath>
#include <concepts>

#include "glaze/core/buffer_traits.hpp"
#include "glaze/core/chrono.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/core/to.hpp"
#include "glaze/core/wrappers.hpp"
#include "glaze/core/write.hpp"
#include "glaze/core/write_chars.hpp"
#include "glaze/core/write_wrappers.hpp"
#include "glaze/util/dump.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/itoa.hpp"
#include "glaze/xml/common.hpp"

namespace glz
{
   template <>
   struct serialize<XML>
   {
      template <auto Opts, class T, is_context Ctx, class B, class IX>
      static void op(T&& value, Ctx&& ctx, B&& b, IX&& ix)
      {
         to<XML, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx),
                                                            std::forward<B>(b), std::forward<IX>(ix));
      }
   };

   template <boolean_like T>
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&&, B&& b, auto&& ix)
      {
         // xs:boolean lexical space.
         xml::detail::append_raw(value ? "true" : "false", b, ix);
      }
   };

   template <num_t T>
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         if (!ensure_space(ctx, b, ix + 32 + write_padding_bytes)) [[unlikely]] {
            return;
         }
         // XSD 1.0 lexical space for xs:float / xs:double spells the special
         // values "NaN", "INF" and "-INF". Do NOT delegate to to<JSON, T> here:
         // JSON has no NaN/Inf, so it emits "null" -- which in XML is just the
         // literal text "null", collapses three distinct values into one, and
         // makes a document fail the xs:double schema generated in Phase 5.
         // Use the shared write_chars, as every sibling format does.
         if constexpr (std::floating_point<std::remove_cvref_t<T>>) {
            if (std::isnan(value)) {
               dump("NaN", b, ix);
               return;
            }
            if (std::isinf(value)) {
               dump(value < 0 ? "-INF" : "INF", b, ix);
               return;
            }
         }
         write_chars::op<Opts>(value, ctx, b, ix);
      }
   };

   template <str_t T>
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&&, B&& b, auto&& ix)
      {
         const std::string_view sv{value};
         if constexpr (xml::check_attribute_pass(Opts)) {
            xml::escape_attribute(sv, b, ix);
         }
         else {
            xml::escape_text(sv, b, ix);
         }
      }
   };

   // Enum with glz::meta/glz::enumerate - writes the enum's key as element text.
   template <class T>
      requires((glaze_enum_t<T> || (meta_keys<T> && std::is_enum_v<std::decay_t<T>>)) && not custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         const sv str = get_enum_name(value);
         if (!str.empty()) {
            xml::detail::append_raw(str, b, ix);
         }
         else [[unlikely]] {
            // Value doesn't have a mapped name, serialize as underlying number.
            serialize<XML>::op<Opts>(static_cast<std::underlying_type_t<T>>(value), ctx, b, ix);
         }
      }
   };

   // Raw enum (without glz::meta) - writes as underlying numeric type.
   template <class T>
      requires(!meta_keys<T> && std::is_enum_v<std::decay_t<T>> && !glaze_enum_t<T> && !custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class... Args>
      static void op(auto&& value, is_context auto&& ctx, Args&&... args)
      {
         serialize<XML>::op<Opts>(static_cast<std::underlying_type_t<std::decay_t<T>>>(value), ctx,
                                  std::forward<Args>(args)...);
      }
   };
}

namespace glz
{
   namespace xml
   {
      // Root element name resolution order:
      //   1. glz::meta<T>::root_name if declared
      //   2. the runtime root_name argument
      //   3. "root"
      template <class T>
      constexpr std::string_view resolve_root_name(std::string_view fallback) noexcept
      {
         if constexpr (requires { meta<std::remove_cvref_t<T>>::root_name; }) {
            return meta<std::remove_cvref_t<T>>::root_name;
         }
         else {
            return fallback;
         }
      }
   }

   template <auto Opts = xml::xml_opts{}, class T, output_buffer Buffer>
   [[nodiscard]] error_ctx write_xml(T&& value, Buffer& buffer, std::string_view root_name = "root") noexcept
   {
      const auto name = xml::resolve_root_name<T>(root_name);

      if constexpr (xml::check_validate_names(Opts)) {
         if (!xml::validate_name(name)) [[unlikely]] {
            return error_ctx{0, error_code::syntax_error, "root name is not a valid XML Name"};
         }
      }

      xml::xml_context ctx{};
      size_t ix = 0;

      if constexpr (xml::check_write_declaration(Opts)) {
         xml::detail::append_raw("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", buffer, ix);
      }

      xml::detail::append_raw("<", buffer, ix);
      xml::detail::append_raw(name, buffer, ix);
      xml::detail::append_raw(">", buffer, ix);

      serialize<XML>::op<set_xml<Opts>()>(std::forward<T>(value), ctx, buffer, ix);

      xml::detail::append_raw("</", buffer, ix);
      xml::detail::append_raw(name, buffer, ix);
      xml::detail::append_raw(">", buffer, ix);

      buffer.resize(ix);
      return error_ctx{ix, ctx.error};
   }

   template <auto Opts = xml::xml_opts{}, class T>
   [[nodiscard]] expected<std::string, error_ctx> write_xml(T&& value, std::string_view root_name = "root") noexcept
   {
      std::string buffer{};
      const auto ec = write_xml<Opts>(std::forward<T>(value), buffer, root_name);
      if (bool(ec)) {
         return unexpected(ec);
      }
      return buffer;
   }

   template <auto Opts = xml::xml_opts{}, class T>
   [[nodiscard]] error_ctx write_file_xml(T&& value, const sv file_path, auto&& buffer,
                                          std::string_view root_name = "root") noexcept
   {
      const auto ec = write_xml<Opts>(std::forward<T>(value), buffer, root_name);
      if (bool(ec)) [[unlikely]] {
         return ec;
      }
      return {0, buffer_to_file(buffer, file_path)};
   }
}
