// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/xml/read.hpp"

// This file depends on xml/read.hpp for the scanning primitives
// (parse_start_tag, parse_comment, parse_pi, parse_char_data, parse_end_tag)
// and for xml::skip_element, which already implements the algorithm described
// below (start tag, then a loop over char data / comments / PIs / nested
// elements -- recursing for nested elements -- until the matching end tag,
// with recursion depth bounded by parse_start_tag's own ctx.push_element
// call). read.hpp's object reader needs the same "skip one unknown child"
// capability for its error_on_unknown_keys=false path, so read.hpp cannot
// also include this file (that would be circular); it calls xml::skip_element
// directly instead. glz::skip_value<XML> below is therefore a thin forwarder
// onto that shared implementation, not a second copy of it.

namespace glz
{
   template <>
   struct skip_value<XML>
   {
      template <auto Opts>
      static void op(is_context auto&& ctx, auto&& it, auto end) noexcept
      {
         xml::skip_element(it, end, ctx);
      }
   };
} // namespace glz
