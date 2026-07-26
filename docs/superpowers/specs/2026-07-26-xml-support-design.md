# XML Support for Glaze — Design

Date: 2026-07-26
Status: Approved for planning

## 1. Goal

Add XML as a first-class Glaze format, following the conventions already
established by YAML and TOML: a `read_xml` / `write_xml` pair operating directly
on object memory, with no new public document type. Add XSD schema generation
alongside the existing JSON Schema generation. Cover the implementation with
unit tests drawn from the W3C XML Test Suite and libxml2's test corpus.

## 2. Scope

In scope:

- `read_xml` / `write_xml` / `read_file_xml` / `write_file_xml` over reflected
  structs, standard containers, and `glz::generic`.
- XML 1.0 5th edition **well-formedness** plus Namespaces in XML 1.0.
- `write_xml_schema<T>()` emitting a W3C XML Schema (XSD) document.
- A conformance test suite sourced from W3C xmlconf and libxml2.

Out of scope, deliberately:

- DTD **validation** (element content models, attribute validity). The internal
  subset is parsed structurally and skipped, not expanded.
- External entities and external DTD subsets. Never fetched — a parse must not
  touch the network or filesystem.
- XML 1.1.
- XPath, XSLT, or any query layer.
- A public DOM type. `glz::generic` is the untyped target, as with YAML.

## 3. Architecture

New files, mirroring `include/glaze/yaml/`:

```
include/glaze/xml.hpp            umbrella header
include/glaze/xml/opts.hpp       glz::xml::xml_opts
include/glaze/xml/common.hpp     Name/NCName productions, char classification,
                                 predefined entity table, xml_context
include/glaze/xml/read.hpp       from<XML,T>, parse<XML>, read_xml, read_file_xml
include/glaze/xml/write.hpp      to<XML,T>, serialize<XML>, write_xml, write_file_xml
include/glaze/xml/skip.hpp       skip_value<XML>
include/glaze/xml/schema.hpp     write_xml_schema, glz::xml_schema
```

Modified files:

- `include/glaze/forward.hpp` — add `inline constexpr std::uint32_t XML = 460;`
  (slots between `YAML = 450` and `STENCIL = 500`).
- `include/glaze/core/opts.hpp` — add `set_xml()` beside `set_yaml()`.
- `include/glaze/core/schema.hpp` — **new**; receives `glz::schema` verbatim.
- `include/glaze/json/schema.hpp` — includes `core/schema.hpp`; otherwise
  unchanged.
- `include/glaze/core/context.hpp` — add one `duplicate_key` value to the Key
  errors group of `error_code`. Verified absent today; the nearest existing
  codes (`unknown_key`, `missing_key`, `key_not_found`) do not describe a
  repeated key. The addition is at the end of its group and additive only.

`glaze/xml.hpp` is **opt-in**: it is not added to `glaze/glaze.hpp`. Verified
that `glaze.hpp` likewise omits `yaml.hpp` and `toml.hpp`, so this matches the
established convention and keeps the Unicode name tables out of default builds.

`format_context<XML>` specializes to an `xml_context` carrying the
namespace-prefix scope stack and the open-element stack needed to validate tag
nesting and resolve prefixes.

### Public API

```cpp
glz::read_xml(value, buffer);                    // struct or glz::generic
glz::write_xml(value);                           // expected<std::string, error_ctx>
glz::write_xml(value, buffer);
glz::write_xml(value, buffer, "user");           // explicit root name
glz::read_file_xml(value, path, buffer);
glz::write_file_xml(value, path, buffer);
glz::write_xml_schema<T>();                      // expected<std::string, error_ctx>
glz::write_xml_schema<T>(buffer);
```

## 4. Mapping rules

One rule set applies identically to reflected structs and `glz::generic`.

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

Example:

```cpp
struct user {
   int id{};
   std::string role{};
   std::string text{};
};

template <>
struct glz::meta<user> {
   using T = user;
   static constexpr auto value = object("@id", &T::id, "@role", &T::role, "#text", &T::text);
};

// <user id="7" role="admin">Alice</user>
```

### Type coercion

Attribute and text values are strings on the wire but coerce to the member's
declared type: `int id` reads `id="7"`. Reading into `glz::generic`, they remain
strings — no type guessing.

### Repeated-element ambiguity

The known failure mode of this convention is that a single `<b/>` is
indistinguishable from a one-element list. Resolution is by **type**:

- Reading into a struct, the member's declared type decides. A
  `std::vector<T>` member always collects, even for one occurrence. A scalar
  member receiving a second occurrence is an error
  (`error_code::duplicate_key`, added per §3) unless `error_on_unknown_keys`
  is off.
- Reading into `glz::generic`, there is no type to consult, so the count
  heuristic applies: one occurrence → value, two or more → array. This is
  documented as a limitation of the untyped path.

### Name handling

