// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cmath>
#include <concepts>
#include <string_view>
#include <utility>
#include <variant>

#include "glaze/core/buffer_traits.hpp"
#include "glaze/core/chrono.hpp"
#include "glaze/core/common.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/core/to.hpp"
#include "glaze/core/write.hpp"
#include "glaze/core/write_chars.hpp"
#include "glaze/json/generic_fwd.hpp"
#include "glaze/util/dump.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/variant.hpp"
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
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         // xs:boolean lexical space.
         xml::detail::append_raw(value ? "true" : "false", ctx, b, ix);
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
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         const std::string_view sv{value};
         if constexpr (xml::check_attribute_pass(Opts)) {
            xml::escape_attribute(sv, ctx, b, ix);
         }
         else {
            xml::escape_text(sv, ctx, b, ix);
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
            xml::detail::append_raw(str, ctx, b, ix);
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

   // std::chrono::system_clock time points and year_month_day: ISO 8601 text
   // content, sharing the digit layout with every other Glaze format via
   // chrono_detail (glaze/core/chrono.hpp). Durations and steady_clock /
   // high_resolution_clock time points need no XML-specific code at all -- they
   // are handled generically for every format (including XML) by the
   // Format-parameterized to<Format, is_duration T> / to<Format, is_count_time_point
   // T> specializations in that same header, since they serialize as a bare numeric
   // count with no calendar semantics to render as text. Only the calendar types
   // below need a format-specific (unquoted, unlike JSON) representation.
   //
   // Neither type satisfies xml_writes_own_start_tag_close, so write_wrapped_element
   // treats them like any other scalar: '>' is written immediately, and this op only
   // ever produces body text.
   template <is_system_time_point T>
      requires(not custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         using Period = typename std::remove_cvref_t<T>::duration::period;
         constexpr size_t max_size = chrono_detail::iso_time_point_max_size<Period> + write_padding_bytes;
         if (!ensure_space(ctx, b, ix + max_size)) [[unlikely]] {
            return;
         }
         chrono_detail::write_iso_time_point<false>(value, ctx, b, ix);
      }
   };

   template <is_year_month_day T>
      requires(not custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         if (!ensure_space(ctx, b, ix + chrono_detail::iso_date_max_size + write_padding_bytes)) [[unlikely]] {
            return;
         }
         chrono_detail::write_iso_date<false>(static_cast<int>(value.year()), static_cast<unsigned>(value.month()),
                                              static_cast<unsigned>(value.day()), ctx, b, ix);
      }
   };
}

namespace glz::xml::detail
{
   // A reflected object: the type matched by the object to<XML, T>
   // specialization below. Kept as its own concept (rather than inlining the
   // clause into xml_writes_own_start_tag_close below) so that specialization's
   // `requires` clause names exactly the set it applies to and stays
   // unambiguous with the is_variant specialization -- xml_writes_own_start_tag_close
   // is a strict superset (it also admits variants), and using it directly as
   // the object specialization's constraint would make the two partial
   // specializations equally-constrained, hence ambiguous, for a variant type.
   template <class T>
   concept xml_reflected_object = (glaze_object_t<T> || reflectable<T>) && !custom_write<T>;

   // Does T's to<XML, T>::op write its own '>' after emitting attributes?
   //
   // MUST stay in lockstep with xml_reflected_object for reflected objects
   // themselves -- if the two ever diverge, the start tag is either never
   // closed (a type matches here but the object specialization below doesn't
   // run, so nothing ever writes '>') or closed twice (the reverse).
   //
   // Variants are included too, but for a different reason: which shape a
   // variant needs (own-close vs. helper-closes) depends on the *active
   // alternative*, which is only known at runtime -- no compile-time branch
   // on the variant's static type can get this right. So a variant always
   // takes responsibility for its own start tag close, and to<XML, is_variant
   // T>::op (below) re-applies this same trait, per alternative, inside the
   // std::visit to decide whether to delegate (object alternative closes
   // itself) or close '>' itself first (scalar/string/enum/container/nested-
   // variant alternative).
   //
   // This must also PROPAGATE THROUGH WRAPPERS. A wrapper's own type (e.g.
   // std::optional<Obj>, std::unique_ptr<Obj>) never satisfies the object
   // clause above, but to<XML, nullable_like T>::op forwards to the held
   // value -- and if that held value is a reflected object (or a variant),
   // IT owns the close. If the property doesn't follow the forwarding,
   // write_wrapped_element writes '>' for the wrapper, and the held object's
   // own writer then writes a second '>' -- well-formed XML with a stray '>'
   // character in the body and no error: silent corruption. This has now
   // bitten std::variant and std::optional; expressing it recursively over
   // nullable_like stops the next wrapper (any nullable_like: optional,
   // unique_ptr, shared_ptr, raw pointer, ...) from repeating it.
   template <class T>
   struct xml_owns_start_tag_close : std::bool_constant<xml_reflected_object<T> || (is_variant<T> && !custom_write<T>)>
   {};

