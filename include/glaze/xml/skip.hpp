// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "glaze/core/context.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/xml/common.hpp"

// xml::skip_element (xml/common.hpp) already implements the algorithm
// described below: a start tag, then a loop over char data / comments / PIs
// / nested elements -- recursing for nested elements -- until the matching
// end tag, with recursion depth bounded by parse_start_tag's own
// ctx.push_element call. glz::skip_value<XML> below is a thin forwarder onto
// that shared implementation, matching every sibling format (json/skip.hpp,
// yaml/skip.hpp, toml/skip.hpp), each of which depends only on its own
// common.hpp -- not on read.hpp -- so that skip.hpp stays a lightweight,
// standalone header. xml/read.hpp's object reader includes this file and
// calls skip_value<XML>::op for its own "skip one unknown child" need
// (error_on_unknown_keys off).

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
