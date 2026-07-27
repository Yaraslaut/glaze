# XML Schema (XSD)

Glaze can generate a [W3C XML Schema](https://www.w3.org/TR/xmlschema-1/) (XSD) document describing the shape
`glz::write_xml` would produce for a reflected type — the XML analog of [JSON Schema](json-schema.md) generation.

> **Note:** XSD generation lives in `glaze/xml.hpp`, the same opt-in header as the rest of XML support.

## Getting Started

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
   static constexpr std::string_view root_name = "book";
};

int main()
{
   auto schema = glz::write_xml_schema<book>().value();
   // schema ==
   //   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
   //   "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n"
   //   "<xs:complexType name=\"book\"><xs:sequence>"
   //   "<xs:element name=\"title\" type=\"xs:string\"/>"
   //   "<xs:element name=\"year\" type=\"xs:int\"/>"
   //   "</xs:sequence></xs:complexType>\n"
   //   "  <xs:element name=\"book\" type=\"book\"/>\n"
   //   "</xs:schema>"
   //
   // There is no prettify option for schema generation: each complexType/simpleType body is emitted as a single
   // unindented line regardless of how deeply the C++ type is nested. This differs from write_xml, whose `prettify`
   // option indents element bodies -- that option has no XSD-generation equivalent.
}
```

`glz::write_xml_schema<T>()` returns `glz::expected<std::string, glz::error_ctx>`; an overload taking a buffer
returns `glz::error_ctx` directly, matching every other Glaze writer.

Every distinct struct and enum type reachable from `T` — including `T` itself — is emitted exactly once as a
top-level named `xs:complexType` or `xs:simpleType` and referenced by name elsewhere, so a self-referential type
(a struct holding a `std::vector` of itself) terminates rather than inlining forever.

The document's single root `xs:element` is named using **only the first two** of `write_xml`'s three root-name
resolution steps: `glz::meta<T>::root_name`, if declared, else the literal fallback `"root"`. `write_xml`'s third
step — a runtime `root_name` argument — has no equivalent here, because `write_xml_schema<T>()` takes no such
argument at all; the name it resolves is necessarily a compile-time property of `T`, since the schema is generated
without a value to write. If you pass a custom runtime root name to `write_xml`, the generated schema's root element
name will not match it unless you also declare `glz::meta<T>::root_name`.

## C++ → XSD Type Mapping

| C++ | XSD |
|---|---|
| `bool` | `xs:boolean` |
| `int8_t` … `int64_t` | `xs:byte`, `xs:short`, `xs:int`, `xs:long` |
| `uint8_t` … `uint64_t` | `xs:unsignedByte` … `xs:unsignedLong` |
| `float`, `double` | `xs:float`, `xs:double` |
| `std::string`, `std::string_view` | `xs:string` |
| enum with `glz::meta` keys | `xs:restriction` + `xs:enumeration` |
| `std::optional<T>` | `minOccurs="0"` / `use="optional"` |
| `std::vector<T>`, sequences | `maxOccurs="unbounded"` |
| `std::variant<...>` | `xs:choice` |
| nested struct | named `xs:complexType` |
| `#text` member, no element members | `xs:simpleContent` / `xs:extension` |
| `#text` member alongside element members | `mixed="true"` on the enclosing `xs:complexType` |
| `std::map<K, V>` | `xs:any` wildcard (runtime keys can't be enumerated in a schema) |
| chrono types | `xs:string` (no chrono-aware XSD type is emitted; see note below) |

`xsd_type_of` has no case for `std::chrono` types — a chrono member falls through to the generic scalar default and
is typed `xs:string`, not `xs:dateTime` or `xs:duration`. There is currently no chrono-aware branch in the type
mapping to produce those.

A member key prefixed with `@` becomes an `xs:attribute` rather than a sequence particle. A `#text` member's effect
on the enclosing type depends on whether the type also has element (non-attribute) members: with none, the type
becomes `xs:simpleContent` / `xs:extension`, since XSD's `simpleContent` cannot carry child elements; with at least
one, the type is instead a `mixed="true"` `xs:complexType`, letting text and elements coexist. Both forms — plus the
`std::map` → `xs:any` mapping above — were verified with `xmllint --schema` against documents `write_xml` actually
produces, so the schema validates `write_xml`'s real output for these shapes:

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
   auto schema = glz::write_xml_schema<xml_user>().value();
   // contains <xs:simpleContent><xs:extension base="xs:string">
   //             <xs:attribute name="id" type="xs:int" use="required"/>
   //             <xs:attribute name="role" type="xs:string" use="required"/>
   //           </xs:extension></xs:simpleContent>
}
```

## Shared Annotations: `glz::json_schema<T>`

Descriptions, numeric bounds, string-length bounds, enumerations, and other value-level facets are authored **once**
via `glz::json_schema<T>` (or an in-struct `glaze_json_schema`) and feed *both* the JSON Schema generator
(`glz::write_json_schema<T>()`) and the XSD generator (`glz::write_xml_schema<T>()`). See
[JSON Schema](json-schema.md) for how to register these annotations; nothing about that registration changes for
XML.

```c++
#include "glaze/json/schema.hpp"
#include "glaze/xml.hpp"