   template <nullable_like T>
      requires(!custom_write<T>)
   struct xml_owns_start_tag_close<T> : xml_owns_start_tag_close<std::remove_cvref_t<decltype(*std::declval<T&>())>>
   {};

   // glz::generic (untyped) values have the same problem as variants, for
   // the same reason: whether the value needs an attribute pass before '>'
   // depends on whether it currently holds an object_t, which is only known
   // at runtime. to<XML, generic_json<Mode, MapType>>::op below always takes
   // responsibility for its own '>', mirroring the is_variant clause above.
   template <num_mode Mode, template <class> class MapType>
      requires(!custom_write<generic_json<Mode, MapType>>)
   struct xml_owns_start_tag_close<generic_json<Mode, MapType>> : std::true_type
   {};

   template <class T>
   concept xml_writes_own_start_tag_close = xml_owns_start_tag_close<std::remove_cvref_t<T>>::value;

   // True when T is a reflected object with a member keyed "#text" (mixed
   // content: text interleaved with child elements). Decided entirely at
   // compile time from the same key sigils the object writer below already
   // partitions members by, so gating prettify on it costs nothing at
   // runtime. reflectable<T> aggregates can never have a "#text" key (a C++
   // member name cannot contain '#'), so this only ever fires for
   // glaze_object_t<T> in practice, but the loop is harmless either way.
   template <class T>
   consteval bool has_text_member() noexcept
   {
      using V = std::remove_cvref_t<T>;
      if constexpr (glaze_object_t<V> || reflectable<V>) {
         for (size_t i = 0; i < reflect<V>::size; ++i) {
            if (is_text_key(reflect<V>::keys[i])) {
               return true;
            }
         }
      }
      return false;
   }

   // Writes `<name ...attrs>body</name>` for a single element and is the ONLY
   // place that emits an element's start/end tags. Both write_xml (for the
   // document root) and the reflected-object writer's element pass (for every
   // child) call this, so a given tag can never be written twice.
   //
   // Attributes must land inside the start tag, before '>', so the two shapes
   // diverge on when '>' is written:
   //  - Types matching xml_writes_own_start_tag_close close their own start
   //    tag: their to<XML, T>::op writes the attribute pass, then '>', then
   //    the body. This function only supplies the outer <name> / </name>
   //    wrapper.
   //  - Everything else (scalars, enums, custom_write types, ...) has no
   //    attribute pass of this shape, so this function writes '>' itself
   //    immediately after the name and the value's to<XML, T>::op only ever
   //    produces body text.
   template <auto Opts, class T>
   void write_wrapped_element(std::string_view name, T&& value, is_context auto& ctx, auto& b, auto& ix)
   {
      using V = std::remove_cvref_t<T>;

      append_raw("<", ctx, b, ix);
      append_raw(name, ctx, b, ix);

      // Prettify child-element bookkeeping. `ctx.wrote_element_child` is a
      // single-frame accumulator: the loop sites below (the object writer's
      // element pass, write_sequence, and the map writer) set it to true right
      // before emitting each child of the value being written here. Save the
      // caller's value and reset it before writing this value's body, so a
      // nested element's own bookkeeping can never clobber the caller's; read it
      // back right after to learn whether THIS body had element children (an
      // indent belongs before the closing tag) or was plain text/scalar content
      // (no whitespace added -- doing so would alter the text).
      [[maybe_unused]] bool saved_wrote_child = false;
      if constexpr (check_prettify(Opts)) {
         saved_wrote_child = ctx.wrote_element_child;
         ctx.wrote_element_child = false;
         ++ctx.indent_depth;
      }

      if constexpr (xml_writes_own_start_tag_close<V>) {
         serialize<XML>::op<Opts>(std::forward<T>(value), ctx, b, ix);
      }
      else {
         append_raw(">", ctx, b, ix);
         serialize<XML>::op<Opts>(std::forward<T>(value), ctx, b, ix);
      }

      [[maybe_unused]] bool had_element_children = false;
      if constexpr (check_prettify(Opts)) {
         --ctx.indent_depth;
         had_element_children = ctx.wrote_element_child;
         ctx.wrote_element_child = saved_wrote_child;
      }

      // Mirror the JSON writer (json/write.hpp:200,314,436): don't close a tag
      // whose body failed to serialize. Emitting </name> anyway leaves a
      // syntactically valid but semantically bogus element in the caller's
      // buffer, indistinguishable from a legitimately empty one.
      if (not bool(ctx.error)) [[likely]] {
         if constexpr (check_prettify(Opts)) {
            if (had_element_children) {
               append_indent<Opts>(ctx, b, ix);
            }
         }
         append_raw("</", ctx, b, ix);
         append_raw(name, ctx, b, ix);
         append_raw(">", ctx, b, ix);
      }
   }

