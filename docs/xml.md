# XML

Glaze provides an XML 1.0 (5th edition) reader and writer, plus [Namespaces in XML
1.0](https://www.w3.org/TR/xml-names/) support. The same compile-time reflection metadata used for JSON works for
XML, so `glz::meta` specializations you already have are reused as-is.

> **Note:** XML support requires a separate include. It is not included in `glaze/glaze.hpp`.

## Getting Started

Include `glaze/xml.hpp` to access the XML API:

```c++
#include "glaze/xml.hpp"

struct book
{
   std::string title{};
   int year{};
};

template <>
struct glz::meta<book>
{
   using T = book;
   static constexpr auto value = object("title", &T::title, "year", &T::year);
};

int main()
{
   const book b{.title = "Dune", .year = 1965};

   const auto xml = glz::write_xml(b, "book").value_or("<error>");
   // xml == "<?xml version=\"1.0\" encoding=\"UTF-8\"?><book><title>Dune</title><year>1965</year></book>"

   book parsed{};
   const auto ec = glz::read_xml(parsed, xml);
   if (ec) {
      const auto message = glz::format_error(ec, xml);
      // handle the error message
   }
}
```

`glz::write_xml` returns `glz::expected<std::string, glz::error_ctx>` when writing into a fresh `std::string`, or
`glz::error_ctx` when writing into a buffer you supply. `glz::read_xml` returns an `error_ctx`. Both `error_ctx`
objects become truthy when an error occurred; pass one to `glz::format_error` for a human-readable explanation.

`glz::write_file_xml` / `glz::read_file_xml` are the file-oriented equivalents, mirroring every other Glaze format.

## The Mapping Table

One rule set applies identically to reflected structs and `glz::generic`. Member keys partition by sigil:

| XML construct | Key | C++ |
|---|---|---|
| `<a><b>1</b></a>` | `"b"` | child element → member |
| `<a b="1"/>` | `"@b"` | attribute → member |
| `<a>text</a>` | `"#text"` | text content → member |
| `<a><b/><b/></a>` | `"b"` | repeated siblings → `std::vector` |
| `<ns:a>` | `"ns:a"` | prefix retained verbatim in the key |
| `<!-- c -->` | — | skipped on read, never written |
| `<?pi?>` | — | skipped on read, never written |
| `<![CDATA[x]]>` | `"#text"` | merged into text content |

Attribute and text values are strings on the wire but coerce to the member's declared type (`int id` reads
`id="7"`). Comments and processing instructions are skipped when reading and are never emitted when writing.

## Attributes and Text

A key beginning with `@` maps to an attribute; the exact key `"#text"` maps to the element's text content. Anything
else maps to a child element:

```c++
#include "glaze/xml.hpp"

struct xml_user
{
   int id{};
   std::string role{};
   std::string text{};
};

template <>
struct glz::meta<xml_user>
{
   using T = xml_user;
   static constexpr auto value = object("@id", &T::id, "@role", &T::role, "#text", &T::text);
   static constexpr std::string_view root_name = "user";
};

int main()
{
   const xml_user u{.id = 7, .role = "admin", .text = "Alice"};
   const auto xml = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(u).value_or("<error>");
   // xml == "<user id=\"7\" role=\"admin\">Alice</user>"

   xml_user parsed{};
   const auto ec = glz::read_xml(parsed, xml);
   // parsed.id == 7, parsed.role == "admin", parsed.text == "Alice"
}
```

A literal element named `@foo` is not representable, since `@` is not a valid XML `NameStartChar` and so cannot
occur in well-formed XML — this loses nothing real documents can express.

## Repeated Elements

A `std::vector<T>` member collects every sibling element sharing its key into successive elements on write, and
back into the vector on read:

```c++
#include "glaze/xml.hpp"
#include <vector>

struct shelf
{
   std::string name{};
   std::vector<int> tag{};
};

template <>
struct glz::meta<shelf>
{
   using T = shelf;
   static constexpr auto value = object("name", &T::name, "tag", &T::tag);
};

int main()
{
   const shelf s{.name = "a", .tag = {1, 2, 3}};
   const auto xml = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(s, "shelf").value_or("<error>");
   // xml == "<shelf><name>a</name><tag>1</tag><tag>2</tag><tag>3</tag></shelf>"

   shelf parsed{};
   const auto ec = glz::read_xml(parsed, std::string{"<shelf><name>a</name><tag>9</tag></shelf>"});
   // parsed.tag == std::vector<int>{9} -- a SINGLE occurrence still fills the vector
}
```

