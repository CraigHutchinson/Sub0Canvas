// The layered document (docs/LAYERED_DOCUMENT.md): stable block identities and the issues layer, without a model.
//
// Each block checks requirements of docs/REQUIREMENTS.md (named R-...), through the driver (doc_driver.hpp), so the checks say what a consumer
// observes and survive changes to how the library stores it: identities are compared, never parsed; blocks are found by name; carries are the format's
// names. Renderings a model sees (R-REN) are pinned on their exact wording on purpose: a change there is measured in Sub0Squad (TESTING.md, L4).
#include "sub0platform/testing/check.hpp"
#include "doc_driver.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/content_id.hpp"

#include <simdjson.h>

#include <algorithm>
#include <set>
#include <string>

using namespace req;
using sub0canvas::Canvas;
using sub0canvas::CanvasOp;
using sub0canvas::Transaction;

namespace {

/// A small calculator in the shape of the tier 0 one: methods of a class, each holding the literals of what it parses.
const std::string kCalc = R"(#include <cmath>
#include <string>

class Parser
{
public:
    double expr()
    {
        double v = term();
        while (true) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term()
    {
        double v = unary();
        while (true) {
            if (eat('*')) v *= unary();
            else if (eat('%')) v = std::fmod(v, unary());
            else return v;
        }
    }
    static double factorial(double v)
    {
        double r = 1;
        for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;
        return r;
    }
    double call(const std::string& f, double x)
    {
        if (f == "sin") return std::sin(x);
        if (f == "log") return std::log10(x);
        if (f == "fact") return factorial(x);
        return x;
    }
};

double evaluate(const std::string& s)
{
    Parser p;
    (void)s;
    return p.expr();
}
)";

std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

/// The text of `kCalc` from `from` up to (not including) `to`.
std::string between(const std::string& text, const std::string& from, const std::string& to)
{
    const auto a = text.find(from), b = text.find(to);
    return text.substr(a, b - a);
}

int firstLine(const Doc& d, const std::string& id)
{
    const auto s = d.where(id);
    return s ? s->first : -1;
}

}  // namespace