   // Writes a sequence as repeated sibling elements -- <name>item</name> for
   // each item, with no wrapper of its own around the sequence. `name` comes
   // from the enclosing struct member key (or map key), which is a runtime
   // value here even though it is a compile-time constant at the call site in
   // the object writer, so it is threaded as an ordinary std::string_view
   // parameter rather than through Opts (which must stay structural, usable
   // as `template <auto Opts>`). Every item still goes through
   // write_wrapped_element above, so a given tag is still written in exactly
   // one place. An empty sequence writes nothing at all, not an empty element.
   //
   // SuppressPrettify is true when the enclosing struct (the type whose body
   // this sequence's items are siblings within) has a "#text" member -- i.e.
   // the same has_text_member<T> check the object writer's pass 2 loop uses
   // for its own direct child branch. Without it, the per-item indent below
   // would land in that struct's mixed-content body and corrupt its #text
   // value on read-back, exactly like the single-child case.
   template <auto Opts, bool SuppressPrettify, class T>
   void write_sequence(std::string_view name, T&& value, is_context auto& ctx, auto& b, auto& ix)
   {
      for (auto&& item : value) {
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }
         if constexpr (check_prettify(Opts) && !SuppressPrettify) {
            append_indent<Opts>(ctx, b, ix);
            ctx.wrote_element_child = true;
         }
         write_wrapped_element<Opts>(name, item, ctx, b, ix);
      }
   }
}