struct reading
{
   int count{};
   std::string code{};
};

template <>
struct glz::meta<reading>
{
   using T = reading;
   static constexpr auto value = object("count", &T::count, "code", &T::code);
   static constexpr std::string_view root_name = "reading";
};

// The SAME specialization feeds both the JSON Schema and the XSD generator.
template <>
struct glz::json_schema<reading>
{
   schema count{.description = "how many", .minimum = 1L, .maximum = 100L};
   schema code{.minLength = 2, .maxLength = 8, .pattern = "[a-z]+"};
};

int main()
{
   auto xsd = glz::write_xml_schema<reading>().value();
   // contains <xs:documentation>how many</xs:documentation>,
   // <xs:minInclusive value="1"/>, <xs:maxInclusive value="100"/>,
   // <xs:minLength value="2"/>, <xs:maxLength value="8"/>, <xs:pattern value="[a-z]+"/>

   auto json = glz::write_json_schema<reading>().value();
   // the same annotations, in JSON Schema form
}
```

## XML-Only Annotations: `glz::xml_schema<T>`

Concepts that describe the *document shape* rather than a value constraint — and so have no JSON Schema
equivalent — live in a separate, optional `glz::xml_schema<T>` specialization. It supplies one `glz::xml_schema_field`
per member it annotates, matched by member name exactly like `glz::json_schema<T>`, plus two optional type-level
constants:

```c++
#include "glaze/xml.hpp"

struct record
{
   std::string key{};
   std::string ref{};
   std::string token{};
};

template <>
struct glz::meta<record>
{
   using T = record;
   static constexpr auto value = object("key", &T::key, "ref", &T::ref, "token", &T::token);
   static constexpr std::string_view root_name = "rec";
};

template <>
struct glz::xml_schema<record>
{
   static constexpr std::string_view target_namespace = "urn:example:rec";
   static constexpr std::string_view namespace_prefix = "r";

   xml_schema_field key{.xsd_type = "xs:ID"};
   xml_schema_field ref{.xsd_type = "xs:IDREF"};
   xml_schema_field token{.xsd_type = "xs:NMTOKEN", .as_attribute = true};
};

