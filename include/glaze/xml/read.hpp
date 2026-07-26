// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "glaze/core/common.hpp"
#include "glaze/core/context.hpp"
#include "glaze/core/read.hpp"
#include "glaze/core/reflect.hpp"
#include "glaze/file/file_ops.hpp"
#include "glaze/json/generic_fwd.hpp"
#include "glaze/json/read.hpp"
#include "glaze/util/bit_array.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/nullable_traits.hpp"
#include "glaze/util/variant.hpp"
#include "glaze/xml/common.hpp"
#include "glaze/xml/skip.hpp"

namespace glz::xml
{
   // True iff every character in s is XML whitespace. An empty string counts
   // as whitespace-only, so a zero-length run of char data between two tags
   // is discarded the same way "  " would be.
   inline bool is_whitespace_only(std::string_view s) noexcept
   {
      for (const char c : s) {
         if (!is_whitespace(c)) {
            return false;
         }
      }
      return true;
   }

   // Reads a maximal run of NameChar starting right after '<', WITHOUT
   // consuming anything or touching ctx -- a look-ahead the object reader
   // uses to decide which member a child element belongs to before
   // committing to parse_start_tag (which pushes the name onto
   // ctx.element_stack and must only run once dispatch is settled). Returns
   // an empty view if `it` is not at '<'; the caller's subsequent
   // parse_start_tag call is what actually reports a malformed name.
   inline std::string_view peek_element_name(const char* it, const char* end) noexcept
   {
      if (it >= end || *it != '<') {
         return {};
      }
      const char* p = it + 1;
      return detail::read_name_span(p, end);
   }

   // Peeks whether the upcoming element (`it` must be at '<') is "empty" for
   // nullable_like purposes: either self-closing ("<name .../>") or
   // non-self-closing with zero characters of content ("<name></name>").
   // Like peek_element_name above, this is a pure classifier -- it never
   // touches `it` or ctx, so from<XML, nullable_like T> can decide which
   // branch to take before committing to a real, error-reporting parse. A
   // malformed or truncated tag simply reports false (not empty); the real
   // parse that follows in either branch is what actually rejects it.
   inline bool peek_is_empty_element(const char* it, const char* end) noexcept
   {
      if (it >= end || *it != '<') {
         return false;
      }
      const char* p = it + 1;
      const std::string_view name = detail::read_name_span(p, end);
      if (name.empty()) {
         return false;
      }

      while (true) {
         const char* const before_ws = p;
         skip_whitespace(p, end);
         const bool had_ws = p != before_ws;

         if (p >= end) {
            return false;
         }
         if (*p == '>') {
            ++p;
            break;
         }
         if (*p == '/') {
            ++p;
            return p < end && *p == '>'; // self-closing => empty
         }
         if (!had_ws) {
            return false; // malformed; let the real parse report it
         }

         const std::string_view attr_name = detail::read_name_span(p, end);
         if (attr_name.empty()) {
            return false;
         }
         skip_whitespace(p, end);
         if (p >= end || *p != '=') {
            return false;
         }
         ++p;
         skip_whitespace(p, end);
         if (p >= end || (*p != '"' && *p != '\'')) {
            return false;
         }
         const char quote = *p;
         ++p;
         // Attribute values cannot contain a literal quote character (escaped
         // as &quot;/&apos; if needed), so this cannot be fooled by an
         // embedded '>' or '/' -- only finding the matching quote matters.
         while (p < end && *p != quote) {
            ++p;
         }
         if (p >= end) {
            return false;
         }
         ++p; // past the closing quote
      }

      // p is right after the '>' of a non-self-closing start tag.
      return size_t(end - p) >= 2 && p[0] == '<' && p[1] == '/';
   }