namespace glz
{
   // Engaged optionals/pointers recurse into the held value. Which member is
   // responsible for the '>' that closes this element's start tag is decided
   // entirely by xml::detail::xml_writes_own_start_tag_close<T>, which
   // (see its definition) propagates through nullable_like wrappers to the
   // held type -- so this op's own T (e.g. optional<Obj>) reports exactly
   // the same answer as Obj itself would. The caller (write_wrapped_element,
   // or a variant's visit for a nullable alternative) already consulted that
   // same trait on this same T before invoking this op:
   //  - trait false (e.g. optional<int>, optional<string>): the caller has
   //    ALREADY written '>' for us, so this op must not write another one --
   //    it only ever produces body text, exactly as before this type existed.
   //  - trait true (held type is a reflected object, or -- recursively -- a
   //    variant/nullable that is itself own-closing): the caller deliberately
   //    did NOT write '>', because it expects THIS op to. When engaged, that
   //    happens by simply delegating to the held value's own writer, which
   //    emits its attribute pass and then its own '>'. When disengaged there
   //    is no held object to delegate to, so this op writes the '>' directly
   //    -- otherwise the start tag would be left permanently unclosed
   //    (`<v` with no '>' at all), not merely empty.
   //
   // Only the object writer (below) can suppress the surrounding <key>/</key>
   // tags entirely (skip_null_members); that decision -- skip the member, or
   // emit <key></key> (or <key attrs></key>...) -- lives there, not here.
   template <nullable_like T>
      requires(not custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         using V = std::remove_cvref_t<decltype(value)>;
         if constexpr (xml::detail::xml_writes_own_start_tag_close<V>) {
            if (value) {
               serialize<XML>::op<Opts>(*value, ctx, b, ix);
            }
            else if (not bool(ctx.error)) [[likely]] {
               xml::detail::append_raw(">", ctx, b, ix);
            }
         }
         else {
            if (value) {
               serialize<XML>::op<Opts>(*value, ctx, b, ix);
            }
         }
      }
   };

   // std::visit onto the active alternative -- mirrors the TOML writer
   // (toml/write.hpp:1461-1475). The active alternative is written exactly as if it
   // had been the member's declared type, so a variant<int, string> member writes
   // either a numeric or text body under the same element name.
   //
   // A variant satisfies xml_writes_own_start_tag_close (see the concept's
   // comment in xml::detail), so write_wrapped_element never writes '>' for a
   // variant -- this op is entirely responsible for it. Which shape is
   // correct depends on the *active* alternative, decided per-visit with the
   // same concept applied to the alternative's own type:
   //  - if the alternative itself owns its start-tag close (a reflected
   //    object, or -- recursively -- a nested variant), delegate to it
   //    unchanged: it emits its attribute pass and then its own '>'.
   //  - otherwise (scalars, strings, enums, chrono types, containers, ...)
   //    this op writes '>' first, exactly as write_wrapped_element's
   //    non-owning branch would have, then lets the alternative write only
   //    body text.
   // Either way, exactly one '>' is written, matching the invariant
   // write_wrapped_element documents for every other type.
   template <is_variant T>
      requires(not custom_write<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         std::visit(
            [&](auto&& alt) {
               using V = std::decay_t<decltype(alt)>;
               if constexpr (xml::detail::xml_writes_own_start_tag_close<V>) {
                  to<XML, V>::template op<Opts>(alt, ctx, b, ix);
               }
               else {
                  // Same guard as write_wrapped_element and the object
                  // writer's attribute pass: don't close a start tag whose
                  // preceding write already failed.
                  if (not bool(ctx.error)) [[likely]] {
                     xml::detail::append_raw(">", ctx, b, ix);
                  }
                  to<XML, V>::template op<Opts>(alt, ctx, b, ix);
               }
            },
            value);
      }
   };

   // Reflected object writer: partitions members by key sigil into attributes
   // ("@name"), text content ("#text"), and child elements (everything else).
   // Attributes must appear inside the start tag, so the member loop runs
   // twice -- once to emit attributes and close the start tag, once to emit
   // the text/element body. This function does NOT write the <name>/</name>
   // wrapper itself; xml::detail::write_wrapped_element owns that, so it can
   // be reused unchanged for both the document root and nested elements.
   template <class T>
      requires(xml::detail::xml_reflected_object<T>)
   struct to<XML, T>
   {
      template <auto Opts, class V, class B>
      static void op(V&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         static constexpr auto N = reflect<T>::size;

         // Mixed content (a "#text" member alongside element members) must be
         // written compactly: any whitespace inserted into this element's body
         // -- before a child's opening tag, or before this element's own
         // closing tag -- would be absorbed into the #text value and silently
         // corrupt it on read-back. Known at compile time from the same key
         // sigils partitioned below.
         static constexpr bool has_text = xml::detail::has_text_member<T>();

         decltype(auto) t = [&]() -> decltype(auto) {
            if constexpr (reflectable<T>) {
               return to_tie(value);
            }
            else {
               return nullptr;
            }
         }();

         auto member = [&]<size_t I>() -> decltype(auto) {
            if constexpr (reflectable<T>) {
               return get_member(value, get<I>(t));
            }
            else {
               return get_member(value, get<I>(reflect<T>::values));
            }
         };

         // Pass 1: attributes, then close the start tag. Both attribute and
         // element keys are validated here -- they share the XML Name
         // production, and keys are compile-time constants, so this is a
         // static_assert rather than a runtime branch. "#text" is exempt: it
         // names content, not an element or attribute.
         for_each<N>([&]<size_t I>() {
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            static constexpr auto key = glz::get<I>(reflect<T>::keys);

            if constexpr (xml::is_attribute_key(key)) {
               if constexpr (xml::check_validate_names(Opts)) {
                  static_assert(xml::validate_name(xml::strip_sigil(key)),
                                "XML attribute name is not a valid XML Name");
               }
               if (skip_member<Opts>(member.template operator()<I>())) {
                  return;
               }
               static constexpr auto attr_name = xml::strip_sigil(key);
               xml::detail::append_raw(" ", ctx, b, ix);
               xml::detail::append_raw(attr_name, ctx, b, ix);
               xml::detail::append_raw("=\"", ctx, b, ix);
               serialize<XML>::op<xml::attribute_pass_on<Opts>()>(member.template operator()<I>(), ctx, b, ix);
               xml::detail::append_raw("\"", ctx, b, ix);
            }
            else if constexpr (xml::is_text_key(key)) {
               // Handled in pass 2.
            }
            else {
               if constexpr (xml::check_validate_names(Opts)) {
                  static_assert(xml::validate_name(key), "XML element key is not a valid XML Name");
               }
            }
         });

         // Same guard as write_wrapped_element's closing tag: if an attribute
         // in pass 1 failed to serialize, don't close the start tag either --
         // an unterminated tag is an honest failure signature, whereas a
         // closed-but-error'd tag can look like a legitimately empty element.
         if (not bool(ctx.error)) [[likely]] {
            xml::detail::append_raw(">", ctx, b, ix);
         }

         // Pass 2: text content, then child elements.
         for_each<N>([&]<size_t I>() {
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            static constexpr auto key = glz::get<I>(reflect<T>::keys);

            if constexpr (xml::is_attribute_key(key)) {
               // Handled in pass 1.
            }
            else if constexpr (xml::is_text_key(key)) {
               serialize<XML>::op<xml::attribute_pass_off<Opts>()>(member.template operator()<I>(), ctx, b, ix);
            }
            else if constexpr (writable_array_t<std::remove_cvref_t<decltype(member.template operator()<I>())>>) {
               // Repeated-sibling convention: a sequence member does not get a
               // wrapper element of its own. The tag repeats once per item, so
               // this bypasses write_wrapped_element for the member as a whole
               // and hands the key down to write_sequence instead, which calls
               // write_wrapped_element once per item.
               xml::detail::write_sequence<Opts, has_text>(key, member.template operator()<I>(), ctx, b, ix);
            }
            else {
               // A disengaged nullable member: the object writer is the only place
               // that can suppress the surrounding <key>/</key> tags entirely, so
               // that decision -- skip when skip_null_members, else fall through
               // and let write_wrapped_element emit <key></key> -- lives here
               // rather than in to<XML, nullable_like>.
               if (skip_member<Opts>(member.template operator()<I>())) {
                  return;
               }
               // has_text: see the comment above N. An element that carries
               // text is written compactly, so no indent precedes this child
               // and it never marks wrote_element_child -- which is exactly
               // what keeps write_wrapped_element from indenting this
               // element's own closing tag below.
               if constexpr (xml::check_prettify(Opts) && !has_text) {
                  xml::detail::append_indent<Opts>(ctx, b, ix);
                  ctx.wrote_element_child = true;
               }
               xml::detail::write_wrapped_element<Opts>(key, member.template operator()<I>(), ctx, b, ix);
            }
         });
      }
   };
}