int main()
{
   auto xsd = glz::write_xml_schema<record>().value();
   // contains targetNamespace="urn:example:rec" xmlns:r="urn:example:rec"
   // elementFormDefault="qualified" attributeFormDefault="unqualified"
   // <xs:element name="key" type="xs:ID" .../>
   // <xs:element name="ref" type="xs:IDREF" .../>
   // <xs:attribute name="token" type="xs:NMTOKEN" .../>  -- forced to attribute form
}
```

`glz::xml_schema_field` has two members, both optional:

| Field | Effect |
|---|---|
| `xsd_type` | Overrides the generated `type="..."` attribute, e.g. `"xs:ID"`, `"xs:IDREF"`, `"xs:NMTOKEN"` |
| `as_attribute` | Forces the member into attribute form, overriding the `@` sigil |

There is no `as_element`: forcing an attribute-keyed member (`"@id"`) into element form would require a
`glz::xml_schema<T>` member literally named `@id`, which is not a valid C++ identifier and so could never be
written -- the same trap `substitution_group` was removed for. `target_namespace` and
`namespace_prefix` are `static constexpr std::string_view` constants on the `glz::xml_schema<T>` specialization
itself (not per-member); declaring `target_namespace` also switches the generated schema to
`elementFormDefault="qualified"` / `attributeFormDefault="unqualified"`, the conventional W3C recommendation.

`glz::json_schema<T>` and `glz::xml_schema<T>` are disjoint by construction — `xml_schema` only ever supplies what
`json_schema` cannot express — so there is no precedence rule to remember in the common case. Where both could
express the same concept (currently, only a member's base XSD type), `xml_schema`'s `xsd_type` wins.

**Substitution groups are deliberately not offered.** Real XSD substitution groups apply only to *global* element
declarations, but this generator always emits local, nested particles inside a `xs:sequence` or `xs:choice`. Adding
a `substitution_group` field to `xml_schema_field` could therefore only ever be a no-op — there is no local particle
form for it to apply to — so it is intentionally left out rather than shipped as a silently-ignored option.

## Facet Mapping

| `glz::schema` | XSD | Fidelity |
|---|---|---|
| `description`, `title` | `xs:annotation/xs:documentation` | exact |
| `minimum`, `maximum` | `xs:minInclusive`, `xs:maxInclusive` | exact |
| `exclusiveMinimum`, `exclusiveMaximum` | `xs:minExclusive`, `xs:maxExclusive` | exact |
| `minLength`, `maxLength` | `xs:minLength`, `xs:maxLength` | exact |
| `enumeration` | `xs:enumeration` | exact |
| `minItems`, `maxItems` | `minOccurs`, `maxOccurs` | exact |
| `defaultValue` | `default` attribute | conditional — see below |
| `format` | `xs:dateTime`, `xs:date`, `xs:time`, `xs:duration`, `xs:anyURI` | partial |
| `pattern` | `xs:pattern` | dialect caveat |
| `multipleOf`, `uniqueItems`, `constant`, `readOnly`, `writeOnly`, `minProperties`, `maxProperties`, `minContains`, `maxContains` | `xs:appinfo` | preserved, not enforced |

Three mappings above carry caveats worth stating plainly:

- **`defaultValue` is dropped whenever the member also has a restriction facet, or is an array.** XSD's `default`
  attribute lives on the `xs:element` tag itself, but once any restriction facet (`minimum`, `maximum`, `minLength`,
  `enumeration`, `pattern`, an XSD-lacking `format`, and so on) is present, the generator instead emits a nested
  anonymous `xs:simpleType`/`xs:restriction`, which has no attribute position left for `default` to attach to — so it
  is simply not written. The same omission applies to array members (`minOccurs`/`maxOccurs` are written instead of
  `type`, and again there is no attribute slot for `default`). A `defaultValue` on a plain, unrestricted, non-array
  member is written faithfully; on anything else, register the facet but do not expect the generated schema to carry
  the default.
- **`pattern` is passed through verbatim.** XSD's regular-expression dialect (`xs:pattern`) is **not** the same
  dialect as JSON Schema's ECMA-262 regex: XSD patterns are implicitly anchored (no `^`/`$` needed, and it is not a
  substring search), and several escape sequences differ between the two. A pattern authored for JSON Schema is not
  guaranteed to mean the same thing once copied into `xs:pattern` — Glaze does not translate between the dialects,
  it emits your `pattern` string unchanged.
- **`format` values without an XSD built-in become `xs:pattern` approximations.** `date-time`, `date`, `time`,
  `duration`, and `uri` map onto real XSD built-in types (`xs:dateTime`, `xs:date`, `xs:time`, `xs:duration`,
  `xs:anyURI`). Formats such as `email`, `hostname`, `ipv4`, `ipv6`, and `uuid` have no XSD built-in type at all, so
  they fall back to `xs:string` restricted by a best-effort `xs:pattern` approximation — good enough to catch gross
  errors, not a faithful reimplementation of the format's real grammar.

## Limitations

**Facets with no XSD equivalent are preserved into `xs:appinfo`, not enforced.** `multipleOf`, `uniqueItems`,
`constant`, `readOnly` / `writeOnly`, `minProperties` / `maxProperties`, and `minContains` / `maxContains` have no
XSD facet to map onto. Rather than silently dropping this authored metadata, Glaze writes it into an
`xs:annotation/xs:appinfo` block as `key=value` text, where it survives for tooling that knows to look for it but is
not validated by a standard XSD processor:

```c++
#include "glaze/json/schema.hpp"
#include "glaze/xml.hpp"