Element and attribute names are validated against the XML 1.0 5th edition
`Name` production when `validate_names` is on. On write, a key that is not a
valid `Name` is an error (`error_code::syntax_error`) rather than emitting
malformed XML — Glaze must never produce a document it would itself reject.

Keys beginning with `@` map to attributes; the exact key `#text` maps to text
content. A literal element named `@foo` is therefore not representable; this is
accepted, as `@` is not a valid `NameStartChar` and so cannot occur in
well-formed XML.

## 5. Options

```cpp
namespace glz::xml
{
   struct xml_opts
   {
      uint32_t format = XML;
      bool error_on_unknown_keys{true};
      bool error_on_missing_keys{false};
      bool skip_null_members{true};
      bool write_declaration{true};   // <?xml version="1.0" encoding="UTF-8"?>
      bool prettify{false};
      uint8_t indentation_width{3};
      bool validate_names{true};
      uint32_t internal{};
   };
}
```

`root_name` is **not** an option member. Options are passed as
`template <auto Opts>`, which requires a structural type, and
`std::string_view` is not structural — verified by compilation. Root name
resolves in this order:

1. `glz::meta<T>::root_name` if declared.
2. The runtime `root_name` argument to `write_xml`.
3. `"root"`.

Reading accepts any root element name.

## 6. Conformance level

Implemented:

- `Name` and `NCName` productions with the full XML 1.0 5th edition
  `NameStartChar` / `NameChar` Unicode ranges.
- The five predefined entities plus `&#nnn;` and `&#xhhh;` character
  references, rejecting surrogates and out-of-range code points.
- CDATA sections; comments including `--` rejection; processing instructions
  including reserved `xml` target rules.
- XML declaration: version, encoding name, and standalone, with position and
  syntax enforced.
- UTF-8 validation, BOM handling, and the three line-ending normalizations
  (`\r\n`, `\r`, `\n` → `\n`).
- Attribute-value normalization, duplicate-attribute rejection
  (`error_code::duplicate_key`), and rejection of a literal `<` in an attribute
  value.
- Namespace binding, `xmlns` and `xmlns:p` scoping, undeclared-prefix
  rejection, and reserved-prefix rules for `xml` and `xmlns`.
- Internal DTD subset parsed structurally and skipped. A reference to an
  entity that was never declared is an error, not a silent pass.

Not implemented: DTD validation, external entities, XML 1.1. Documents
requiring these are reported as errors or as not-applicable in the test
harness, never silently accepted.

## 7. Schema generation

### 7.1 `glz::schema` relocation

`glz::schema` moves verbatim from `include/glaze/json/schema.hpp` to a new
`include/glaze/core/schema.hpp`. `json/schema.hpp` includes the new header and
is otherwise untouched.

This is a **pure move, not a split**. Splitting into a neutral base plus a
JSON-derived struct would break designated-initializer syntax
(`schema{.description = "..."}`), which does not work through a base class in
C++ and is the documented user-facing form. Every existing
`glz::json_schema<T>` specialization keeps compiling unchanged.

The XSD generator ignores the JSON-structural keywords (`type`, `ref`,
`properties`, `items`, `defs`, `oneOf`, `required`, …), which are already
documented as internal to schema generation.

### 7.2 Annotation sources

- `glz::json_schema<T>` (or in-struct `glaze_json_schema`) supplies **shared**
  annotations and feeds both the JSON Schema and XSD generators. Its name is
  retained; introducing a second neutral holder would reintroduce the dual
  lookup path the shared type is meant to remove.
- `glz::xml_schema<T>` is **optional** and supplies **XML-only** concepts that
  `glz::schema` cannot express: target namespace and prefix, `xs:ID` /
  `xs:IDREF` / `xs:NMTOKEN` type selection, forcing a member to attribute or
  element form independently of its key sigil, and substitution groups.

The two holders are disjoint by construction — `xml_schema` carries only what
`json_schema` cannot — so there is no precedence rule in the common case. Where
both could express the same thing, `xml_schema` wins, and this is documented.

### 7.3 Facet mapping

| `glz::schema` | XSD | Fidelity |
|---|---|---|
| `description`, `title` | `xs:annotation/xs:documentation` | exact |
| `minimum`, `maximum` | `xs:minInclusive`, `xs:maxInclusive` | exact |
| `exclusiveMinimum`, `exclusiveMaximum` | `xs:minExclusive`, `xs:maxExclusive` | exact |
| `minLength`, `maxLength` | `xs:minLength`, `xs:maxLength` | exact |
| `enumeration` | `xs:enumeration` | exact |
| `minItems`, `maxItems` | `minOccurs`, `maxOccurs` | exact |
| `defaultValue` | `default` attribute | exact |
| `format` | `xs:dateTime`, `xs:date`, `xs:time`, `xs:duration`, `xs:anyURI` | partial |
| `pattern` | `xs:pattern` | dialect caveat |
| `multipleOf`, `uniqueItems`, `constant`, `readOnly`, `writeOnly`, `minProperties`, `maxProperties`, `minContains`, `maxContains` | `xs:appinfo` | preserved, not enforced |

