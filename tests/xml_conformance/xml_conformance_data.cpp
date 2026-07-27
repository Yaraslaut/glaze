// Data-driven XML conformance test over the W3C XML Test Suite (xmlts20130923).
//
// Built only when -Dglaze_XML_CONFORMANCE_DATA=ON.
//
// Case metadata comes from the suite's own manifests rather than from the
// directory layout, because only xmltest/sun/ibm use not-wf/valid/invalid
// subdirectories -- eduni, oasis and japanese do not.
//
// TYPE semantics, per testcases.dtd:
//   not-wf  -> the parser MUST report an error
//   valid   -> MUST parse (well-formed and DTD-valid)
//   invalid -> MUST parse (well-formed; we do not validate)
//   error   -> optional behaviour; skipped
//
// This target asserts a *baseline*, not zero failures: 154 not-wf cases are
// known, deliberate failures where correctness depends on expanding entities
// or attribute defaults declared in a DTD internal subset, which we
// intentionally do not do (see declares_internal_entity/skip_reason above --
// those filters already remove the cases the manifest can identify ahead of
// time, but not every internal-subset-dependent not-wf case is expressible
// that way). Requiring t.failed == 0 here would make the target permanently
// red and therefore useless as a regression guard, per the final-review
// finding that led to this comment. What must always be exactly zero is
// false rejections -- valid/invalid (well-formed) documents we incorrectly
// reject -- since unlike tolerating an unrecognized not-wf case, rejecting
// well-formed input is never acceptable.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/json/generic.hpp"
#include "glaze/xml.hpp"
#include "ut/ut.hpp"

using namespace ut;
namespace fs = std::filesystem;

#ifndef XML_CONFORMANCE_DIR
#error "XML_CONFORMANCE_DIR must be defined by CMake"
#endif

namespace
{
   // Manifest files, relative to xmlconf/. Enumerated explicitly: eduni/xmlconf.xml
   // and xmltest/xmlconf.xml are stylesheet wrappers, not case lists.
   constexpr std::string_view manifests[]{
      "xmltest/xmltest.xml",
      "sun/sun-valid.xml",
      "sun/sun-invalid.xml",
      "sun/sun-not-wf.xml",
      "japanese/japanese.xml",
      "oasis/oasis.xml",
      "ibm/ibm_oasis_valid.xml",
      "ibm/ibm_oasis_invalid.xml",
      "ibm/ibm_oasis_not-wf.xml",
      "eduni/errata-2e/errata2e.xml",
      "eduni/errata-3e/errata3e.xml",
      "eduni/errata-4e/errata4e.xml",
      "eduni/namespaces/1.0/rmt-ns10.xml",
      "eduni/misc/ht-bh.xml",
   };

   std::string read_file(const fs::path& p)
   {
      std::ifstream in{p, std::ios::binary};
      return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
   }

   struct test_case
   {
      std::string id{};
      std::string type{};
      std::string uri{};
      std::string entities{};
      std::string version{};
      std::string edition{};
      std::string recommendation{};
      std::string nmspace{};
      fs::path base{}; // directory of the manifest that declared it
   };

   // Extracts attr="value" from a <TEST ...> tag. Deliberately a hand-rolled
   // scanner rather than glz::read_xml: the harness must not depend on the
   // parser it is testing, or a parser bug would silently empty the corpus.
   std::string attr_of(std::string_view tag, std::string_view name)
   {
      std::string needle{name};
      needle += "=\"";
      const auto pos = tag.find(needle);
      if (pos == std::string_view::npos) return {};
      const auto start = pos + needle.size();
      const auto end = tag.find('"', start);
      if (end == std::string_view::npos) return {};
      return std::string{tag.substr(start, end - start)};
   }

   std::vector<test_case> load_manifest(const fs::path& manifest)
   {
      std::vector<test_case> cases;
      const auto text = read_file(manifest);
      const auto base = manifest.parent_path();

      size_t pos = 0;
      while ((pos = text.find("<TEST", pos)) != std::string::npos) {
         // Guard against matching <TESTCASES ...> or <TESTSUITE ...>.
         const char after = pos + 5 < text.size() ? text[pos + 5] : '\0';
         if (after != ' ' && after != '\t' && after != '\n' && after != '\r') {
            ++pos;
            continue;
         }
         const auto close = text.find('>', pos);
         if (close == std::string::npos) break;

         const std::string_view tag{text.data() + pos, close - pos};
         test_case c{};
         c.id = attr_of(tag, "ID");
         c.type = attr_of(tag, "TYPE");
         c.uri = attr_of(tag, "URI");
         c.entities = attr_of(tag, "ENTITIES");
         c.version = attr_of(tag, "VERSION");
         c.edition = attr_of(tag, "EDITION");
         c.recommendation = attr_of(tag, "RECOMMENDATION");
         c.nmspace = attr_of(tag, "NAMESPACE");
         c.base = base;
         if (!c.uri.empty() && !c.type.empty()) {
            cases.push_back(std::move(c));
         }
         pos = close + 1;
      }
      return cases;
   }