struct ratio_holder
{
   double ratio{};
};

template <>
struct glz::meta<ratio_holder>
{
   using T = ratio_holder;
   static constexpr auto value = object("ratio", &T::ratio);
   static constexpr std::string_view root_name = "r";
};

template <>
struct glz::json_schema<ratio_holder>
{
   schema ratio{.exclusiveMinimum = 0.0, .exclusiveMaximum = 1.0, .multipleOf = 0.25};
};

int main()
{
   auto xsd = glz::write_xml_schema<ratio_holder>().value();
   // <xs:minExclusive value="0"/> and <xs:maxExclusive value="1"/> are real XSD facets;
   // multipleOf has none, so it is preserved as <xs:appinfo>multipleOf=0.25</xs:appinfo>
}
```

**`xs:choice` distinguishes variant alternatives by type, not by element name.** `write_xml` serializes every
alternative of a `std::variant` member under the *same* element name, distinguished only by the shape of the
content that name wraps. XSD's `xs:choice` has no construct for "same element name, disambiguated by content type"
— a schema validator must be able to tell alternatives apart by name alone. The generator therefore gives each
choice alternative its own element name, derived from the alternative's type:

```c++
#include "glaze/xml.hpp"
#include <variant>

struct point
{
   int x{};
   int y{};
};

template <>
struct glz::meta<point>
{
   using T = point;
   static constexpr auto value = object("x", &T::x, "y", &T::y);
};

struct variant_holder
{
   std::variant<int, point> v{};
};

template <>
struct glz::meta<variant_holder>
{
   using T = variant_holder;
   static constexpr auto value = object("v", &T::v);
   static constexpr std::string_view root_name = "h";
};

int main()
{
   auto xsd = glz::write_xml_schema<variant_holder>().value();
   // <xs:element name="v"><xs:complexType><xs:choice>
   //   <xs:element name="int" type="xs:int"/>
   //   <xs:element name="point" type="point"/>
   // </xs:choice></xs:complexType></xs:element>
}
```

The generated schema therefore does not validate documents actually produced by `write_xml` for a variant member
byte-for-byte against the element name `write_xml` chose (`v` for every alternative) — it validates a shape that
distinguishes alternatives the way XSD is able to, not the way `write_xml` itself writes them. The alternative's
element name is derived from a sanitized form of its type name; for a type without a declared `glz::meta` name,
that name comes from the compiler's own type-name spelling, which is legible for a plain struct (`point`) but can be
an unreadable mangled-looking string for standard-library types such as `std::string` — annotate variant
alternatives with `glz::meta` names when they will appear in a generated schema.