int main()
{
    const std::string path = "src/calc.cpp";
    {   // R-ID-1: every function-like block gets an identity; a line resolves to the block holding it; a line outside every block to none.
        // R-ISS-1 (precondition): a block knows the literals it holds.
        Doc d;
        d.write(path, kCalc);
        CHECK_EQ(d.blocks(path).size(), static_cast<std::size_t>(5));
        const std::string term = d.id(path, "term");
        CHECK(!term.empty() && firstLine(d, term) == 16);
        CHECK_EQ(d.holder(path, 20), term);
        CHECK(d.holder(path, 1).empty());
        CHECK(d.holdsLiteral(term, "%"));
    }
    {   // R-ID-2: a SYMBOL replace through the canvas keeps the identity, and a block it pushed down keeps its own and resolves to its new lines.
        // R-ID-3: the edit is in the block's history, with its turn; an untouched block's history records nothing.
        Doc d;
        Canvas canvas;
        canvas.addFile(path, kCalc);
        d.write(path, canvas.text(path));
        const std::string term = d.id(path, "term"), call = d.id(path, "call");
        Transaction tx{path, canvas.revision(path), "dev", {}};
        CanvasOp op;
        op.kind = CanvasOp::Kind::replaceSymbol;
        op.symbol = "term";
        op.text = "    double term()\n    {\n        double v = unary();\n        while (true) {\n            if (eat('*')) v *= unary();\n            else if (eat('/')) v /= unary();\n"
                  "            else if (eat('%')) v = std::fmod(v, unary());\n            else return v;\n        }\n    }\n";
        tx.ops.push_back(op);
        CHECK(canvas.apply(tx).ok);
        d.turn(1);
        d.write(path, canvas.text(path));
        CHECK_EQ(d.id(path, "term"), term);
        CHECK_EQ(d.id(path, "call"), call);
        CHECK_EQ(firstLine(d, call), 32);
        const auto history = d.history(term);
        CHECK(history.size() == 2 && history[1].carry == "kept" && history[1].detail == "edited" && history[1].turn == 1);   // added, then edited
        CHECK(!d.logged("kept", call));
    }
    {   // R-ID-4: a whole-file rewrite that reorders functions, changes one signature and renames another keeps every identity, and says how.
        Doc d;
        d.write(path, kCalc);
        const std::string term = d.id(path, "term"), factorial = d.id(path, "factorial"), call = d.id(path, "call"), expr = d.id(path, "expr");
        std::string rewritten = kCalc;
        const std::string factorialText = between(rewritten, "    static double factorial", "    double call");
        rewritten = replaceOnce(rewritten, factorialText, "");
        rewritten = replaceOnce(rewritten, "    double expr()\n", factorialText + "    double expr()\n");           // factorial moved to the top
        rewritten = replaceOnce(rewritten, "double call(const std::string& f, double x)", "double call(const std::string& f, double x, bool degrees)");   // re-signed
        rewritten = replaceOnce(rewritten, "    double term()\n", "    double multiplicative()\n");                // renamed, body unchanged
        rewritten = replaceOnce(rewritten, "v = term();", "v = multiplicative();");
        rewritten = replaceOnce(rewritten, "v += term();", "v += multiplicative();");
        rewritten = replaceOnce(rewritten, "v -= term();", "v -= multiplicative();");
        d.write(path, rewritten);
        CHECK_EQ(d.id(path, "factorial"), factorial);
        CHECK_EQ(d.id(path, "call"), call);
        CHECK_EQ(d.id(path, "multiplicative"), term);
        CHECK_EQ(d.id(path, "expr"), expr);
        CHECK(d.logged("resigned", call) && d.logged("renamed", term));
        CHECK_EQ(firstLine(d, factorial), 7);
    }
    {   // R-ID-5: a split keeps the original's identity and gives the new part its own.
        // R-ID-6: a merge sends the merged block's identity to the survivor, and the survivor's history holds the merged block's.
        Doc d;
        d.write(path, kCalc);
        const std::string call = d.id(path, "call"), factorial = d.id(path, "factorial");
        const std::string split = replaceOnce(kCalc, "        if (f == \"log\") return std::log10(x);\n", "        if (f == \"log\") return logarithm(x);\n");
        const std::string withHelper = replaceOnce(split, "    double call(", "    static double logarithm(double x)\n    {\n        if (x <= 0) return 0;\n        return std::log10(x);\n    }\n    double call(");
        d.write(path, withHelper);
        CHECK_EQ(d.id(path, "call"), call);
        CHECK(!d.id(path, "logarithm").empty() && d.id(path, "logarithm") != call);
        std::string merged = withHelper;
        merged = replaceOnce(merged, between(merged, "    static double factorial", "    static double logarithm"), "");
        merged = replaceOnce(merged, "        if (f == \"fact\") return factorial(x);\n",
                             "        if (f == \"fact\") {\n            double v = x;\n            double r = 1;\n            for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;\n            return r;\n        }\n");
        d.write(path, merged);
        CHECK(d.logged("merged", factorial));
        CHECK_EQ(d.resolvesTo(factorial), call);
        const auto history = d.history(call);
        CHECK(std::any_of(history.begin(), history.end(), [&](const Doc::HistoryEntry& e) { return e.id == factorial && e.carry == "merged"; }));
    }
    {   // R-ID-7: a deleted block's items are orphans, and the renderings say so. R-ID-8: written again, its identity comes back.
        // R-ID-9: moved to another file, its identity comes back there.
        Doc d;
        d.write(path, kCalc);
        const std::string call = d.id(path, "call"), evaluate = d.id(path, "evaluate");
        const std::string without = replaceOnce(kCalc, between(kCalc, "    double call", "};"), "");
        d.write(path, without);
        CHECK(!d.where(call) && d.logged("retired", call));
        const LayerItem orphan = makeItem({.title = "test logarithms: log(100) is 4.6 but 2", .detail = "FAIL 9 - logarithms: ...", .key = "issues/test/logarithms",
                                           .anchors = {blockAnchor(call, path, 30, Anchor::Confidence::suspect)}});
        CHECK(d.detail(orphan).find("no longer exists") != std::string::npos);
        CHECK(d.summary({orphan}).find("a function that no longer exists") != std::string::npos);
        d.write(path, kCalc);                                            // written again (the canvas's "FILE first, then SYMBOL each function")
        CHECK_EQ(d.id(path, "call"), call);
        CHECK(d.logged("revived", call));
        const std::string evalText = kCalc.substr(kCalc.find("double evaluate"));
        d.write(path, replaceOnce(kCalc, evalText, ""));
        d.write("src/eval.cpp", "#include <string>\n\n" + evalText);
        CHECK_EQ(d.id("src/eval.cpp", "evaluate"), evaluate);
    }
    {   // R-ID-10: indentation-based languages (Python) get identities too, carried through a reorder.
        Doc d;
        d.write("calc/ops.py", "def add(a, b):\n    return a + b\n\n\ndef sub(a, b):\n    return a - b\n");
        const std::string sub = d.id("calc/ops.py", "sub");
        d.write("calc/ops.py", "def sub(a, b):\n    return a - b\n\n\ndef add(a, b):\n    return a + b\n");
        CHECK_EQ(d.id("calc/ops.py", "sub"), sub);
        CHECK_EQ(firstLine(d, sub), 1);
    }
    {   // R-ISS-2: an operator held by several blocks points at none of them (tier 0 bugs-10: "(1+2" did not throw was put on expr() and unary(), the
        // defect was in primary()); held by one block, it still points there.
        Doc d;
        d.write(path, replaceOnce(kCalc, "    static double factorial", "    double unary()\n    {\n        if (eat('-')) return -unary();\n        return 0;\n    }\n    static double factorial"));
        const auto issues = d.issues("GATE FAIL: 2 of 16 tests failed\n"
                                     "FAIL 1 - invalid_input_throws: CHECK_THROWS failed: ev(\"(1-2\") did not throw\n"
                                     "FAIL 2 - remainder: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\n");
        CHECK_EQ(issues.size(), static_cast<std::size_t>(2));
        if (issues.size() == 2) {
            CHECK(issues[0].anchors.empty());
            CHECK(issues[1].anchors.size() == 1 && issues[1].anchors[0].blockId == d.id(path, "term"));
            CHECK(d.summary(issues).find(" 1. test invalid_input_throws: CHECK_THROWS failed: ev(\"(1-2\") did not throw\n") != std::string::npos);
        }
    }
    {   // R-ISS-7: a runner that reports no expression (Python's unittest: `test_modulo: 30.0 != 1`) is localised from the test's own function in the
        // test sources it is given; without them the failure is anchored nowhere rather than guessed. R-ISS-8: a Windows runner's carriage returns,
        // even inside a line, change nothing.
        Doc d;
        d.write(path, kCalc);
        const std::string verdict = "GATE FAIL: 1 of 9 tests failed\nFAIL test_modulo: 30.0 != 1\n";
        const auto bare = d.issues(verdict);
        CHECK(bare.size() == 1 && bare[0].anchors.empty() && bare[0].key == "issues/test/test_modulo");
        const std::map<std::string, std::string> sources{{"tests/test_calc.py", "def helper(a, b):\n    assert a == b\n\n\ndef test_modulo():\n    helper(ev('10%3'), 1)\n"}};
        const auto located = d.issues(verdict, sources);
        CHECK(located.size() == 1 && located[0].anchors.size() == 1 && located[0].anchors[0].blockId == d.id(path, "term"));
        const auto crlf = d.issues("GATE FAIL: 1 of 9 tests failed\r\nFAIL 6 - modulo\r: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\r\n");
        CHECK(crlf.size() == 1 && crlf[0].key == "issues/test/modulo" && crlf[0].anchors.size() == 1 && crlf[0].anchors[0].blockId == d.id(path, "term"));
    }
    {   // R-ISS-1: failing tests land on their suspect block (named like the test, or holding the literal under test), marked suspect.
        // R-ISS-5: an item's key is the same next turn, whatever its number. R-REN-1..4: the renderings, at the CURRENT lines after a rewrite.
        Doc d;
        d.write(path, kCalc);
        const std::string verdict =
            "GATE FAIL: 3 of 16 tests failed\n"
            "FAIL 6 - modulo: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\n"
            "FAIL 7 - factorial: CHECK_NEAR failed: ev(\"5!\") is 24 but 120 is 120\n"
            "FAIL 9 - logarithms: CHECK_NEAR failed: ev(\"log(100)\") is 4.60517 but 2 is 2\n";
        const auto issues = d.issues(verdict);
        CHECK_EQ(issues.size(), static_cast<std::size_t>(3));
        if (issues.size() == 3) {
            CHECK(issues[0].kind == "test" && issues[0].number == 1 && issues[0].anchors.size() == 1 && issues[0].anchors[0].blockId == d.id(path, "term"));
            CHECK(issues[0].anchors[0].confidence == Anchor::Confidence::suspect);
            CHECK(issues[1].anchors.size() == 1 && issues[1].anchors[0].blockId == d.id(path, "factorial"));
            CHECK_EQ(issues[1].key, std::string{"issues/test/factorial"});
            CHECK(issues[2].anchors.size() == 1 && issues[2].anchors[0].blockId == d.id(path, "call"));
            const std::string summary = d.summary(issues);
            CHECK(summary.rfind("3 issues (`=== READ: ISSUE n ===`", 0) == 0);
            CHECK(summary.find(" 1. test modulo: CHECK_NEAR failed: ev(\"10%3\")") != std::string::npos && summary.find("likely in term() (lines 16-24)") != std::string::npos);
            CHECK(summary.find(" 3. test logarithms") != std::string::npos && summary.find("likely in call()") != std::string::npos);
            // The identities hold after a whole-file rewrite that moves everything down: the same issues render at the new lines.
            d.write(path, "// a header comment\n// and another\n" + kCalc);
            CHECK(d.summary(issues).find("likely in term() (lines 18-26)") != std::string::npos);
            const std::string detail = d.detail(issues[0]);
            CHECK(detail.rfind("=== ISSUE 1 (test) ===\nFAIL 6 - modulo", 0) == 0);
            CHECK(detail.find("Likely in src/calc.cpp, term(), now at lines 18-26:\n  18|     double term()") != std::string::npos);
            CHECK(d.focus(issues).find("src/calc.cpp:18\n") != std::string::npos);
            // R-REN-3: the first issues in full under the summary, the rest left to a read; within a character budget, the first always.
            const std::string two = d.inlined(issues, 2);
            CHECK(two.rfind("The first 2 in full:\n=== ISSUE 1 (test) ===", 0) == 0);
            CHECK(two.find("=== ISSUE 2 (test) ===") != std::string::npos && two.find("=== ISSUE 3") == std::string::npos);
            CHECK(two.find("  18|     double term()") != std::string::npos);
            CHECK(two.find("The rest (3) are listed above: `=== READ: ISSUE n ===` shows one in full.\n") != std::string::npos);
            const std::string all = d.inlined(issues, 5);
            CHECK(all.rfind("In full:\n", 0) == 0 && all.find("=== ISSUE 3") != std::string::npos && all.find("The rest") == std::string::npos);
            const std::string tight = d.inlined(issues, 3, 10);
            CHECK(tight.rfind("The first 1 in full:\n=== ISSUE 1", 0) == 0 && tight.find("The rest (2-3)") != std::string::npos);
            CHECK(d.inlined(issues, 0).empty() && d.inlined({}).empty());
            // R-ISS-5 across turns: the same verdict against the moved text gives the same keys in the same order.
            const auto again = d.issues(verdict);
            CHECK(again.size() == 3 && again[0].key == issues[0].key && again[1].key == issues[1].key && again[2].key == issues[2].key);
        }
        d.write(path, kCalc);
        // R-ISS-3: every diagnostic lands exactly on the block holding its line, and one message at several places is ONE item with an anchor per place.
        // R-ISS-4: a diagnostic outside every block keeps its path and line.
        const std::string compile =
            "does not compile: src/calc.cpp\n"
            "  src/calc.cpp:11:35: error: 'terms' was not declared in this scope\n"
            "     11 |             if (eat('+')) v += terms();\n"
            "        |                                   ^~~~~\n"
            "  src/calc.cpp:12:40: error: 'terms' was not declared in this scope\n"
            "  src/calc.cpp:43:20: error: 'terms' was not declared in this scope\n"
            "  src/calc.cpp:2:1: error: expected unqualified-id before 'oops'\n"
            "The code does not compile, so no test could run. Fix the errors first.\n";
        const auto errs = d.issues(compile);
        CHECK_EQ(errs.size(), static_cast<std::size_t>(2));
        if (errs.size() == 2) {
            CHECK(errs[0].kind == "error" && errs[0].anchors.size() == 3);
            CHECK(errs[0].key.rfind("issues/error/", 0) == 0 && errs[0].key != errs[1].key);
            CHECK(errs[0].anchors[0].blockId == d.id(path, "expr") && errs[0].anchors[0].confidence == Anchor::Confidence::exact);
            CHECK(errs[0].anchors[2].blockId == d.id(path, "evaluate"));
            CHECK(errs[0].detail.find("^~~~~") != std::string::npos);                            // the first occurrence keeps its caret
            CHECK(errs[1].anchors.size() == 1 && errs[1].anchors[0].blockId.empty() && errs[1].anchors[0].line == 2);
            const std::string summary = d.summary(errs);
            CHECK(summary.find("- in 3 places: expr(), evaluate()") != std::string::npos);
            CHECK(summary.find("- in src/calc.cpp:2") != std::string::npos);
            CHECK(d.detail(errs[0]).find("It occurs at 3 places") != std::string::npos);
            CHECK(d.detail(errs[1]).find("At src/calc.cpp:2 (outside any function):") != std::string::npos);
        }
        {   // R-REN-3: two issues in one function: inline, its code is shown once and the second names it; read alone, each shows its code.
            const auto both = d.issues("does not compile: src/calc.cpp\n  src/calc.cpp:11:35: error: 'terms' was not declared in this scope\n"
                                       "  src/calc.cpp:12:20: error: 'v2' was not declared in this scope\n");
            CHECK_EQ(both.size(), static_cast<std::size_t>(2));
            if (both.size() == 2) {
                const std::string inl = d.inlined(both);
                CHECK(inl.find("In src/calc.cpp, expr(), now at lines") != std::string::npos);
                CHECK(inl.find("=== ISSUE 2 (error) ===") != std::string::npos && inl.find("In expr() (lines ") != std::string::npos && inl.find(", shown above.\n") != std::string::npos);
                CHECK(d.detail(both[1]).find("shown above") == std::string::npos);
            }
        }
        // R-ISS-6: Python's syntax check names a line too.
        const auto py = d.issues("syntax error in src/calc.cpp:20: invalid syntax\n    else if\nThe code does not parse, so no test could run.");
        CHECK(py.size() == 1 && py[0].anchors.size() == 1 && py[0].anchors[0].blockId == d.id(path, "term"));
    }
    {   // R-REN-5: reads name issues by number; anything else is not an issue read.
        const auto asks = sub0canvas::parseIssueReads("=== READ: ISSUE 3 ===\n=== READ: ISSUES 1, 2 ===\n=== READ: src/calc.cpp SYMBOL term ===\n");
        CHECK(asks == (std::vector<int>{3, 1, 2}));
        CHECK(sub0canvas::parseIssueReads("=== READ: src/ISSUE.cpp LINES 1-3 ===").empty());
    }
    {   // R-FMT-1: the snapshot is valid JSON carrying the schema, the turn, each file's revision, the blocks, their history and every layer's anchors.
        Doc d;
        d.turn(4);
        d.write(path, kCalc);
        const auto issues = d.issues("FAIL 6 - modulo: CHECK_NEAR failed: ev(\"10%3\") is 3.33333 but 1 is 1\n");
        const std::string j = d.snapshot({{"issues", issues}}, {{path, 2}});
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        CHECK(!parser.parse(j).get(doc));
        CHECK(j.find("\"schema\":\"layered-doc-1\"") != std::string::npos && j.find("\"turn\":4") != std::string::npos);
        CHECK(j.find("\"revision\":2") != std::string::npos && j.find("\"carry\":\"added\"") != std::string::npos);
        CHECK(j.find("\"confidence\":\"suspect\"") != std::string::npos && j.find("\"blockId\":\"" + d.id(path, "term") + "\"") != std::string::npos);
    }
    {   // R-SRC-6, R-FMT-3: a compiler's column and range are read; two sites on one line stay two anchors; Python's form has no column.
        Doc d;
        d.write(path, kCalc);
        const std::string gcc =
            "src/calc.cpp:12:20: error: 'unary' was not declared in this scope\n"
            "   12 |         double v = unary();\n"
            "      |                    ^~~~~\n"
            "src/calc.cpp:12:30: error: 'unary' was not declared in this scope\n"
            "src/calc.cpp:15:5-9: warning: unused variable -Werror=unused\n"
            "src/calc.cpp:20: invalid syntax\n";
        const auto items = d.issues(gcc);
        CHECK(items.size() == 3);
        if (items.size() == 3) {
            CHECK(items[0].anchors.size() == 2);   // the same message at two columns of one line is one item with two anchors, never one
            CHECK(items[0].anchors[0].line == 12 && items[0].anchors[0].column == 20);
            CHECK(items[0].anchors[0].endLine == 12 && items[0].anchors[0].endColumn == 25);   // the underline `^~~~~`: five characters, the end just past them
            CHECK(items[0].anchors[1].column == 30 && items[0].anchors[1].endColumn == 0);     // a repeat names no underline: a point
            CHECK(items[1].anchors[0].column == 5 && items[1].anchors[0].endLine == 15 && items[1].anchors[0].endColumn == 10);   // `col-endcol` as a range, end just past
            CHECK(items[2].anchors[0].line == 20 && items[2].anchors[0].column == 0 && items[2].anchors[0].endColumn == 0);
        }
        // A caret alone is a point; an underline that does not cover the named column is not that diagnostic's range.
        const auto point = d.issues("src/calc.cpp:12:20: error: x\n   12 |         double v = unary();\n      |                    ^\n");
        CHECK(point.size() == 1 && point[0].anchors[0].column == 20 && point[0].anchors[0].endColumn == 0);
        const auto elsewhere = d.issues("src/calc.cpp:12:20: error: x\n   12 |         double v = unary();\n      |  ^~~~~\n");
        CHECK(elsewhere.size() == 1 && elsewhere[0].anchors[0].endColumn == 0);
    }
    {   // R-FMT-3: the snapshot writes the item id, the anchor's column and range, and a layerMeta entry for the issues; every old field is still there.
        Doc d;
        d.write(path, kCalc);
        const auto items = d.issues("src/calc.cpp:12:20: error: 'unary' was not declared in this scope\n   12 |         double v = unary();\n      |                    ^~~~~\n");
        sub0canvas::SnapshotSpec spec;
        spec.layers = {{"issues", items}};
        spec.revisions = {{path, 3}};
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        const std::string text = d.snapshot(spec);
        CHECK(!parser.parse(text).get(doc));
        const auto item = doc["layers"]["issues"].at(0);
        CHECK(std::string_view{item["id"]}.rfind("issues/error/", 0) == 0);
        CHECK(std::string_view{item["title"]}.find("unary") != std::string_view::npos && std::string_view{item["detail"]}.find("^~~~~") != std::string_view::npos);   // old fields
        const auto anchor = item["anchors"].at(0);
        CHECK(int64_t{anchor["line"]} == 12 && int64_t{anchor["column"]} == 20 && int64_t{anchor["endLine"]} == 12 && int64_t{anchor["endColumn"]} == 25);
        CHECK(std::string_view{anchor["confidence"]} == "exact" && std::string_view{anchor["blockId"]} == d.id(path, "expr"));
        CHECK(std::string_view{doc["layerMeta"]["issues"]["label"]} == "Issues" && std::string_view{doc["layerMeta"]["issues"]["producer"]}.size() > 0);
        // A layer the caller alone knows has no meta unless given.
        sub0canvas::SnapshotSpec other;
        other.layers = {{"notes", {makeItem({"notes", "note", "a note", "", "notes/1", {}})}}};
        CHECK(!parser.parse(d.snapshot(other)).get(doc));
        CHECK(doc["layerMeta"].error() != simdjson::SUCCESS);
        other.layerMeta = {{"notes", {"Notes", "what a reviewer said", "a person"}}};
        CHECK(!parser.parse(d.snapshot(other)).get(doc) && std::string_view{doc["layerMeta"]["notes"]["label"]} == "Notes");
    }
    {   // R-SRC-5, R-FMT-3: every file carries its revision number AND an opaque revision id (the number as "r<n>" unless the caller names another), a content id
        // that is git's blob id, and the policies that made the snapshot are recorded.
        Doc d;
        d.write(path, kCalc);
        sub0canvas::SnapshotSpec spec;
        spec.revisions = {{path, 3}};
        spec.revisionIds = {{path, "9f2c-7"}};
        spec.policies = {{"loop", "mock-loop@1"}, {"reconcile", "reconcile@1"}};
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        CHECK(!parser.parse(d.snapshot(spec)).get(doc));
        const auto f = doc["files"][path];
        CHECK(int64_t{f["revision"]} == 3 && std::string_view{f["revisionId"]} == "9f2c-7");
        CHECK(std::string_view{f["textId"]} == sub0canvas::contentId(kCalc) && std::string_view{f["text"]} == kCalc);
        CHECK(std::string_view{doc["policies"]["loop"]} == "mock-loop@1" && std::string_view{doc["policies"]["reconcile"]} == "reconcile@1");
        sub0canvas::SnapshotSpec plain;
        plain.revisions = {{path, 3}};
        CHECK(!parser.parse(d.snapshot(plain)).get(doc) && std::string_view{doc["files"][path]["revisionId"]} == "r3" && doc["policies"].error() != simdjson::SUCCESS);
        // The content id is git's blob id (`git hash-object`): stable across runs and platforms, and the same for the same bytes.
        CHECK(sub0canvas::contentId("") == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
        CHECK(sub0canvas::contentId("hello\n") == "ce013625030ba8dba906f756967f9e9ca394464a");
        CHECK(sub0canvas::contentId("what is up, doc?") == "bd9dbf5aae1a3862dd1526723246b20206e5fc37");
        CHECK(sub0canvas::contentId(std::string(70, 'x') + "\n" + std::string(200, 'y') + "\n") == "9ec8b96e22618344ab568bd6348cd3cb884e8fbf");   // more than one 64-byte block
    }
    {   // R-FMT-4: events are per snapshot with `eventsFrom`, and the cumulative log is the same events in order.
        Doc d;
        d.turn(0);
        d.write(path, kCalc);
        const std::size_t afterFirst = d.eventCount();
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        CHECK(!parser.parse(d.snapshot({})).get(doc));
        CHECK(int64_t{doc["eventsFrom"]} == 0 && doc["events"].get_array().size() == afterFirst && afterFirst >= 5);
        d.turn(1);
        std::string renamed = kCalc;
        renamed.replace(renamed.find("factorial"), 9, "fact_of");
        renamed.replace(renamed.find("factorial"), 9, "fact_of");
        d.write(path, renamed);
        sub0canvas::SnapshotSpec second;
        second.eventsFrom = afterFirst;
        CHECK(!parser.parse(d.snapshot(second)).get(doc));
        CHECK(int64_t{doc["eventsFrom"]} == static_cast<int64_t>(afterFirst) && doc["events"].get_array().size() == d.eventCount() - afterFirst && d.eventCount() > afterFirst);
        for (auto e : doc["events"].get_array()) CHECK(int64_t{e["turn"]} == 1);   // only what happened since the previous snapshot
        // The cumulative log: a header, then one line per event with its position; the snapshot's events are a slice of it.
        const std::string log = d.eventLog("calc");
        std::size_t lines = 0;
        for (const char c : log) lines += c == '\n';
        CHECK(lines == d.eventCount() + 1 && log.rfind("{\"schema\":\"canvas-events-1\",\"document\":\"calc\"}\n", 0) == 0);
        CHECK(log.find("{\"seq\":" + std::to_string(afterFirst) + ",\"kind\":\"block\"") != std::string::npos);
    }
    {   // R-AUTH-14, R-FMT-5: by id, a text is carried once: a later snapshot lists the texts that are new and refers to the others; alone, a snapshot carries all it needs.
        Doc d;
        d.write("a.cpp", "int a() { return 1; }\n");
        d.write("b.cpp", "int b() { return 2; }\n");
        std::set<std::string> carried;
        sub0canvas::SnapshotSpec spec;
        spec.textsById = true;
        spec.textsCarried = &carried;
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        CHECK(!parser.parse(d.snapshot(spec)).get(doc));
        CHECK(doc["files"]["a.cpp"]["text"].error() != simdjson::SUCCESS);   // no inline text
        CHECK(doc["texts"].get_object().size() == 2 && carried.size() == 2);
        CHECK(std::string_view{doc["texts"][std::string{std::string_view{doc["files"]["a.cpp"]["textId"]}}]} == "int a() { return 1; }\n");
        d.write("b.cpp", "int b() { return 3; }\n");
        CHECK(!parser.parse(d.snapshot(spec)).get(doc));
        CHECK(doc["texts"].get_object().size() == 1 && carried.size() == 3);   // only the changed file's new text
        sub0canvas::SnapshotSpec alone;
        alone.textsById = true;
        CHECK(!parser.parse(d.snapshot(alone)).get(doc) && doc["texts"].get_object().size() == 2);
        d.write("c.cpp", "int a() { return 1; }\n");   // the same bytes under another path are one text
        CHECK(!parser.parse(d.snapshot(alone)).get(doc) && doc["texts"].get_object().size() == 2 && doc["files"].get_object().size() == 3);
    }
    return finish();
}
