// XML conformance tests transcribed from the W3C XML Test Suite.
// Source: https://www.w3.org/XML/Test/ (xmlts20130923)
//
// TYPE semantics, per xmlconf.xml:
//   not-wf  -> the parser MUST report an error
//   valid   -> the document is well-formed AND DTD-valid; we assert it parses
//   invalid -> well-formed but DTD-invalid; we do not validate, so it parses
//   error   -> optional behaviour; not asserted here

#include <string>

#include "glaze/json/generic.hpp"
#include "glaze/xml.hpp"
#include "ut/ut.hpp"

using namespace ut;

namespace
{
   // A document that must be rejected.
   bool rejects(std::string_view xml)
   {
      glz::generic g{};
      return bool(glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, std::string{xml}));
   }

   // A document that must be accepted.
   bool accepts(std::string_view xml)
   {
      glz::generic g{};
      return !glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, std::string{xml});
   }
}

// ============================================================================
// xmltest — James Clark's suite (not-wf)
// ============================================================================

suite xmltest_not_wf = [] {
   "not-wf-sa-001: attribute value missing quotes"_test = [] { expect(rejects(R"(<doc a1=v1></doc>)")); };
   "not-wf-sa-002: name starts with a digit"_test = [] { expect(rejects(R"(<doc>< 1doc></doc>)")); };
   "not-wf-sa-003: PI target missing"_test = [] { expect(rejects(R"(<doc><?></doc>)")); };
   "not-wf-sa-004: attribute value not quoted"_test = [] { expect(rejects(R"(<doc a1="v1' ></doc>)")); };
   "not-wf-sa-005: no root element"_test = [] { expect(rejects(R"(<!-- comment -->)")); };
   "not-wf-sa-006: comment not terminated"_test = [] { expect(rejects(R"(<doc><!-- abc </doc>)")); };
   "not-wf-sa-007: reference to undefined entity"_test = [] { expect(rejects(R"(<doc>&entity;</doc>)")); };
   "not-wf-sa-008: name contains an invalid character"_test = [] { expect(rejects(R"(<doc a1;="v1"></doc>)")); };
   "not-wf-sa-009: bare ampersand"_test = [] { expect(rejects(R"(<doc>&#RE;</doc>)")); };
   "not-wf-sa-010: no closing angle bracket"_test = [] { expect(rejects(R"(<doc a1="v1"</doc>)")); };
   "not-wf-sa-012: illegal name"_test = [] { expect(rejects(R"(<doc></>)")); };
   "not-wf-sa-014: CDATA end in content"_test = [] { expect(rejects(R"(<doc>]]></doc>)")); };
   "not-wf-sa-016: PI target 'xml' is reserved"_test = [] { expect(rejects(R"(<doc><?xml version="1.0"?></doc>)")); };
   "not-wf-sa-017: CDATA section not terminated"_test = [] { expect(rejects(R"(<doc><![CDATA[abc]]<doc>)")); };
   "not-wf-sa-023: illegal character in name"_test = [] { expect(rejects(R"(<doc>&.;</doc>)")); };
   "not-wf-sa-024: illegal character reference"_test = [] { expect(rejects(R"(<doc>&#x0;</doc>)")); };
   "not-wf-sa-028: duplicate attribute"_test = [] { expect(rejects(R"(<doc a1="v1" a1="v2"></doc>)")); };
   "not-wf-sa-029: mismatched end tag"_test = [] { expect(rejects(R"(<doc></DOC>)")); };
   "not-wf-sa-032: multiple root elements"_test = [] { expect(rejects(R"(<doc></doc><doc></doc>)")); };
   "not-wf-sa-034: unclosed element"_test = [] { expect(rejects(R"(<doc>)")); };
   "not-wf-sa-036: text after the root element"_test = [] { expect(rejects(R"(<doc></doc>text)")); };
   "not-wf-sa-044: '<' in an attribute value"_test = [] { expect(rejects(R"(<doc a1="<foo"></doc>)")); };
   "not-wf-sa-050: empty document"_test = [] { expect(rejects("")); };
   "not-wf-sa-076: undefined entity in an attribute"_test = [] { expect(rejects(R"(<doc a1="&e;"></doc>)")); };
   "not-wf-sa-085: illegal character in comment"_test = [] { expect(rejects(R"(<doc><!-- a -- b --></doc>)")); };
   "not-wf-sa-093: hex char refs may not use uppercase 'X'"_test = [] {
      // XML 1.0 section 4.1 [66] spells the marker '&#x' in lowercase only.
      expect(rejects(R"(<doc>&#X58;</doc>)"));
      // The lowercase form is well-formed.
      expect(accepts(R"(<doc>&#x58;</doc>)"));
   };
   // FINDING (do not "fix" by inverting): as transcribed, U+0300 (0xCC 0x80) sits in *element
   // content*, not in a Name, so it is legal per the XML Char production and both xmllint and
   // glz::read_xml correctly accept it (glz::xml::is_name_start_char(0x0300) is false, but that
   // rule only applies to Name tokens, cf. tests/xml_test/xml_test.cpp:103,148). The upstream
   // xmltest/not-wf/sa/140.xml case tests a combining character starting a *Name*; this
   // transcription does not reproduce that condition, so the "reject" expectation below is a
   // transcription defect in the task brief, not a parser defect. Left unmodified per
   // instructions: report, do not adjust the expectation.
   "not-wf-sa-140: name starts with a combining character"_test = [] { expect(rejects("<doc>\xCC\x80x</doc>")); };
   "not-wf-not-sa-001: XML declaration must be first"_test = [] {
      expect(rejects("\n<?xml version=\"1.0\"?><doc></doc>"));
   };
};

// ============================================================================
// xmltest — well-formed cases that must be accepted
// ============================================================================

suite xmltest_valid = [] {
   "valid-sa-001: minimal document"_test = [] { expect(accepts(R"(<doc></doc>)")); };
   "valid-sa-002: empty element tag"_test = [] { expect(accepts(R"(<doc/>)")); };
   "valid-sa-003: character reference"_test = [] { expect(accepts(R"(<doc>&#32;</doc>)")); };
   "valid-sa-004: attribute with a character reference"_test = [] { expect(accepts(R"(<doc a1="&#32;"></doc>)")); };
   "valid-sa-006: nested elements"_test = [] { expect(accepts(R"(<doc><a><b></b></a></doc>)")); };
   "valid-sa-012: names may contain a colon"_test = [] { expect(accepts(R"(<doc------></doc------>)")); };
   "valid-sa-020: hexadecimal character reference"_test = [] { expect(accepts(R"(<doc>&#x20;</doc>)")); };
   "valid-sa-023: predefined entities"_test = [] { expect(accepts(R"(<doc>&amp;&lt;&gt;&quot;&apos;</doc>)")); };
   "valid-sa-047: CDATA section"_test = [] { expect(accepts(R"(<doc><![CDATA[<&>]]></doc>)")); };
   "valid-sa-049: comment before the root"_test = [] { expect(accepts(R"(<!-- lead --><doc></doc>)")); };
   "valid-sa-050: PI before the root"_test = [] { expect(accepts(R"(<?pi data?><doc></doc>)")); };
   "valid-sa-068: internal subset is skipped"_test = [] {
      expect(accepts("<!DOCTYPE doc [<!ELEMENT doc (#PCDATA)>]><doc>x</doc>"));
   };
   "valid-sa-093: whitespace around the root"_test = [] { expect(accepts("  <doc></doc>  ")); };
   "valid-decl: full XML declaration"_test = [] {
      expect(accepts(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><doc/>)"));
   };
};

// ============================================================================
// eduni / oasis — namespace conformance
// ============================================================================

suite namespace_conformance = [] {
   "NSDoc-001: simple prefix binding"_test = [] { expect(accepts(R"(<p:doc xmlns:p="urn:p"><p:a/></p:doc>)")); };
   "NSDoc-002: default namespace"_test = [] { expect(accepts(R"(<doc xmlns="urn:d"><a/></doc>)")); };
   "rmt-ns10-004: undeclared prefix on an element"_test = [] { expect(rejects(R"(<p:doc/>)")); };
   "rmt-ns10-005: undeclared prefix on an attribute"_test = [] { expect(rejects(R"(<doc p:a="v"/>)")); };
   "rmt-ns10-011: colon at the start of a name"_test = [] { expect(rejects(R"(<:doc/>)")); };
   "rmt-ns10-013: trailing colon"_test = [] { expect(rejects(R"(<doc:/>)")); };
   "rmt-ns10-016: two colons"_test = [] { expect(rejects(R"(<a:b:c xmlns:a="urn:a"/>)")); };
   "rmt-ns10-035: xmlns may not be bound"_test = [] { expect(rejects(R"(<doc xmlns:xmlns="urn:x"/>)")); };
   "rmt-ns10-036: xml prefix needs no declaration"_test = [] { expect(accepts(R"(<doc xml:lang="en"/>)")); };
   "rmt-ns10-043: xml may not be rebound"_test = [] { expect(rejects(R"(<doc xmlns:xml="urn:wrong"/>)")); };
   "scope: prefix leaves scope with its element"_test = [] {
      expect(rejects(R"(<r><a xmlns:p="urn:p"><p:x/></a><p:y/></r>)"));
   };
};

// ============================================================================
// ibm — character range and encoding conformance
// ============================================================================

suite character_conformance = [] {
   "ibm-not-wf-P02-01: character 0x00 is forbidden"_test = [] {
      expect(rejects(std::string_view{"<doc>\0</doc>", 12}));
   };
   "ibm-not-wf-P02-02: character 0x0B is forbidden"_test = [] { expect(rejects("<doc>\x0B</doc>")); };
   "ibm-not-wf-P02-03: character 0x0C is forbidden"_test = [] { expect(rejects("<doc>\x0C</doc>")); };
   "ibm-not-wf-P66-01: reference to a surrogate"_test = [] { expect(rejects(R"(<doc>&#xD800;</doc>)")); };
   "ibm-not-wf-P66-02: reference beyond U+10FFFF"_test = [] { expect(rejects(R"(<doc>&#x110000;</doc>)")); };
   "noncharacter FFFE is forbidden"_test = [] { expect(rejects(R"(<doc>&#xFFFE;</doc>)")); };
   "noncharacter FFFF is forbidden"_test = [] { expect(rejects(R"(<doc>&#xFFFF;</doc>)")); };
   "malformed UTF-8 is rejected"_test = [] { expect(rejects("<doc>\xFF\xFE</doc>")); };
   "truncated UTF-8 is rejected"_test = [] { expect(rejects("<doc>\xC3</doc>")); };
   "overlong UTF-8 is rejected"_test = [] { expect(rejects("<doc>\xC0\x80</doc>")); };
   "valid multibyte UTF-8 is accepted"_test = [] {
      expect(accepts("<doc>caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80</doc>"));
   };
   "BOM is accepted"_test = [] { expect(accepts("\xEF\xBB\xBF<doc/>")); };
};

// ============================================================================
// libxml2 regression corpus — cases mirrored from libxml2 test/
// ============================================================================

suite libxml2_regressions = [] {
   "libxml2 att1: entity in an attribute"_test = [] { expect(accepts(R"(<doc a="&amp;"/>)")); };
   "libxml2 cdata: nested-looking CDATA"_test = [] { expect(accepts(R"(<doc><![CDATA[<![CDATA[]]></doc>)")); };
   "libxml2 comment in the epilogue"_test = [] { expect(accepts(R"(<doc/><!-- trailing -->)")); };
   "libxml2 pi in the epilogue"_test = [] { expect(accepts(R"(<doc/><?pi data?>)")); };
   "libxml2 dtd with a public identifier"_test = [] {
      expect(accepts(R"(<!DOCTYPE doc PUBLIC "-//X//DTD//EN" "x.dtd"><doc/>)"));
   };
   "libxml2 deeply nested input does not overflow"_test = [] {
      // Guards the recursion limit rather than asserting acceptance.
      std::string deep;
      for (int i = 0; i < 5000; ++i) deep += "<a>";
      for (int i = 0; i < 5000; ++i) deep += "</a>";
      glz::generic g{};
      const auto ec = glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, deep);
      expect(bool(ec)) << "must reject rather than blow the stack";
      expect(ec == glz::error_code::exceeded_max_recursive_depth);
   };
   "libxml2 attribute value normalization"_test = [] {
      glz::generic g{};
      expect(!glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, std::string{"<doc a=\"x\ty\"/>"}));
      expect(g["@a"].get<std::string>() == "x y");
   };
};

int main() {}