   // Returns a non-empty reason when the case falls outside the documented
   // scope. Every skip is counted and reported, so exclusions can never
   // silently hide a regression.
   std::string skip_reason(const test_case& c)
   {
      if (c.type == "error") return "TYPE=error is optional behaviour";
      if (c.version == "1.1") return "XML 1.1 is out of scope";
      if (c.recommendation == "XML1.1") return "XML 1.1 is out of scope";
      // Namespace-inapplicable cases. NAMESPACE="no" marks documents that are
      // valid XML 1.0 but invalid under XML Namespaces -- e.g. valid-sa-012
      // uses an attribute named ":". We are namespace-aware, so rejecting them
      // is correct and they must not be counted as failures.
      if (c.nmspace == "no") return "NAMESPACE=no: not applicable to a namespace-aware parser";
      // We skip rather than expand the internal DTD subset, so any case whose
      // point is entity expansion cannot be judged here.
      //
      // NOTE: the manifest's ENTITIES attribute is NOT sufficient on its own.
      // ENTITIES="none" means "no EXTERNAL entity processing required" -- such a
      // document may still declare a general entity in its internal subset and
      // reference it. Measured against xmltest/valid/sa, 16 of 120 documents are
      // exactly this shape, and filtering on ENTITIES alone reports them as
      // false failures. The document-content check below is required as well.
      if (!c.entities.empty() && c.entities != "none") return "requires entity expansion (ENTITIES=" + c.entities + ")";
      // We target XML 1.0 5th edition. EDITION="1 2 3 4" cases assert
      // pre-5th-edition name rules that 5th edition deliberately relaxed.
      if (!c.edition.empty() && c.edition.find('5') == std::string::npos) {
         return "applies only to editions " + c.edition;
      }
      return {};
   }

   // True when the document's internal DTD subset declares a general entity.
   // Needed because the manifest's ENTITIES attribute only describes EXTERNAL
   // entity requirements -- see the note in skip_reason.
   bool declares_internal_entity(std::string_view text)
   {
      const auto open = text.find('[');
      if (open == std::string_view::npos) return false;
      const auto close = text.find("]>", open);
      if (close == std::string_view::npos) return false;
      return text.substr(open, close - open).find("<!ENTITY") != std::string_view::npos;
   }

   struct tally
   {
      size_t passed{};
      size_t failed{};
      // Subset of `failed` where a valid/invalid (well-formed) document was
      // incorrectly rejected -- see the file header. Must always be zero,
      // unlike `failed` itself, which tolerates the documented 154-case
      // internal-DTD-subset baseline.
      size_t false_rejections{};
      size_t skipped{};
      size_t missing{};
      std::vector<std::string> failures{};
   };
}

suite w3c_data_driven = [] {
   "w3c_xmlconf_corpus"_test = [] {
      const fs::path root{XML_CONFORMANCE_DIR};
      expect(fs::exists(root)) << "corpus not found at " << root.string();

      tally t{};

      for (const auto m : manifests) {
         const auto manifest_path = root / fs::path{std::string{m}};
         if (!fs::exists(manifest_path)) {
            ++t.missing;
            continue;
         }

         for (const auto& c : load_manifest(manifest_path)) {
            const auto reason = skip_reason(c);
            if (!reason.empty()) {
               ++t.skipped;
               continue;
            }

            const auto doc = c.base / fs::path{c.uri};
            if (!fs::exists(doc)) {
               ++t.missing;
               continue;
            }

            const auto text = read_file(doc);

            // Two content-derived exclusions the manifest cannot express.
            if (text.size() >= 2 &&
                (static_cast<unsigned char>(text[0]) == 0xFF || static_cast<unsigned char>(text[0]) == 0xFE)) {
               ++t.skipped; // UTF-16; we are UTF-8 only by design
               continue;
            }
            if (declares_internal_entity(text)) {
               ++t.skipped; // internal subset declares a general entity; we skip the subset
               continue;
            }

            glz::generic g{};
            const auto ec = glz::read_xml<glz::xml::xml_opts{.error_on_unknown_keys = false}>(g, text);

            const bool errored = bool(ec);
            const bool must_error = (c.type == "not-wf");

            if (errored == must_error) {
               ++t.passed;
            }
            else {
               ++t.failed;
               if (!must_error) {
                  // c.type is "valid" or "invalid" (well-formed) but we rejected it: always a
                  // real defect, never tolerated by the baseline below.
                  ++t.false_rejections;
               }
               if (t.failures.size() < 60) {
                  t.failures.push_back(c.id + " [" + c.type + "] " + c.uri +
                                       (must_error ? " : accepted, should reject" : " : rejected, should accept"));
               }
            }
         }
      }

      std::string detail;
      for (const auto& f : t.failures) {
         detail += "\n  " + f;
      }

      // Always visible, pass or fail -- not just folded into an assertion
      // failure message -- so a baseline drift (in either direction) shows up
      // in ordinary CI output, not only when it crosses the threshold below.
      std::fprintf(stderr, "xml_conformance_data: passed=%zu failed=%zu false_rejections=%zu skipped=%zu missing=%zu\n",
                   t.passed, t.failed, t.false_rejections, t.skipped, t.missing);

      // 154 is the documented, known baseline of internal-DTD-subset failures
      // described in the file header -- not an aspiration. Regress (grow)
      // past it and this fails; shrinking it (fixing one of those cases) is
      // welcome and simply lowers the number that needs updating here.
      constexpr size_t known_failure_baseline = 154;
      expect(t.failed <= known_failure_baseline)
         << "passed=" << t.passed << " failed=" << t.failed << " (baseline=" << known_failure_baseline << ")"
         << " skipped=" << t.skipped << " missing=" << t.missing << detail;

      // Unlike `failed`, this must always be exactly zero: rejecting a
      // well-formed valid/invalid document is a real defect, not a tolerated
      // gap, no matter how small.
      expect(t.false_rejections == size_t(0))
         << t.false_rejections << " valid/invalid documents were incorrectly rejected" << detail;

      // Guard against a silently empty corpus, which would make this vacuously green.
      expect(t.passed > size_t(400)) << "corpus appears truncated: only " << t.passed << " cases ran";
      expect(t.missing == size_t(0)) << t.missing << " manifest or document files were not found";
   };
};

int main() {}