   // Consumes CharData interleaved with Comment/PI content until the
   // element's closing tag is reached (left unconsumed, at '<', for the
   // caller to parse via parse_end_tag). A nested element is not valid
   // content for a leaf scalar -- there is no member to bind it to -- and is
   // rejected rather than silently absorbed.
   inline bool parse_leaf_content(const char*& it, const char* end, xml_context& ctx, std::string& out) noexcept
   {
      out.clear();
      while (true) {
         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return false;
         }

         // Route through parse_char_data unconditionally rather than only when
         // 'it' is not at '<': it already recognises '<![CDATA[' as character
         // data and merges it in, and returns immediately, without consuming,
         // when '<' opens something else -- leaving 'it' there for the markup
         // dispatch below. This is what lets a content run that *starts* with
         // CDATA (e.g. "<title><![CDATA[Dune]]></title>") parse correctly,
         // not just one where CDATA follows some leading text.
         std::string chunk;
         if (!parse_char_data(it, end, ctx, chunk)) {
            return false;
         }
         out += chunk;

         if (it == end) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return false;
         }
         if (size_t(end - it) >= 2 && it[1] == '/') {
            return true; // caller parses the end tag
         }
         if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
            if (!parse_comment(it, end, ctx)) {
               return false;
            }
            continue;
         }
         if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
            if (!parse_pi(it, end, ctx)) {
               return false;
            }
            continue;
         }
         ctx.error = error_code::syntax_error; // a scalar leaf cannot contain a child element
         return false;
      }
   }
}

namespace glz::xml::detail
{
   // A reflected object: the type matched by the object from<XML, T>
   // specialization below. Mirrors xml_reflected_object in xml/write.hpp,
   // kept as an independent concept (rather than shared) so that read.hpp
   // does not have to include write.hpp.
   template <class T>
   concept reflected_object = (glaze_object_t<T> || reflectable<T>) && !custom_read<T>;

   // Compile-time index of the "#text" member, or reflect<T>::size if T has
   // none -- mirrors has_text_member<T>() in xml/write.hpp, but returns the
   // index (not just presence) since the object reader needs to reach the
   // member directly when accumulating character data.
   template <class T>
   consteval size_t text_member_index() noexcept
   {
      for (size_t i = 0; i < reflect<T>::size; ++i) {
         if (is_text_key(reflect<T>::keys[i])) {
            return i;
         }
      }
      return reflect<T>::size;
   }

   // A minimal, JSON-format local opts value used only to delegate scalar
   // text into the JSON number/boolean grammar (see assign_scalar_text
   // below). It is deliberately NOT the caller's own Opts (an
   // xml::xml_opts), which has no `null_terminated` (or other JSON-specific)
   // member the JSON reader requires; null_terminated is false because the
   // text handed in is a temporary std::string slice, not the original
   // document buffer.
   inline constexpr glz::opts json_scalar_opts{.format = JSON, .null_terminated = false};

   // Converts already-extracted, already-unescaped text into a scalar value.
   // Strings are assigned directly. Everything else reuses the JSON reader's
   // own scalar grammar: on the wire, an XML attribute value or element text
   // node is unquoted raw text, which happens to be exactly what a JSON
   // number or boolean literal looks like, so `from<JSON, M>` can read it
   // directly -- this is the "JSON scalar-from-string path" the object
   // reader's attribute binding reuses so `int id` accepts `id="7"`.
   template <class M, class Ctx>
   inline void assign_scalar_text(M& value, Ctx& ctx, std::string_view text) noexcept
   {
      if constexpr (string_t<M>) {
         value = text;
      }
      else if constexpr (boolean_like<M>) {
         if (text == "true" || text == "1") {
            value = true;
         }
         else if (text == "false" || text == "0") {
            value = false;
         }
         else {
            ctx.error = error_code::syntax_error;
         }
      }
      else if constexpr (num_t<M>) {
         // XSD 1.0 lexical space for xs:float / xs:double spells the special
         // values "NaN", "INF" and "-INF" (see xml/write.hpp's num_t
         // to<XML,T>::op) -- recognise them before falling back to the JSON
         // number grammar, which has no such literals.
         if constexpr (std::floating_point<M>) {
            if (text == "NaN") {
               value = std::numeric_limits<M>::quiet_NaN();
               return;
            }
            if (text == "INF") {
               value = std::numeric_limits<M>::infinity();
               return;
            }
            if (text == "-INF") {
               value = -std::numeric_limits<M>::infinity();
               return;
            }
         }
         const char* p = text.data();
         const char* e = p + text.size();
         from<JSON, M>::template op<json_scalar_opts>(value, ctx, p, e);
         // With null_terminated=false, the JSON number reader signals
         // `end_reached` (a benign non-error -- see core/read.hpp's
         // finalize_read_context) rather than `none` when the number runs
         // exactly to the end of the buffer handed to it, which it always
         // does here since `text` contains nothing but the number. Normally
         // finalize_read_context clears that signal once the top-level read
         // finishes; delegating into the JSON reader directly like this
         // bypasses that step, so it is cleared explicitly here instead.
         if (ctx.error == error_code::end_reached) {
            ctx.error = error_code::none;
         }
         if (!bool(ctx.error) && p != e) [[unlikely]] {
            ctx.error = error_code::syntax_error; // trailing characters after the number
         }
      }
      else if constexpr (is_named_enum<M>) {
         constexpr auto NN = reflect<M>::size;
         if constexpr (NN == 1) {
            static constexpr auto key = glz::get<0>(reflect<M>::keys);
            if (text == key) {
               value = glz::get<0>(reflect<M>::values);
            }
            else {
               ctx.error = error_code::unexpected_enum;
            }
         }
         else {
            static constexpr auto HashInfo = hash_info<M>;
            const auto index = decode_hash_with_size<XML, M, HashInfo, HashInfo.type>::op(
               text.data(), text.data() + text.size(), text.size());
            if (index >= NN) [[unlikely]] {
               ctx.error = error_code::unexpected_enum;
               return;
            }
            visit<NN>(
               [&]<size_t I>() {
                  static constexpr auto key = glz::get<I>(reflect<M>::keys);
                  if (text == key) [[likely]] {
                     value = glz::get<I>(reflect<M>::values);
                  }
                  else {
                     ctx.error = error_code::unexpected_enum;
                  }
               },
               index);
         }
      }
      else if constexpr (std::is_enum_v<M>) {
         // Raw enum (without glz::meta) -- underlying numeric representation.
         std::underlying_type_t<M> underlying{};
         assign_scalar_text(underlying, ctx, text);
         value = static_cast<M>(underlying);
      }
      else {
         ctx.error = error_code::syntax_error; // unsupported scalar member type
      }
   }