Reading into a struct is always unambiguous because the member's declared type decides: a `std::vector<T>` member
collects every occurrence — even exactly one — while a scalar member that receives a second occurrence of its key is
an error (`glz::error_code::duplicate_key`) unless `error_on_unknown_keys` is disabled. This is different from the
`glz::generic` path; see [Limitations](#limitations) below.

## Root Element Naming

The root element name resolves in this order:

1. `glz::meta<T>::root_name`, if declared.
2. The runtime `root_name` argument passed to `glz::write_xml`.
3. `"root"`, if neither is given.

```c++
#include "glaze/xml.hpp"

struct plain
{
   int x{};
};

template <>
struct glz::meta<plain>
{
   using T = plain;
   static constexpr auto value = object("x", &T::x);
};

int main()
{
   const plain p{.x = 1};

   // No glz::meta::root_name and no argument -- falls back to "root".
   const auto a = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(p).value_or("<error>");
   // a == "<root><x>1</x></root>"

   // The runtime argument overrides the "root" fallback.
   const auto b = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(p, "custom").value_or("<error>");
   // b == "<custom><x>1</x></custom>"
}
```

`root_name` is deliberately **not** an `xml_opts` member. Options are passed as a `template <auto Opts>` non-type
template parameter, which requires `Opts` to be a structural type, and `std::string_view` is not structural — it
cannot be used this way even if it were added. Reading, by contrast, accepts any root element name; the name on the
wire is never checked against `root_name`.

## Options

`glz::xml::xml_opts` extends the format-agnostic `glz::opts` with XML-specific controls:

| Option | Default | Description |
|---|---|---|
| `error_on_unknown_keys` | `true` | Error on an element/attribute key with no matching member |
| `error_on_missing_keys` | `false` | Error when a required member's key never appears |
| `skip_null_members` | `true` | Omit disengaged `std::optional` members when writing |
| `write_declaration` | `true` | Emit `<?xml version="1.0" encoding="UTF-8"?>` before the root element |
| `prettify` | `false` | Indent nested elements and insert newlines |
| `indentation_width` | `3` | Spaces per indent level when `prettify` is on |
| `validate_names` | `true` | Validate element/attribute names against the XML `Name` production before writing |

```c++
#include "glaze/xml.hpp"

struct book
{
   std::string title{};
   int year{};
};

template <>
struct glz::meta<book>
{
   using T = book;
   static constexpr auto value = object("title", &T::title, "year", &T::year);
};

int main()
{
   const book b{.title = "Dune", .year = 1965};
   const auto xml =
      glz::write_xml<glz::xml::xml_opts{.write_declaration = false, .prettify = true}>(b, "book").value_or("<error>");
   // xml == "<book>\n"
   //        "   <title>Dune</title>\n"
   //        "   <year>1965</year>\n"
   //        "</book>"
}
```

An element carrying both `#text` and child elements (mixed content) is always written compactly, even with
`prettify` on: indenting it would append a newline and indentation directly to the text value, corrupting it on
read-back.

## `glz::generic`

Reading without a target type — for example to inspect an unknown document — uses `glz::generic`, exactly as with
JSON and YAML. Attribute and text values remain strings; there is no type guessing.

```c++
#include "glaze/json/generic.hpp"
#include "glaze/xml.hpp"

int main()
{
   glz::generic g{};
   const std::string xml = R"(<user id="7" role="admin">Alice</user>)";
   const auto ec = glz::read_xml(g, xml);

   const bool ok = !ec && g["@id"].get<std::string>() == "7" && g["@role"].get<std::string>() == "admin" &&
                   g["#text"].get<std::string>() == "Alice";
}
```

## Limitations

### The repeated-element count heuristic

Reading into `glz::generic`, there is no declared type to consult, so occurrence count decides: one occurrence of an
element name yields a scalar value, two or more yield an array.

```c++
#include "glaze/json/generic.hpp"
#include "glaze/xml.hpp"

int main()
{
   glz::generic one{};
   [[maybe_unused]] const auto ec1 = glz::read_xml(one, std::string{"<r><i>1</i></r>"});
   // one["i"].is_array() == false -- a single occurrence stays scalar

   glz::generic many{};
   [[maybe_unused]] const auto ec2 = glz::read_xml(many, std::string{"<r><i>1</i><i>2</i></r>"});
   // many["i"].is_array() == true, many["i"].size() == 2
}
```

This is a limitation of the untyped path only. Reading into a struct is always unambiguous: a `std::vector<T>`
member collects every occurrence, even a single one (see [Repeated Elements](#repeated-elements) above).

### Nested containers are not supported

The repeated-sibling convention takes an element name from the *enclosing struct member's key*. A sequence nested
inside another sequence — or a sequence used as a map's mapped value — has no such key to borrow, because there is
no struct member standing between the two levels. `write_xml` reports `glz::error_code::syntax_error` for these
shapes rather than emitting output:

```c++
#include "glaze/xml.hpp"
#include <vector>

struct holder
{
   std::vector<std::vector<int>> m{};
};

template <>
struct glz::meta<holder>
{
   using T = holder;
   static constexpr auto value = object("m", &T::m);
};

int main()
{
   const holder h{.m = {{1, 2}, {3, 4}}};
   const auto r = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(h, "s");
   const bool rejected = !r.has_value() && r.error() == glz::error_code::syntax_error;
}
```

Writing the inner vectors' item bodies back to back would be well-formed XML, but it silently fuses the items:
`vector<vector<int>>{{1,2},{3,4}}` would become `<m>12</m><m>34</m>`, which is unrecoverable on read-back — there is
no way to tell where one inner sequence ends and the next begins. Wrap the inner sequence in a struct so its items
have a name:

```c++
#include "glaze/xml.hpp"
#include <vector>

struct table_row
{
   std::vector<int> cell{};
};

template <>
struct glz::meta<table_row>
{
   using T = table_row;
   static constexpr auto value = object("cell", &T::cell);
};

struct table
{
   std::vector<table_row> row{};
};

template <>
struct glz::meta<table>
{
   using T = table;
   static constexpr auto value = object("row", &T::row);
};

int main()
{
   const table t{.row = {{.cell = {1, 2}}, {.cell = {3, 4}}}};
   const auto xml = glz::write_xml<glz::xml::xml_opts{.write_declaration = false}>(t, "t").value_or("<error>");
   // xml == "<t><row><cell>1</cell><cell>2</cell></row><row><cell>3</cell><cell>4</cell></row></t>"
}
```

A bare top-level sequence (`glz::write_xml(std::vector<T>{...})`) is rejected for the same underlying reason: it
would repeat its wrapper tag once per item at the document root, producing multiple root elements, which is not
well-formed XML.

## Conformance

Glaze implements XML 1.0 (5th edition) **well-formedness** plus
[Namespaces in XML 1.0](https://www.w3.org/TR/xml-names/). Measured against the W3C XML Conformance Test Suite,
**1009 cases pass with 0 false rejections**. Implemented:

- The full `Name` / `NCName` productions, including the complete `NameStartChar` / `NameChar` Unicode ranges.
- The five predefined entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`) plus `&#nnn;` / `&#xhhh;` character
  references, rejecting surrogates and out-of-range code points.
- CDATA sections, comments (including `--` rejection), and processing instructions (including the reserved `xml`
  target rule).
- The XML declaration (version, encoding, standalone), with position and syntax enforced.
- UTF-8 validation, BOM handling, and line-ending normalization (`\r\n`, `\r`, `\n` → `\n`).
- Attribute-value normalization, duplicate-attribute rejection, and rejection of a literal `<` in an attribute
  value.
- Namespace binding and scoping, undeclared-prefix rejection, and the reserved-prefix rules for `xml` and `xmlns`.

**Not implemented, deliberately:**

1. **DTD validation is not performed.** The internal subset is parsed structurally and skipped, not expanded, so an
   entity declared only in the internal subset is undeclared as far as Glaze is concerned — `&custom;` is an error
   even if a document's DTD defines it. Malformed *declarations* inside the internal subset are also not detected;
   154 W3C `not-wf` cases covering internal-subset grammar errors are accepted rather than rejected. This is a
   deliberate scope boundary, not an oversight.
2. **External entities and external DTD subsets are never fetched.** Parsing never touches the network or the
   filesystem. `SYSTEM` and `PUBLIC` identifiers are recorded structurally and otherwise ignored.
3. **XML 1.1 is not supported.** The declaration's `VersionNum` production accepts any `1.x`, so `<?xml
   version="1.1"?>` parses, but XML 1.1-specific features (extended name characters, additional line-ending forms)
   are not implemented.
4. **UTF-8 only.** A document that declares a non-UTF-8 encoding is rejected outright rather than silently misread.

Reading is intentionally permissive about document shape: any root element name is accepted, and unknown keys can
be tolerated with `error_on_unknown_keys = false`. Writing is intentionally strict about names: a key that is not a
valid XML `Name` is always rejected rather than ever emitting a document Glaze's own reader would reject.