Two documented caveats:

- **`format`**: `date-time`, `date`, `time`, `duration`, and `uri` map to XSD
  built-in types. `email`, `hostname`, `ipv4`, `ipv6`, `uuid`, and the rest
  have no XSD built-in and are emitted as `xs:pattern` restrictions.
- **`pattern`**: XSD regular expressions are a different dialect from JSON
  Schema's ECMA-262 — implicitly anchored, with different escape rules. The
  pattern is passed through verbatim with a documented warning. Translating
  between dialects is out of scope.

Facets with no XSD equivalent are written into `xs:appinfo` rather than
silently dropped, so no authored metadata is lost.

### 7.4 C++ type mapping

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
| `#text` member | `xs:simpleContent` / `xs:extension` |
| chrono types | `xs:dateTime`, `xs:duration` |

Nested struct types are emitted as named top-level `xs:complexType`
definitions and referenced, so recursive types terminate.

## 8. Test plan

Four targets, mirroring the YAML test layout.

### `tests/xml_test/`

Hand-written, always built. Covers the type surface Glaze supports: scalars,
`std::string`, `std::optional`, `std::vector` / `array` / `deque` / `list`,
`std::map` / `unordered_map`, nested structs, tuples, `std::variant`, enums,
`glz::generic`, and chrono types. Plus every `xml_opts` flag, error-code
assertions for each failure mode, and the full `@` / `#text` mapping matrix.

### `tests/xml_conformance/xml_conformance.cpp`

Always built, no network. Cases transcribed inline as string literals from the
W3C XML Test Suite, each asserting accept-or-reject with the expected
`error_code`. Grouped by upstream collection (`xmltest`, `sun`, `oasis`,
`ibm`, `eduni`, `japanese`) so a failure names its originating case ID.

### `tests/xml_conformance/xml_conformance_struct.cpp`

The struct-mapping side of conformance: the same documents read into real C++
types, asserting that values land correctly rather than merely that parsing
succeeded.

### `tests/xml_conformance/xml_conformance_data.cpp`

Gated behind the CMake option `glaze_XML_CONFORMANCE_DATA` (OFF by default),
matching `glaze_YAML_CONFORMANCE_DATA`. `FetchContent`s the pinned W3C xmlconf
archive plus libxml2's `test/` corpus and walks every case directory, driven by
the `TYPE` attribute in `xmlconf.xml`:

| `TYPE` | Expectation |
|---|---|
| `not-wf` | must produce an error |
| `valid` | must parse (we do not validate) |
| `invalid` | must parse (we do not validate) |
| `error` | skipped — optional behavior |

Cases requiring external entities or XML 1.1 are skipped by an explicit,
documented exclusion list rather than silently passing. The harness reports the
skip count so exclusions cannot hide regressions.

### `tests/xml_test/xml_schema_test.cpp`

XSD generation tests. Where `xmllint` is available on the build machine, the
generated schemas are validated with `xmllint --schema`, and documents produced
by `write_xml` are validated against their own generated schema — a real
external oracle. Where `xmllint` is absent, these assertions are skipped and
string-comparison tests still run.

## 9. Documentation

- `docs/xml.md` — format guide covering the mapping table, options, and the
  documented limitations.
- `docs/xml-schema.md` — XSD generation, the facet mapping table, and the
  `format` / `pattern` caveats.
- `mkdocs.yml` — nav entries under Formats and Serialization/Parsing.
- `README.md` — one line in the supported-formats list.
- `docs/json-schema.md` — note that `glz::json_schema<T>` now feeds both
  generators.

## 10. Build integration

- `tests/CMakeLists.txt` — `add_subdirectory(xml_test)` and
  `add_subdirectory(xml_conformance)`.
- `tests/xml_test/CMakeLists.txt` and `tests/xml_conformance/CMakeLists.txt`
  modeled on their YAML counterparts, including the MinGW `-Wa,-mbig-obj`
  workaround and `target_code_coverage`.

## 11. Risks

- **Size.** YAML is roughly 9,700 lines of implementation and 22,000 of tests.
  XML at this conformance level is the same order of magnitude. The work will
  land incrementally with tests passing at each stage, not as one drop.
- **Compile time.** The XML headers are opt-in via `glaze/xml.hpp` and are not
  pulled in by `glaze/glaze.hpp`, matching YAML and TOML (verified: neither is
  included there).
- **Unicode tables.** The XML 1.0 5th edition `NameStartChar` / `NameChar`
  ranges are large. They will be generated as sorted `constexpr` range tables
  with binary search, not open-coded conditionals.
- **`glz::schema` move.** Although source-compatible, it touches a widely
  included public header. The existing JSON schema test suite is the guard, and
  must pass unchanged.