namespace glz
{
   // A sequence reached with no enclosing member key to name its items: a
   // sequence inside a sequence, or a map's mapped value. XML has no way to
   // name these items, and the repeated-sibling convention this format uses
   // takes the element name from the enclosing struct member key.
   //
   // Writing the item bodies back to back would be well-formed XML but would
   // silently fuse the items: vector<vector<int>>{{1,2},{3,4}} becomes
   // <m>12</m><m>34</m>, and repeated objects fuse into one element with no
   // separator, unrecoverable on read-back. Erroring is the honest behavior
   // until a nested-container convention is specified.
   template <class T>
      requires(writable_array_t<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&&, is_context auto&& ctx, B&&, auto&&)
      {
         ctx.error = error_code::syntax_error;
         ctx.custom_error_message =
            "a nested sequence has no element name; wrap it in a struct member so its items can be named";
      }
   };

   // Maps write each entry as an element named after its key: a std::map with
   // key "alpha" and value 1 becomes <alpha>1</alpha>. Map keys are runtime
   // values (unlike struct member keys, which are compile-time constants), so
   // validation is a runtime check that sets ctx.error rather than a
   // static_assert.
   template <class T>
      requires(writable_map_t<T>)
   struct to<XML, T>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         for (auto&& [key, mapped] : value) {
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            const std::string_view key_sv{key};
            if constexpr (xml::check_validate_names(Opts)) {
               if (!xml::validate_name(key_sv)) [[unlikely]] {
                  ctx.error = error_code::syntax_error;
                  ctx.custom_error_message = "map key is not a valid XML Name";
                  return;
               }
            }
            if constexpr (xml::check_prettify(Opts)) {
               xml::detail::append_indent<Opts>(ctx, b, ix);
               ctx.wrote_element_child = true;
            }
            xml::detail::write_wrapped_element<Opts>(key_sv, mapped, ctx, b, ix);
         }
      }
   };

   // glz::generic (untyped): reverses the mapping from<XML, generic_json<Mode,
   // MapType>> (xml/read.hpp) builds -- "@name" keys become attributes,
   // "#text" becomes body text, an array_t value repeats its key as sibling
   // elements (write_sequence, exactly like a reflected object's sequence
   // member), and everything else gets one wrapped child element. A value
   // that is not an object_t at all (the read side's collapse case: an
   // element with no attributes and no children reads back as a bare string)
   // writes as body text/content directly, with no attribute pass -- which is
   // why this type must own its start tag close (see xml_owns_start_tag_close
   // above): whether an attribute pass happens depends on the runtime
   // variant alternative, not on the static type, the same reason is_variant
   // owns its own close.
   template <num_mode Mode, template <class> class MapType>
      requires(not custom_write<generic_json<Mode, MapType>>)
   struct to<XML, generic_json<Mode, MapType>>
   {
      template <auto Opts, class B>
      static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix)
      {
         using G = std::remove_cvref_t<decltype(value)>;
         using object_t = typename G::object_t;
         using array_t = typename G::array_t;

         if (const auto* obj = value.template get_if<object_t>()) {
            // Pass 1: attributes, then close the start tag -- mirrors the
            // reflected-object writer, but over a runtime map instead of
            // compile-time reflected keys.
            for (auto&& [key, mapped] : *obj) {
               if (bool(ctx.error)) [[unlikely]] {
                  return;
               }
               if (!xml::is_attribute_key(key)) {
                  continue; // handled in pass 2
               }
               const auto attr_name = xml::strip_sigil(std::string_view{key});
               if constexpr (xml::check_validate_names(Opts)) {
                  if (!xml::validate_name(attr_name)) [[unlikely]] {
                     ctx.error = error_code::syntax_error;
                     ctx.custom_error_message = "generic attribute key is not a valid XML Name";
                     return;
                  }
               }
               const std::string* attr_value = mapped.template get_if<std::string>();
               if (!attr_value) [[unlikely]] {
                  ctx.error = error_code::syntax_error;
                  ctx.custom_error_message = "a generic attribute value must be a string";
                  return;
               }
               xml::detail::append_raw(" ", ctx, b, ix);
               xml::detail::append_raw(attr_name, ctx, b, ix);
               xml::detail::append_raw("=\"", ctx, b, ix);
               xml::escape_attribute(*attr_value, ctx, b, ix);
               xml::detail::append_raw("\"", ctx, b, ix);
            }
            if (not bool(ctx.error)) [[likely]] {
               xml::detail::append_raw(">", ctx, b, ix);
            }

            // Pass 2: text, then child elements.
            for (auto&& [key, mapped] : *obj) {
               if (bool(ctx.error)) [[unlikely]] {
                  return;
               }
               if (xml::is_attribute_key(key)) {
                  continue; // pass 1
               }
               if (xml::is_text_key(key)) {
                  const std::string* text = mapped.template get_if<std::string>();
                  if (!text) [[unlikely]] {
                     ctx.error = error_code::syntax_error;
                     ctx.custom_error_message = "a generic #text value must be a string";
                     return;
                  }
                  xml::escape_text(*text, ctx, b, ix);
                  continue;
               }
               const std::string_view key_sv{key};
               if constexpr (xml::check_validate_names(Opts)) {
                  if (!xml::validate_name(key_sv)) [[unlikely]] {
                     ctx.error = error_code::syntax_error;
                     ctx.custom_error_message = "generic element key is not a valid XML Name";
                     return;
                  }
               }
               if (const array_t* arr = mapped.template get_if<array_t>()) {
                  xml::detail::write_sequence<Opts, false>(key_sv, *arr, ctx, b, ix);
               }
               else {
                  xml::detail::write_wrapped_element<Opts>(key_sv, mapped, ctx, b, ix);
               }
            }
            return;
         }

         // Every other alternative: no attributes are possible, so this op
         // closes the start tag itself immediately (see the class comment
         // above for why generic_json must always own its close).
         if (not bool(ctx.error)) [[likely]] {
            xml::detail::append_raw(">", ctx, b, ix);
         }

         if (const auto* s = value.template get_if<std::string>()) {
            xml::escape_text(*s, ctx, b, ix);
            return;
         }
         if (const auto* bv = value.template get_if<bool>()) {
            xml::detail::append_raw(*bv ? "true" : "false", ctx, b, ix);
            return;
         }
         if constexpr (Mode == num_mode::u64) {
            if (const auto* u = value.template get_if<uint64_t>()) {
               to<XML, uint64_t>::template op<Opts>(*u, ctx, b, ix);
               return;
            }
         }
         if constexpr (Mode == num_mode::u64 || Mode == num_mode::i64) {
            if (const auto* i = value.template get_if<int64_t>()) {
               to<XML, int64_t>::template op<Opts>(*i, ctx, b, ix);
               return;
            }
         }
         if (const auto* d = value.template get_if<double>()) {
            to<XML, double>::template op<Opts>(*d, ctx, b, ix);
            return;
         }
         if (value.template holds<array_t>()) {
            // Reached with no enclosing key to name its items -- same
            // situation, and same answer, as the bare writable_array_t
            // rejection above.
            ctx.error = error_code::syntax_error;
            ctx.custom_error_message = "a nested sequence has no element name";
            return;
         }
         // null_t: nothing further to write.
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
      // A bare sequence at the document root would repeat its wrapper tag once
      // per item (the repeated-sibling convention that makes sense for a
      // struct member), producing multiple root elements -- not well-formed
      // XML. Maps are unaffected: writable_array_t excludes writable_map_t.
      if constexpr (writable_array_t<std::remove_cvref_t<T>>) {
         return error_ctx{0, error_code::syntax_error, "a sequence cannot be the document root; wrap it in a struct"};
      }

      const auto name = xml::resolve_root_name<T>(root_name);

      if constexpr (xml::check_validate_names(Opts)) {
         if (!xml::validate_name(name)) [[unlikely]] {
            return error_ctx{0, error_code::syntax_error, "root name is not a valid XML Name"};
         }
      }

      xml::xml_context ctx{};
      size_t ix = 0;

      if constexpr (xml::check_write_declaration(Opts)) {
         xml::detail::append_raw("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", ctx, buffer, ix);
         // The root element is always at indent_depth 0, so a bare '\n' (no spaces)
         // is exactly what append_indent would produce -- write it directly rather
         // than paying for a value read of ctx.indent_depth that is guaranteed 0.
         if constexpr (xml::check_prettify(Opts)) {
            xml::detail::append_raw("\n", ctx, buffer, ix);
         }
      }

      // write_wrapped_element owns tag emission for both scalars and objects
      // (whose attributes must land inside this same start tag), so write_xml
      // never emits <name>/</name> itself -- doing so here as well would
      // double-wrap object roots.
      xml::detail::write_wrapped_element<set_xml<Opts>()>(name, std::forward<T>(value), ctx, buffer, ix);

      if (bool(ctx.error)) [[unlikely]] {
         return error_ctx{ix, ctx.error, ctx.custom_error_message};
      }
      buffer_traits<std::remove_cvref_t<Buffer>>::finalize(buffer, ix);
      return error_ctx{ix, error_code::none, ctx.custom_error_message};
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