   // Parses one leaf element -- start tag through matching end tag -- and
   // hands the accumulated body text to `convert`. Shared by every scalar
   // from<XML, T> specialization (bool, numbers, strings, enums) so the tag
   // handling lives in exactly one place. A self-closing element yields an
   // empty text.
   template <auto Opts, class Ctx, class Convert>
   inline void read_leaf_element(Ctx& ctx, const char*& it, const char* end, Convert&& convert)
   {
      xml::start_tag tag;
      if (!xml::parse_start_tag(it, end, ctx, tag)) {
         return;
      }

      std::string text;
      if (!tag.self_closing) {
         if (!xml::parse_leaf_content(it, end, ctx, text)) {
            return;
         }
         std::string end_name;
         if (!xml::parse_end_tag(it, end, ctx, end_name)) {
            return;
         }
      }

      convert(text);
   }
}

namespace glz
{
   template <>
   struct parse<XML>
   {
      template <auto Opts, class T, is_context Ctx, class It0, class It1>
      static void op(T&& value, Ctx&& ctx, It0&& it, It1 end)
      {
         size_t bad_offset = 0;
         if (!xml::validate_utf8(std::string_view{it, size_t(end - it)}, bad_offset)) [[unlikely]] {
            ctx.error = error_code::syntax_error;
            return;
         }

         if (!xml::parse_prolog(it, end, ctx)) {
            return;
         }

         if (it == end) [[unlikely]] {
            // An empty or whitespace/comment/PI-only document has no root element.
            ctx.error = error_code::unexpected_end;
            return;
         }

         from<XML, std::remove_cvref_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx), it, end);
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }

         // Trailer ::= Misc* -- only whitespace, comments, and PIs may follow
         // the document (root) element; anything else, including a second
         // root element, is not well-formed.
         while (it != end) {
            if (xml::is_whitespace(*it)) {
               xml::skip_whitespace(it, end);
               continue;
            }
            if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
               if (!xml::parse_comment(it, end, ctx)) {
                  return;
               }
               continue;
            }
            if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
               if (!xml::parse_pi(it, end, ctx)) {
                  return;
               }
               continue;
            }
            ctx.error = error_code::syntax_error; // trailing content after the root element
            return;
         }
      }
   };

   template <boolean_like T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   template <num_t T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   template <string_t T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Enum with glz::meta/glz::enumerate -- reads the enum's key from element text.
   template <class T>
      requires(is_named_enum<T> && !custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Raw enum (without glz::meta) -- reads as underlying numeric type.
   template <class T>
      requires(std::is_enum_v<T> && !glaze_enum_t<T> && !meta_keys<T> && !custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         xml::detail::read_leaf_element<Opts>(
            ctx, it, end, [&](const std::string& text) { xml::detail::assign_scalar_text(value, ctx, text); });
      }
   };

   // Sequences: the repeated-sibling convention (mirrored from write_sequence
   // in xml/write.hpp) means the repetition itself lives in the PARENT --
   // the reflected-object reader above (Task 12) already special-cases a
   // readable_array_t/emplace_backable member and loops there, calling
   // from<XML, item_t> once per occurrence with item_t already unwrapped to
   // the element type. This specialization exists for every OTHER site that
   // reaches a sequence type generically -- the held type of a nullable_like
   // wrapper, a variant alternative, or a map's mapped_type -- where only a
   // single element is ever at `it`. It therefore parses exactly one item
   // and never loops; looping here would consume siblings that only the
   // parent (which owns the loop and the "is this element still the same
   // key" test) is in a position to recognise.
   template <class T>
      requires(readable_array_t<T> && emplace_backable<T> && !custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         auto& item = value.emplace_back();
         using item_t = std::remove_cvref_t<decltype(item)>;
         from<XML, item_t>::template op<Opts>(item, ctx, it, end);
      }
   };

   // Maps: each child element becomes one entry, keyed by its tag name, with
   // the mapped value read via from<XML, mapped_type>. Mirrors the
   // reflected-object reader's own element-dispatch loop (Task 12) over a
   // runtime key instead of a compile-time reflected member -- including the
   // CDATA-safe parse_char_data dispatch discipline documented there.
   template <class T>
      requires(readable_map_t<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         using mapped_t = typename std::remove_cvref_t<T>::mapped_type;

         xml::start_tag tag;
         if (!xml::parse_start_tag(it, end, ctx, tag)) {
            return;
         }

         if (!tag.attributes.empty()) {
            if constexpr (Opts.error_on_unknown_keys) {
               ctx.error = error_code::unknown_key; // a map has no member to bind an attribute to
               return;
            }
         }

         if (tag.self_closing) {
            return;
         }

         while (true) {
            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            // Route through parse_char_data unconditionally -- see the
            // reflected-object reader below for why: it already recognises
            // '<![CDATA[' as character data and merges it in, and returns
            // immediately, without consuming, when '<' opens something else.
            std::string chunk;
            if (!xml::parse_char_data(it, end, ctx, chunk)) {
               return;
            }
            if (!xml::is_whitespace_only(chunk)) {
               ctx.error = error_code::syntax_error; // a map has no member to bind text to
               return;
            }

            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            if (size_t(end - it) >= 2 && it[1] == '/') {
               std::string end_name;
               if (!xml::parse_end_tag(it, end, ctx, end_name)) {
                  return;
               }
               break;
            }
            if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
               if (!xml::parse_comment(it, end, ctx)) {
                  return;
               }
               continue;
            }
            if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
               if (!xml::parse_pi(it, end, ctx)) {
                  return;
               }
               continue;
            }

            const std::string key{xml::peek_element_name(it, end)};
            if (value.find(key) != value.end()) {
               ctx.error = error_code::duplicate_key;
               return;
            }
            from<XML, mapped_t>::template op<Opts>(value[key], ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
         }
      }
   };

   // nullable_like (std::optional, std::unique_ptr, std::shared_ptr, raw
   // pointers, ...): a self-closing element ("<maybe/>"), or a non-
   // self-closing one with no content at all ("<maybe></maybe>"), leaves the
   // value disengaged; anything else constructs the held value and recurses.
   // xml::peek_is_empty_element is a non-mutating look-ahead (mirroring
   // xml::peek_element_name) precisely because parse_start_tag pushes the
   // element name onto ctx.element_stack as a side effect -- calling it
   // speculatively and then again for real would push twice.
   template <nullable_like T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         if (xml::peek_is_empty_element(it, end)) {
            xml::start_tag tag;
            if (!xml::parse_start_tag(it, end, ctx, tag)) {
               return;
            }
            if (!tag.self_closing) {
               std::string end_name;
               if (!xml::parse_end_tag(it, end, ctx, end_name)) {
                  return;
               }
            }
            value = {};
            return;
         }

         if (!nullable_emplace<Opts>(value, ctx)) {
            return;
         }
         using V = std::remove_cvref_t<decltype(*value)>;
         from<XML, V>::template op<Opts>(*value, ctx, it, end);
      }
   };

   // is_variant: try each alternative in declaration order against a saved
   // cursor, restoring both the iterator and any state a failed attempt
   // mutated between tries. Mirrors include/glaze/yaml/read.hpp's variant
   // fallback (its is_variant<T> struct from<YAML, T>::op, around line
   // 6244-6273): a speculative attempt runs against a scratch copy of `it`,
   // and on failure the shared ctx.error is cleared and ctx.element_stack is
   // truncated back to its pre-attempt size -- necessary here (unlike a
   // format with no such stack) because a partially-parsed nested element
   // that never reached its own end tag would otherwise leave stale entries
   // behind for the next alternative. error_code::no_matching_variant_type
   // is set only if every alternative fails.
   template <is_variant T>
      requires(!custom_read<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }

         using V = std::remove_cvref_t<T>;
         static constexpr auto N = std::variant_size_v<V>;

         const auto start = it;
         const auto stack_depth = ctx.element_stack.size();

         auto try_parse = [&]<size_t I>() -> bool {
            using Alt = std::variant_alternative_t<I, V>;
            Alt alt{};
            auto attempt_it = start;
            from<XML, Alt>::template op<Opts>(alt, ctx, attempt_it, end);
            if (!bool(ctx.error)) {
               value = std::move(alt);
               it = attempt_it;
               return true;
            }
            ctx.error = error_code::none;
            ctx.custom_error_message = {};
            ctx.element_stack.resize(stack_depth);
            return false;
         };

         bool parsed = false;
         for_each_short_circuit<N>([&]<size_t I>() { return (parsed = try_parse.template operator()<I>()); });

         if (!parsed) {
            ctx.error = error_code::no_matching_variant_type;
         }
      }
   };

   // glz::generic (untyped) reader: builds an object_t per element using the
   // documented XML<->generic mapping --
   //   * each attribute becomes key "@name" with a string value (no type
   //     guessing: XML has no schema here to tell a number from a numeric
   //     string, so nothing is guessed);
   //   * non-whitespace text becomes "#text", also a string;
   //   * each child element becomes a key holding the child's own value on
   //     its first occurrence, or an array_t accumulating both once a second
   //     occurrence of the same key is seen.
   // An element with no attributes, no children, and only text collapses to
   // a bare string -- its accumulated text, verbatim -- rather than
   // {"#text": "..."}; this is what lets g["title"].get<std::string>() work
   // directly instead of every leaf needing {"#text": ...} unwrapped by hand.
   //
   // Documented limitation: reading into glz::generic cannot distinguish a
   // single occurrence of a child element from a genuine one-element list,
   // because there is no declared type to consult here -- the occurrence
   // COUNT decides (one -> the child's own value, two or more -> array_t).
   // Contrast the typed path above: a std::vector<T> member always collects,
   // even for a single occurrence, because the declared type removes the
   // ambiguity.
   template <num_mode Mode, template <class> class MapType>
      requires(!custom_read<generic_json<Mode, MapType>>)
   struct from<XML, generic_json<Mode, MapType>>
   {
      template <auto Opts, class It, class End>
      static void op(auto& value, is_context auto&& ctx, It&& it, End end)
      {
         using G = std::remove_cvref_t<decltype(value)>;

         xml::start_tag tag;
         if (!xml::parse_start_tag(it, end, ctx, tag)) {
            return;
         }

         const bool any_attr = !tag.attributes.empty();
         for (const auto& attr : tag.attributes) {
            std::string key;
            key.reserve(attr.name.size() + 1);
            key += '@';
            key += attr.name;
            value[key] = attr.value;
         }

         if (tag.self_closing) {
            if (!any_attr) {
               value.data = std::string{};
            }
            return;
         }

         bool any_child = false;
         std::string text;

         while (true) {
            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            // Route through parse_char_data unconditionally -- see the
            // reflected-object reader below for the CDATA-at-content-start
            // hazard this discipline avoids.
            std::string chunk;
            if (!xml::parse_char_data(it, end, ctx, chunk)) {
               return;
            }
            text += chunk;

            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            if (size_t(end - it) >= 2 && it[1] == '/') {
               std::string end_name;
               if (!xml::parse_end_tag(it, end, ctx, end_name)) {
                  return;
               }
               break;
            }
            if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
               if (!xml::parse_comment(it, end, ctx)) {
                  return;
               }
               continue;
            }
            if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
               if (!xml::parse_pi(it, end, ctx)) {
                  return;
               }
               continue;
            }

            const std::string_view child_name = xml::peek_element_name(it, end);
            const bool existed = value.contains(child_name);

            G child{};
            from<XML, G>::template op<Opts>(child, ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            any_child = true;

            if (!existed) {
               value[child_name] = std::move(child);
            }
            else {
               auto& slot = value[child_name];
               if (slot.is_array()) {
                  slot.get_array().push_back(std::move(child));
               }
               else {
                  typename G::array_t arr;
                  arr.push_back(std::move(slot));
                  arr.push_back(std::move(child));
                  slot.data = std::move(arr);
               }
            }
         }

         if (!any_attr && !any_child) {
            value.data = std::move(text);
            return;
         }
         if (!xml::is_whitespace_only(text)) {
            value["#text"] = text;
         }
      }
   };

   // Reflected object reader: parses attributes into "@name" members, text
   // into a "#text" member (if any), and child elements into everything
   // else. This is the substantial piece of Task 12 -- see the design notes
   // in the task brief and docs/superpowers/plans/2026-07-26-xml-support.md
   // (Task 12) for the exact member-binding rules this implements.
   template <class T>
      requires(xml::detail::reflected_object<T>)
   struct from<XML, T>
   {
      template <auto Opts, class It, class End>
      static void op(auto&& value, is_context auto&& ctx, It&& it, End end)
      {
         using V = std::remove_cvref_t<decltype(value)>;
         static constexpr auto N = reflect<V>::size;
         static constexpr auto HashInfo = hash_info<V>;
         static constexpr auto text_index = xml::detail::text_member_index<V>();
         static constexpr bool has_text = text_index < N;

         xml::start_tag tag;
         if (!xml::parse_start_tag(it, end, ctx, tag)) {
            return;
         }

         decltype(auto) t = [&]() -> decltype(auto) {
            if constexpr (reflectable<V>) {
               return to_tie(value);
            }
            else {
               return nullptr;
            }
         }();

         auto member = [&]<size_t I>() -> decltype(auto) {
            if constexpr (reflectable<V>) {
               return get_member(value, get<I>(t));
            }
            else {
               return get_member(value, get<I>(reflect<V>::values));
            }
         };

         bit_array<N> assigned{};

         // Bind attributes: "@" + attr.name looked up against reflect<V>::keys.
         for (const auto& attr : tag.attributes) {
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }

            std::string key;
            key.reserve(attr.name.size() + 1);
            key += '@';
            key += attr.name;

            const auto index = decode_hash_with_size<XML, V, HashInfo, HashInfo.type>::op(
               key.data(), key.data() + key.size(), key.size());
            const bool key_matches = index < N && key == reflect<V>::keys[index];

            if (key_matches) [[likely]] {
               assigned[index] = true;
               visit<N>(
                  [&]<size_t I>() {
                     xml::detail::assign_scalar_text(member.template operator()<I>(), ctx, attr.value);
                  },
                  index);
            }
            else if constexpr (Opts.error_on_unknown_keys) {
               ctx.error = error_code::unknown_key;
               return;
            }
         }
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }

         const auto check_missing_keys = [&]() {
            if constexpr (Opts.error_on_missing_keys) {
               constexpr auto req_fields = required_fields<V, Opts>();
               if ((req_fields & assigned) != req_fields) {
                  for (size_t i = 0; i < N; ++i) {
                     if (!assigned[i] && req_fields[i]) {
                        ctx.custom_error_message = reflect<V>::keys[i];
                        break;
                     }
                  }
                  ctx.error = error_code::missing_key;
               }
            }
         };

         if (tag.self_closing) {
            check_missing_keys();
            return;
         }

         while (true) {
            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            // Route through parse_char_data unconditionally, before testing
            // for '<' at all: it already recognises '<![CDATA[' as character
            // data and merges it in, and returns immediately, without
            // consuming, when '<' opens something else -- leaving 'it' there
            // for the markup dispatch below. This is what lets a content run
            // that *starts* with CDATA parse correctly, not just one where
            // CDATA follows some leading text.
            std::string chunk;
            if (!xml::parse_char_data(it, end, ctx, chunk)) {
               return;
            }
            if constexpr (has_text) {
               assigned[text_index] = true;
               member.template operator()<text_index>() += chunk;
            }
            else if (!xml::is_whitespace_only(chunk)) {
               if constexpr (Opts.error_on_unknown_keys) {
                  ctx.error = error_code::unknown_key;
                  return;
               }
            }

            if (it == end) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            // 'it' is guaranteed to sit at '<' here, and not at a CDATA-open
            // (parse_char_data already consumed those).
            if (size_t(end - it) >= 2 && it[1] == '/') {
               std::string end_name;
               if (!xml::parse_end_tag(it, end, ctx, end_name)) {
                  return;
               }
               break;
            }
            if (size_t(end - it) >= 4 && std::string_view{it, 4} == "<!--") {
               if (!xml::parse_comment(it, end, ctx)) {
                  return;
               }
               continue;
            }
            if (size_t(end - it) >= 2 && std::string_view{it, 2} == "<?") {
               if (!xml::parse_pi(it, end, ctx)) {
                  return;
               }
               continue;
            }

            const std::string_view child_name = xml::peek_element_name(it, end);
            const auto index = decode_hash_with_size<XML, V, HashInfo, HashInfo.type>::op(
               child_name.data(), child_name.data() + child_name.size(), child_name.size());
            const bool key_matches = index < N && child_name == reflect<V>::keys[index];

            if (key_matches) [[likely]] {
               visit<N>(
                  [&]<size_t I>() {
                     using member_t = std::remove_cvref_t<decltype(member.template operator()<I>())>;
                     if constexpr (readable_array_t<member_t> && emplace_backable<member_t>) {
                        // Repeated-sibling convention (mirrors write_sequence in
                        // xml/write.hpp): every occurrence appends one item.
                        // from<XML, item_t> parses exactly one item; it never loops.
                        assigned[I] = true;
                        auto& item = member.template operator()<I>().emplace_back();
                        using item_t = std::remove_cvref_t<decltype(item)>;
                        from<XML, item_t>::template op<Opts>(item, ctx, it, end);
                     }
                     else {
                        if (assigned[I]) {
                           ctx.error = error_code::duplicate_key;
                           return;
                        }
                        assigned[I] = true;
                        from<XML, member_t>::template op<Opts>(member.template operator()<I>(), ctx, it, end);
                     }
                  },
                  index);
               if (bool(ctx.error)) [[unlikely]] {
                  return;
               }
            }
            else if constexpr (Opts.error_on_unknown_keys) {
               ctx.error = error_code::unknown_key;
               return;
            }
            else {
               skip_value<XML>::op<Opts>(ctx, it, end);
               if (bool(ctx.error)) [[unlikely]] {
                  return;
               }
            }
         }

         check_missing_keys();
      }
   };

   // Convenience functions

   template <auto Opts = xml::xml_opts{}, class T, contiguous Buffer>
   [[nodiscard]] error_ctx read_xml(T&& value, Buffer&& buffer) noexcept
   {
      xml::xml_context ctx{};
      return read<set_xml<Opts>()>(std::forward<T>(value), std::forward<Buffer>(buffer), ctx);
   }

   template <auto Opts = xml::xml_opts{}, class T, is_buffer Buffer>
   [[nodiscard]] error_ctx read_file_xml(T&& value, const std::string_view file_path, Buffer&& buffer) noexcept
   {
      xml::xml_context ctx{};
      ctx.current_file = file_path;
      const auto ec = file_to_buffer(buffer, ctx.current_file);

      if (bool(ec)) {
         return {0, ec};
      }

      return read<set_xml<Opts>()>(std::forward<T>(value), buffer, ctx);
   }

   template <auto Opts = xml::xml_opts{}, class T>
   [[nodiscard]] error_ctx read_file_xml(T&& value, const std::string_view file_path) noexcept
   {
      return read_file_xml<Opts>(value, file_path, std::string{});
   }

} // namespace glz
