// Slice B4: the source-anchor resolver (docs/REQUIREMENTS.md R-SRC-1, R-SRC-2, R-AUTH-3; docs/BACKBONE.md section 5.1; docs/sessions/canvas-source-addressing.md).
// The truth below is written by hand from the rules, never from the resolver's own output:
//   * `unit`: small texts with the answer worked out per case (unchanged, an unrelated insertion, whitespace, equal quotes, a moved block, a block deleted and
//     revived, a rewritten block, a missing file, policy changes);
//   * `mock`: 26 anchors made at revision 1 (or later) of the mock squad's attempts (fixtures/mock), resolved at each attempt's last revision, the expectations
//     read off the revisions' diffs by hand (the table sits beside the anchors); the same anchors run again through the RevisionIndex over a `CanvasSource`
//     (the DocumentSource seam), which must agree outcome for outcome;
//   * `stream`: 24 anchors made on a seeded corpus (bench/corpus.hpp) and resolved after its edit stream; the truth is the generator's own record of each edit
//     (renamed, moved, removed, revived, split, merged), read by an independent oracle that finds the function by name and the quote line by text.
// The property under test everywhere: a `resolved` result is exact and right, and anything the resolver is not certain of is flagged `moved` with a confidence
// or `orphaned`: zero silent wrong moves.
//
// Argument: the fixtures directory.
#include "sub0platform/testing/check.hpp"

#include "bench/corpus.hpp"
#include "fixture_source.hpp"
#include "sub0canvas/anchor.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/content_id.hpp"
#include "sub0canvas/resolver.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sub0canvas;
namespace fs = std::filesystem;
using fixture::CanvasSource;

namespace {

// ---- helpers -----------------------------------------------------------------------------------------------------------------------------------------------

/// A canvas-like document under test: files with a block index fed every version.
struct Doc
{
    BlockIndex index;
    std::map<std::string, std::string> files;
    void put(const std::string& path, const std::string& text)
    {
        files[path] = text;
        index.update(path, text);
    }
    void drop(const std::string& path)
    {
        files.erase(path);
        index.update(path, "");
    }
    [[nodiscard]] CanvasDocument view(const std::string& revision) const { return CanvasDocument(index, files, revision); }
};

constexpr const char* kPath = "src/m.cpp";

const std::string kBase =
    "#include <cmath>\n"
    "\n"
    "int add(int a, int b)\n"
    "{\n"
    "    int sum = a + b;\n"
    "    return sum;\n"
    "}\n"
    "\n"
    "int twice(int v)\n"
    "{\n"
    "    return v + v;\n"
    "}\n";

Resolution resolveIn(const Doc& d, const std::string& revision, const SourceAnchor& a, const ResolvePolicy& policy = {})
{
    const CanvasDocument view = d.view(revision);
    return resolveAnchor(a, view, policy);
}

/// The soundness check every test applies: a resolved result must hold the quote's exact bytes at its location.
bool locationHoldsQuote(const Doc& d, const Resolution& r, const SourceAnchor& a)
{
    if (r.outcome == Outcome::orphaned) return true;
    const auto it = d.files.find(r.path);
    if (it == d.files.end()) return false;
    std::size_t off = 0;
    for (int l = 1; l < r.line; ++l) off = it->second.find('\n', off) + 1;
    off += static_cast<std::size_t>(r.column - 1);
    return it->second.compare(off, a.quote.size(), a.quote) == 0;
}

// ---- unit cases --------------------------------------------------------------------------------------------------------------------------------------------

void testUnit()
{
    Doc d;
    d.put(kPath, kBase);
    const SourceAnchor a = makeLineAnchor(kPath, "r1", kBase, 5, &d.index);   // "    int sum = a + b;"
    CHECK_EQ(a.quote, std::string("    int sum = a + b;"));
    CHECK(!a.blockId.empty());
    CHECK_EQ(a.column, 1);
    CHECK_EQ(a.endColumn, 21);

    // Same revision: the document is the one the anchor was made against.
    {
        const Resolution r = resolveIn(d, "r1", a);
        CHECK(r.outcome == Outcome::resolved);
        CHECK(r.method == Method::unchanged);
        CHECK(r.editable);
        CHECK_EQ(r.line, 5);
        CHECK_EQ(r.candidates, std::size_t{1});
    }
    // Another revision with the same text: found by identity and exact quote, once; nothing shifted.
    {
        const Resolution r = resolveIn(d, "r2", a);
        CHECK(r.outcome == Outcome::resolved);
        CHECK(r.method == Method::exact);
        CHECK(r.reason == Reason::none);
        CHECK_EQ(r.line, 5);
        CHECK_EQ(r.lineShift, 0);
        CHECK_EQ(r.confidence, 1.0);
        CHECK_EQ(r.blockId, a.blockId);
    }
    // An unrelated insertion above: resolved at the shifted line, and the shift is reported (never silent).
    {
        Doc e = d;
        e.put(kPath, "// header\n// more\n" + kBase);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::resolved);
        CHECK(r.reason == Reason::shifted);
        CHECK_EQ(r.line, 7);
        CHECK_EQ(r.lineShift, 2);
        CHECK(r.editable);
        CHECK_EQ(r.path, std::string(kPath));
    }
    // Whitespace only: found, but not exact, so flagged.
    {
        Doc e = d;
        std::string t = kBase;
        t.replace(t.find("    int sum = a + b;"), 20, "        int  sum =  a + b;");
        e.put(kPath, t);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.method == Method::whitespace);
        CHECK(r.reason == Reason::whitespaceOnly);
        CHECK_EQ(r.confidence, 0.95);
        CHECK(!r.editable);
        CHECK_EQ(r.line, 5);
    }
    // Edited a little: a similar text, flagged with its similarity as confidence (at most the cap).
    {
        Doc e = d;
        std::string t = kBase;
        t.replace(t.find("int sum = a + b;"), 16, "int sum = a + b + 1;");
        e.put(kPath, t);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.method == Method::fuzzy);
        CHECK(r.confidence >= 0.6 && r.confidence <= 0.75);
        CHECK(!r.editable);
        CHECK_EQ(r.line, 5);
        // The same edit under a stricter policy is not trusted at all: the block alone is offered.
        ResolvePolicy strict;
        strict.fuzzyMin = 0.99;
        const Resolution s = resolveIn(e, "r2", a, strict);
        CHECK(s.method == Method::block);
        CHECK_EQ(s.confidence, 0.3);
        // And a policy that forbids fuzzy matching on short quotes does the same.
        ResolvePolicy shortOnly;
        shortOnly.fuzzyMinChars = 100;
        CHECK(resolveIn(e, "r2", a, shortOnly).method == Method::block);
    }
    // The block's body rewritten: the identity lives (same signature), the quote is gone: the block, at low confidence, never resolved.
    {
        Doc e = d;
        std::string t = kBase;
        t.replace(t.find("    int sum = a + b;\n    return sum;"), 35, "    return a * b * 17 - a;");
        e.put(kPath, t);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.reason == Reason::quoteNotInBlock);
        CHECK_EQ(r.confidence, 0.3);
        CHECK(!r.editable);
        CHECK_EQ(r.blockId, a.blockId);
        CHECK_EQ(r.line, 3);
        CHECK_EQ(r.endLine, 6);
    }
    // A moved block (the functions swapped): identity finds it, the quote is exact, once.
    {
        Doc e = d;
        const std::string swapped =
            "#include <cmath>\n\nint twice(int v)\n{\n    return v + v;\n}\n\nint add(int a, int b)\n{\n    int sum = a + b;\n    return sum;\n}\n";
        e.put(kPath, swapped);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::resolved);
        CHECK_EQ(r.line, 10);
        CHECK_EQ(r.lineShift, 5);
        CHECK(locationHoldsQuote(e, r, a));
    }
    // The block moved to another file: the same exact text, but a bigger step than a shift, so flagged with the identity's confidence.
    {
        Doc e = d;
        e.put(kPath, "#include <cmath>\n\nint twice(int v)\n{\n    return v + v;\n}\n");
        e.put("src/n.cpp", "int add(int a, int b)\n{\n    int sum = a + b;\n    return sum;\n}\n");
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.reason == Reason::blockFollowed);
        CHECK_EQ(r.path, std::string("src/n.cpp"));
        CHECK_EQ(r.line, 3);
        CHECK_EQ(r.confidence, 0.95);
        CHECK(!r.editable);
        CHECK(locationHoldsQuote(e, r, a));
    }
    // A block deleted, then written again: while it is gone the anchor is orphaned (not moved onto something else); once revived it is resolved again.
    {
        Doc e = d;
        e.put(kPath, "#include <cmath>\n\nint twice(int v)\n{\n    return v + v;\n}\n");
        const Resolution gone = resolveIn(e, "r2", a);
        CHECK(gone.outcome == Outcome::orphaned);
        CHECK(gone.reason == Reason::blockRetired);
        CHECK(gone.method == Method::none);
        e.put(kPath, kBase);
        const Resolution back = resolveIn(e, "r3", a);
        CHECK(back.outcome == Outcome::resolved);
        CHECK_EQ(back.line, 5);
        CHECK(back.editable);
    }
    // The file is gone, or its text is not known: orphaned, with that reason.
    {
        Doc e = d;
        e.drop(kPath);
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::orphaned);
        CHECK(r.reason == Reason::fileGone);
    }
    // No block identity (an adapter's anchor, a canvas that cannot derive one): the quote alone, once in the file, is flagged at the file-quote confidence.
    {
        SourceAnchor loose = a;
        loose.blockId.clear();
        const Resolution r = resolveIn(d, "r2", loose);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.reason == Reason::noIdentity);
        CHECK_EQ(r.confidence, 0.7);
        CHECK_EQ(r.line, 5);
        // The context pair cannot lift it to resolved: only identity can.
        CHECK(!r.editable);
    }
    // A whole-block anchor (no quote): resolved by identity alone, and never an edit target.
    {
        SourceAnchor whole;
        whole.path = kPath;
        whole.revision = "r1";
        whole.blockId = a.blockId;
        const Resolution r = resolveIn(d, "r2", whole);
        CHECK(r.outcome == Outcome::resolved);
        CHECK_EQ(r.line, 3);
        CHECK_EQ(r.endLine, 7);
        CHECK(!r.editable);
    }
    // The resolution as JSON, in a fixed order.
    {
        const std::string j = resolutionJson(resolveIn(d, "r2", a));
        CHECK(j.starts_with(R"({"outcome":"resolved","method":"exact","reason":"none","confidence":1.0,)"));
        CHECK(j.contains(R"("editable":true)"));
    }
}

/// Equal quotes: the adversarial family. In every variation the anchored place is the SECOND `x = x + 1;`.
void testDuplicates()
{
    const std::string base =
        "int f(int x)\n"
        "{\n"
        "    x = x + 1;\n"
        "    x = x * 2;\n"
        "    x = x + 1;\n"
        "    return x;\n"
        "}\n";
    Doc d;
    d.put("src/f.cpp", base);
    const SourceAnchor a = makeLineAnchor("src/f.cpp", "r1", base, 5, &d.index);
    // The anchored copy deleted: the first copy is left, once. It is exact text in the right block but is NOT what the anchor pointed at (its context differs):
    // flagged, never resolved.
    {
        Doc e = d;
        e.put("src/f.cpp", "int f(int x)\n{\n    x = x + 1;\n    x = x * 2;\n    return x;\n}\n");
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.reason == Reason::contextChanged);
        CHECK_EQ(r.confidence, 0.8);
        CHECK(!r.editable);
    }
    // Both copies stay and a line goes in above: two equal quotes, the context pair picks the second; still only `moved` (equal text cannot prove which is meant).
    {
        Doc e = d;
        e.put("src/f.cpp", "int f(int x)\n{\n    x = x - 7;\n    x = x + 1;\n    x = x * 2;\n    x = x + 1;\n    return x;\n}\n");
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.method == Method::context);
        CHECK(r.reason == Reason::duplicateByContext);
        CHECK_EQ(r.line, 6);
        CHECK_EQ(r.alternatives, std::size_t{1});
        CHECK(!r.editable);
    }
    // The context destroyed too: equal quotes, none told apart. The one nearest the old line is offered at 0.5; the other is named as an alternative.
    {
        Doc e = d;
        e.put("src/f.cpp", "int f(int x)\n{\n    x = x + 1;\n    x = x - 3;\n    x = x + 1;\n    return x;\n}\n");
        const Resolution r = resolveIn(e, "r2", a);
        CHECK(r.outcome == Outcome::moved);
        CHECK(r.reason == Reason::duplicates);
        CHECK_EQ(r.confidence, 0.5);
        CHECK_EQ(r.line, 5);
        CHECK_EQ(r.alternatives, std::size_t{1});
    }
    // An equal content id proves nothing changed: the original position, though the quote repeats.
    {
        SourceAnchor same = a;
        same.textId = contentId(base);
        const Resolution r = resolveIn(d, "r2", same);
        CHECK(r.outcome == Outcome::resolved);
        CHECK(r.method == Method::unchanged);
        CHECK_EQ(r.line, 5);
        // The same anchor against a changed text gets no such shortcut.
        Doc e = d;
        e.put("src/f.cpp", "int f(int x)\n{\n    x = x + 1;\n    x = x * 2;\n    return x;\n}\n");
        CHECK(resolveIn(e, "r2", same).outcome == Outcome::moved);
    }
    // Quotes that repeat in ANOTHER block do not matter: the search is inside the identity block first.
    {
        const SourceAnchor mid = makeLineAnchor("src/f.cpp", "r1", base, 4, &d.index);   // "    x = x * 2;", once in f
        Doc e = d;
        e.put("src/f.cpp", base + "\nint g(int x)\n{\n    x = x * 2;\n    return x;\n}\n");
        const Resolution r = resolveIn(e, "r2", mid);
        CHECK(r.outcome == Outcome::resolved);
        CHECK_EQ(r.line, 4);
        CHECK_EQ(r.candidates, std::size_t{1});
    }
}

// ---- the invariants every resolution must keep -------------------------------------------------------------------------------------------------------------

std::string squeeze(const std::string& s)
{
    std::string out;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::size_t gHighConfidence = 0, gResolved = 0, gMoved = 0, gOrphaned = 0, gChecked = 0;

/// What must hold of ANY resolution, whatever the truth: (I1) resolved holds the quote's exact bytes; (I2) a confidence of 0.8 or more holds the quote up to
/// whitespace (only exact or whitespace evidence reaches it: a similar text is capped below); (I5) confidence is 1 only when resolved, 0 only when orphaned.
void checkInvariants(const std::map<std::string, std::string>& files, const Resolution& r, const SourceAnchor& a, const std::string& what)
{
    ++gChecked;
    (r.outcome == Outcome::resolved ? gResolved : r.outcome == Outcome::moved ? gMoved : gOrphaned)++;
    CHECK_EQ(r.outcome == Outcome::resolved, r.confidence == 1.0);
    CHECK_EQ(r.outcome == Outcome::orphaned, r.confidence == 0.0);
    if (r.outcome == Outcome::orphaned) return;
    const auto it = files.find(r.path);
    if (it == files.end()) { std::fprintf(stderr, "%s: location in a missing file %s\n", what.c_str(), r.path.c_str()); CHECK(false); return; }
    // The span of the location.
    const std::string& text = it->second;
    std::size_t from = 0;
    for (int l = 1; l < r.line; ++l) from = text.find('\n', from) + 1;
    from += static_cast<std::size_t>(r.column - 1);
    std::size_t to = 0;
    for (int l = 1; l < r.endLine; ++l) to = text.find('\n', to) + 1;
    to += static_cast<std::size_t>(r.endColumn - 1);
    const std::string held = to >= from ? text.substr(from, to - from) : std::string{};
    if (r.outcome == Outcome::resolved && !a.quote.empty() && held != a.quote) {
        std::fprintf(stderr, "%s: resolved but the location holds [%s], not [%s]\n", what.c_str(), held.c_str(), a.quote.c_str());
        CHECK(false);
    }
    if (r.confidence >= 0.8) {
        ++gHighConfidence;
        if (!a.quote.empty() && r.method != Method::block && squeeze(held) != squeeze(a.quote)) {
            std::fprintf(stderr, "%s: confidence %.2f but the location holds [%s], not [%s]\n", what.c_str(), r.confidence, held.c_str(), a.quote.c_str());
            CHECK(false);
        }
    }
    CHECK(!(r.outcome == Outcome::resolved) || r.editable == !a.quote.empty());
    CHECK(!(r.outcome == Outcome::moved) || !r.editable);
    CHECK(!r.detail.empty());
}

// ---- the mock squad's attempts -----------------------------------------------------------------------------------------------------------------------------

/// One persisted attempt: every revision of every file (fixtures/mock/<scenario>/canvas/<revisionFileName: / as %2F, % as %25>.r<N>).
struct Attempt
{
    std::map<std::string, std::map<int, std::string>> texts;   // path -> revision -> text
    int maxRevision = 0;
};

Attempt loadAttempt(const fs::path& dir)
{
    Attempt at;
    for (const auto& entry : fs::directory_iterator(dir / "canvas")) {
        const std::string name = entry.path().filename().string();
        const std::size_t dot = name.rfind(".r");
        if (dot == std::string::npos) continue;
        std::string path;   // the name's percent escapes undone: %2F is '/', %25 is '%'
        for (std::size_t i = 0; i < dot; ++i) {
            if (name[i] == '%' && name.compare(i, 3, "%2F") == 0) { path += '/'; i += 2; }
            else if (name[i] == '%' && name.compare(i, 3, "%25") == 0) { path += '%'; i += 2; }
            else path += name[i];
        }
        const int rev = std::atoi(name.c_str() + dot + 2);
        at.texts[path][rev] = fixture::slurp(entry.path());
        at.maxRevision = std::max(at.maxRevision, rev);
    }
    return at;
}

enum class Kind { exact, edited, gone };

/// One hand-checked anchor: made on `line` of `path` after round `madeRound` (the file's revision number; 1 is the start), whose text there is `hint` (checked, so
/// a wrong line number in this table fails loudly); at the attempt's last revision the truth is `kind`: `exact`, the same text is now on line `finalLine`;
/// `edited`, the text was changed and its edited form is on `finalLine`; `gone`, nothing of it is left in the file. `mustResolve`: nothing in or around the
/// anchored block changed except by shifting lines, so the resolver has no excuse for less than `resolved`.
struct MockAnchor
{
    const char* scenario;
    const char* path;
    int madeRound;
    int line;
    const char* hint;
    Kind kind;
    int finalLine;
    bool mustResolve;
};

constexpr const char* kCalc = "src/calc.cpp";

const MockAnchor kMock[] = {
    // g1: the divisor test replaced (r1 -> r2); everything else untouched.
    {"g1-symbol-fix", kCalc, 1, 5, "    if (d == 0) return 0;", Kind::edited, 5, false},
    {"g1-symbol-fix", kCalc, 1, 6, "    return a / d;", Kind::exact, 6, false},          // its neighbour above changed: context differs, so moved, not resolved
    {"g1-symbol-fix", kCalc, 1, 11, "    return x * x;", Kind::exact, 11, true},
    {"g1-symbol-fix", kCalc, 1, 9, "double square(double x)", Kind::exact, 9, true},
    {"g1-symbol-fix", kCalc, 1, 3, "double divide(double a, double d)", Kind::exact, 3, false},
    // b8: a comment line added in divide (shifts what follows by one), square's body changed.
    {"b8-stale-base", kCalc, 1, 11, "    return x + x;", Kind::edited, 12, false},
    {"b8-stale-base", kCalc, 1, 6, "    return a / d;", Kind::exact, 7, false},
    {"b8-stale-base", kCalc, 1, 9, "double square(double x)", Kind::exact, 10, false},
    {"b8-stale-base", kCalc, 1, 3, "double divide(double a, double d)", Kind::exact, 3, false},   // its first body line was replaced: context differs
    // g5: a function inserted above area: five lines of shift, nothing else.
    {"g5-fix-away-from-diagnostic", kCalc, 1, 16, "    return 3 * square(r) + legacy(r);", Kind::exact, 21, true},
    {"g5-fix-away-from-diagnostic", kCalc, 1, 9, "double square(double x)", Kind::exact, 9, true},
    {"g5-fix-away-from-diagnostic", kCalc, 1, 11, "    return x * x;", Kind::exact, 11, true},
    // b15: comments added and removed, then the divisor test replaced; anchors made at r1, r2 and r3.
    {"b15-flapping-test", kCalc, 1, 9, "double square(double x)", Kind::exact, 10, true},
    {"b15-flapping-test", kCalc, 1, 16, "    return 3 * square(r);", Kind::exact, 17, true},
    {"b15-flapping-test", kCalc, 1, 5, "    if (d == 0) return 0;", Kind::edited, 5, false},
    {"b15-flapping-test", kCalc, 3, 18, "    return 3 * square(r);", Kind::exact, 17, true},
    {"b15-flapping-test", kCalc, 2, 10, "double square(double x)", Kind::exact, 10, true},
    // g4: divide renamed and commented, area re-signed and re-bodied, square deleted (moved elsewhere).
    {"g4-rename-resign-move", kCalc, 1, 6, "    return a / d;", Kind::exact, 7, false},
    {"g4-rename-resign-move", kCalc, 1, 11, "    return x * x;", Kind::gone, 0, false},
    {"g4-rename-resign-move", kCalc, 1, 16, "    return 3 * square(r) + legacy(r);", Kind::edited, 12, false},
    {"g4-rename-resign-move", kCalc, 1, 14, "double area(double r)", Kind::edited, 10, false},
    {"g4-rename-resign-move", kCalc, 3, 17, "    return k * square(r) + legacy(r);", Kind::edited, 12, false},
    {"g4-rename-resign-move", kCalc, 1, 3, "double divide(double a, double d)", Kind::edited, 4, false},
    {"g4-rename-resign-move", kCalc, 1, 1, "#include <stdexcept>", Kind::exact, 1, false},   // outside every block: no identity, so never above 0.7
    // g6: a change and its revert, then the divisor test replaced.
    {"g6-worse-is-reverted", kCalc, 1, 11, "    return x * x;", Kind::exact, 11, true},      // changed to x + x and changed back
    {"g6-worse-is-reverted", kCalc, 1, 1, "#include <stdexcept>", Kind::exact, 1, false},
    {"g6-worse-is-reverted", kCalc, 1, 5, "    if (d == 0) return 0;", Kind::edited, 5, false},
    // g7: eight files, one statement edited in each; a large table block beside it.
    {"g7-eight-turns-texts-by-id", "src/f1.cpp", 1, 4, "    double y = x + k;", Kind::edited, 4, false},
    {"g7-eight-turns-texts-by-id", "src/f1.cpp", 1, 16, "    if (x == 1000) return 0 * x + 1;   // the table row 0 of the unit 1", Kind::exact, 16, true},
    {"g7-eight-turns-texts-by-id", "src/f2.cpp", 1, 9, "double offset2(double x)", Kind::exact, 9, true},
    {"g7-eight-turns-texts-by-id", "src/f3.cpp", 1, 5, "    if (y < 0) y = 0;", Kind::exact, 5, false},
    // b3: the same divisor test replaced, the neighbour kept.
    {"b3-loop-rolled-back", kCalc, 1, 6, "    return a / d;", Kind::exact, 6, false},
};

/// The check of one mock anchor's result against its hand-checked truth.
void checkMock(const MockAnchor& m, const Resolution& r, const std::string& flavor)
{
    const std::string what = std::string(m.scenario) + " " + m.path + ":" + std::to_string(m.line) + " (" + flavor + "): " + std::string(toString(r.outcome)) + "/" +
                             std::string(toString(r.method)) + "/" + std::string(toString(r.reason)) + " conf " + std::to_string(r.confidence) + " at " + r.path + ":" +
                             std::to_string(r.line);
    const auto bad = [&](const char* why) { std::fprintf(stderr, "%s -- %s\n", what.c_str(), why); CHECK(false); };
    switch (m.kind) {
    case Kind::exact:
        // The same text is on finalLine: found, never orphaned; resolved and moved-at-0.7-or-more must be right.
        if (r.outcome == Outcome::orphaned) { bad("the text is still there but the anchor is orphaned"); break; }
        if (r.outcome == Outcome::resolved || r.confidence >= 0.7) {
            if (r.line != m.finalLine || r.path != m.path) bad("wrong place");
        }
        if (m.mustResolve && r.outcome != Outcome::resolved) bad("expected resolved");
        break;
    case Kind::edited:
        // The text was changed: never resolved. A location offered at 0.6 or more by similarity must be the edited text's place; a block offered must hold it.
        if (r.outcome == Outcome::resolved) { bad("resolved although the text was edited"); break; }
        if (r.outcome == Outcome::moved) {
            if (r.method == Method::block) {
                if (m.finalLine < r.line || m.finalLine > r.endLine) bad("the block offered does not hold the edited text");
            } else if (r.confidence >= 0.6 && r.line != m.finalLine) {
                bad("a similar-text location that is not the edited text's place");
            }
        }
        break;
    case Kind::gone:
        if (r.outcome == Outcome::resolved) bad("resolved although the text is gone");
        if (r.outcome == Outcome::moved && r.confidence >= 0.8) bad("high confidence although the text is gone");
        break;
    }
}

/// Replays an attempt round by round (round k applies every file's revision k that exists), making `specs`' anchors when their round has been applied, and
/// returns the anchors, the final document, and for the history flavor a canvas of the same steps.
struct Replay
{
    Doc doc;
    std::vector<SourceAnchor> anchors;
    std::vector<std::string> stepOfRound;   // round -> "r<step>" (the revision id the DocumentSource uses)
    std::string finalRevision;
    std::vector<CanvasSource::Step> steps;
    std::unique_ptr<Canvas> canvas;
};

Replay replayAttempt(const Attempt& at, const std::vector<const MockAnchor*>& specs)
{
    Replay rp;
    rp.canvas = std::make_unique<Canvas>();
    rp.stepOfRound.assign(static_cast<std::size_t>(at.maxRevision) + 1, "");
    std::size_t step = 0;
    for (int round = 1; round <= at.maxRevision; ++round) {
        CanvasSource::Step changed;
        for (const auto& [path, revs] : at.texts) {
            const auto it = revs.find(round);
            if (it == revs.end()) continue;
            rp.doc.put(path, it->second);
            if (round == 1) rp.canvas->addFile(path, it->second);
            else CHECK(rp.canvas->replaceText(path, it->second, "t").ok);
            changed.emplace_back(path, rp.canvas->revision(path));
        }
        if (changed.empty()) continue;
        rp.steps.push_back(std::move(changed));
        rp.stepOfRound[static_cast<std::size_t>(round)] = CanvasSource::idOf(step++);
        for (const MockAnchor* m : specs) {
            if (m->madeRound != round) continue;
            const std::string& text = rp.doc.files.at(m->path);
            SourceAnchor a = makeLineAnchor(m->path, rp.stepOfRound[static_cast<std::size_t>(round)], text, m->line, &rp.doc.index);
            if (a.quote != m->hint) { std::fprintf(stderr, "%s %s:%d: the table says [%s] but the fixture has [%s]\n", m->scenario, m->path, m->line, m->hint, a.quote.c_str()); CHECK(false); }
            rp.anchors.push_back(std::move(a));
        }
    }
    rp.finalRevision = CanvasSource::idOf(step - 1);
    return rp;
}

void testMock(const fs::path& fixtures)
{
    std::map<std::string, std::vector<const MockAnchor*>> byScenario;
    for (const MockAnchor& m : kMock) byScenario[m.scenario].push_back(&m);
    std::size_t total = 0;
    for (const auto& [scenario, specs] : byScenario) {
        const Attempt at = loadAttempt(fixtures / "mock" / scenario);
        Replay rp = replayAttempt(at, specs);
        // The specs in the order the anchors were made (by round).
        std::vector<const MockAnchor*> ordered;
        for (int round = 1; round <= at.maxRevision; ++round)
            for (const MockAnchor* m : specs) if (m->madeRound == round) ordered.push_back(m);
        CHECK_EQ(ordered.size(), rp.anchors.size());
        const CanvasDocument view = rp.doc.view(rp.finalRevision);
        AnchorResolver resolver(view);
        CanvasSource source(*rp.canvas, rp.steps);
        RevisionIndex history(source);
        for (std::size_t i = 0; i < ordered.size() && i < rp.anchors.size(); ++i) {
            const MockAnchor& m = *ordered[i];
            const SourceAnchor& a = rp.anchors[i];
            const Resolution r = resolver.resolve(a);
            checkMock(m, r, "canvas");
            checkInvariants(rp.doc.files, r, a, std::string(scenario) + " canvas");
            // The same anchor through the history: no block id (derived from the line at its revision), identity by RevisionIndex::carry, texts by the source.
            SourceAnchor loose = a;
            loose.blockId.clear();
            HistoryDocument hdoc(history, source, Scope{m.path}, rp.finalRevision);
            const Resolution h = resolveAnchor(loose, hdoc);
            checkMock(m, h, "history");
            checkInvariants(rp.doc.files, h, a, std::string(scenario) + " history");
            if (h.outcome != r.outcome || h.line != r.line || h.path != r.path || h.confidence != r.confidence || h.reason != r.reason) {
                std::fprintf(stderr, "%s %s:%d: the canvas and the history disagree: %s vs %s\n", scenario.c_str(), m.path, m.line, resolutionJson(r).c_str(),
                             resolutionJson(h).c_str());
                CHECK(false);
            }
            ++total;
        }
    }
    CHECK_EQ(total, sizeof(kMock) / sizeof(kMock[0]));
    CHECK_GE(total, std::size_t{26});
}

// ---- the corpus's edit stream ------------------------------------------------------------------------------------------------------------------------------

namespace corpus = sub0canvas::bench;

/// An anchor made on a function of the starting corpus, and the family of (path, name) its text may now live in, kept by following the generator's edit truth
/// (renamed: the name changes; moved: the path; removed: leaves; revived: returns; split: the tail joins; merged: the absorbed one's body goes to the survivor).
struct StreamAnchor
{
    std::string path, name, quote, firstKind;
    int line = 0;
    std::set<std::pair<std::string, std::string>> family, graveyard;
    SourceAnchor anchor;
};

/// The line ranges [first, last] (1-based) of the functions named `name` in `text`: a signature at column 0 holding `name(`, to the closing column-0 brace (C++) or
/// the next blank or unindented line (Python). Independent of the library's block finder.
std::vector<std::pair<int, int>> extentsOf(const std::string& text, const std::string& name, bool python)
{
    std::vector<std::string> lines;
    for (std::size_t b = 0; b < text.size();) {
        const std::size_t e = std::min(text.find('\n', b), text.size());
        lines.push_back(text.substr(b, e - b));
        b = e + 1;
    }
    std::vector<std::pair<int, int>> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (l.empty() || l[0] == ' ' || l[0] == '\t') continue;
        const std::size_t at = l.find(name + "(");
        if (at == std::string::npos) continue;
        if (at > 0 && (std::isalnum(static_cast<unsigned char>(l[at - 1])) || l[at - 1] == '_')) continue;
        std::size_t last = i;
        for (std::size_t j = i + 1; j < lines.size(); ++j) {
            if (python ? (lines[j].empty() || lines[j][0] != ' ') : lines[j] == "}") { last = python ? j - 1 : j; break; }
            last = j;
        }
        out.emplace_back(static_cast<int>(i) + 1, static_cast<int>(last) + 1);
    }
    return out;
}

void testStream()
{
    corpus::CorpusSpec spec;
    spec.seed = 7;
    spec.targetLines = 8000;
    auto made = corpus::generateCorpus(spec);
    CHECK(made.has_value());
    if (!made) return;
    corpus::EditStream stream(*made, 11);
    std::vector<corpus::Commit> commits;
    for (int i = 0; i < 60; ++i) commits.push_back(stream.next());

    Doc doc;
    for (const auto& [path, text] : made->files) doc.put(path, text);

    // Choose the anchors: the first functions the stream touches, a few of each kind, each with a body line of its own.
    std::vector<StreamAnchor> anchors;
    std::map<std::string, int> perKind;
    const auto kindName = [](corpus::EditKind k) {
        static const char* names[] = {"edited", "resigned", "renamed", "added", "removed", "revived", "moved", "split", "merged"};
        return std::string(names[static_cast<int>(k)]);
    };
    const auto familyOf = [&](const std::string& path, const std::string& name) -> StreamAnchor* {
        for (StreamAnchor& s : anchors) if (s.family.contains({path, name})) return &s;
        return nullptr;
    };
    for (const corpus::Commit& c : commits)
        for (const corpus::EditTruth& t : c.truth) {
            using K = corpus::EditKind;
            // A new track when this edit touches an untracked function of the starting corpus.
            if (!t.fileAdded && !t.fileDeleted && !t.name.empty() && anchors.size() < 24 && perKind[kindName(t.kind)] < 6 && !familyOf(t.path, t.name) && made->models.count(t.path)) {
                const corpus::CodeFile& model = made->models.at(t.path);
                for (const corpus::Function& f : model.functions) {
                    if (f.name != t.name) continue;
                    // A body line with enough text, unique in the starting file.
                    for (const std::string& body : f.body) {
                        if (body.find_first_not_of(" {}") == std::string::npos || body.size() < 16) continue;
                        const std::string& text = made->files.at(t.path);
                        const std::string needle = "\n" + body + "\n";
                        const std::size_t first = text.find(needle);
                        if (first == std::string::npos || text.find(needle, first + 1) != std::string::npos) continue;
                        StreamAnchor s;
                        s.path = t.path;
                        s.name = t.name;
                        s.quote = body;
                        s.firstKind = kindName(t.kind);
                        s.line = 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(first) + 1, '\n'));
                        s.family.insert({t.path, t.name});
                        s.anchor = makeLineAnchor(t.path, "c0", text, s.line, &doc.index);
                        CHECK_EQ(s.anchor.quote, body);
                        anchors.push_back(std::move(s));
                        ++perKind[kindName(t.kind)];
                        break;
                    }
                    break;
                }
            }
            // Follow this edit in every family.
            for (StreamAnchor& s : anchors) {
                const std::pair<std::string, std::string> from{t.path, t.name};
                switch (t.kind) {
                case K::renamed: if (s.family.erase(from)) s.family.insert({t.path, t.newName}); break;
                case K::moved: if (s.family.erase(from)) s.family.insert({t.otherPath, t.name}); break;
                case K::removed:
                    if (t.fileDeleted) { for (auto it = s.family.begin(); it != s.family.end();) { if (it->first == t.path) { s.graveyard.insert(*it); it = s.family.erase(it); } else ++it; } }
                    else if (s.family.erase(from)) s.graveyard.insert(from);
                    break;
                case K::revived: if (s.graveyard.erase({t.otherPath, t.newName})) s.family.insert({t.path, t.newName}); break;
                case K::split: if (s.family.contains(from)) s.family.insert({t.path, t.newName}); break;
                case K::merged: if (s.family.erase(from)) s.family.insert({t.path, t.newName}); break;
                default: break;
                }
            }
        }
    CHECK_GE(anchors.size(), std::size_t{20});
    std::set<std::string> kinds;
    for (const StreamAnchor& s : anchors) kinds.insert(s.firstKind);
    CHECK_GE(kinds.size(), std::size_t{5});

    // Replay the stream into the index and the files, each commit's files smallest-growth first (a moved function leaves its file before it enters the next).
    for (const corpus::Commit& c : commits) {
        std::vector<const corpus::FileChange*> order;
        for (const corpus::FileChange& f : c.changes) order.push_back(&f);
        const auto growth = [&](const corpus::FileChange* f) {
            const auto it = doc.files.find(f->path);
            const long before = it == doc.files.end() ? 0 : static_cast<long>(it->second.size());
            return (f->text ? static_cast<long>(f->text->size()) : 0L) - before;
        };
        std::ranges::stable_sort(order, [&](const auto* x, const auto* y) { return growth(x) < growth(y); });
        for (const corpus::FileChange* f : order) {
            if (f->text) doc.put(f->path, *f->text);
            else doc.drop(f->path);
        }
    }
    const corpus::FileMap finalTexts = stream.texts();
    CHECK_EQ(finalTexts.size(), doc.files.size());

    // Resolve, and judge against the oracle.
    const CanvasDocument view = doc.view("c60");
    AnchorResolver resolver(view);
    std::size_t present = 0, absent = 0, resolvedRight = 0;
    for (const StreamAnchor& s : anchors) {
        const Resolution r = resolver.resolve(s.anchor);
        const std::string what = "stream " + s.path + ":" + std::to_string(s.line) + " " + s.name + " (" + s.firstKind + "): " + std::string(toString(r.outcome)) + "/" +
                                 std::string(toString(r.method)) + "/" + std::string(toString(r.reason)) + " conf " + std::to_string(r.confidence) + " at " + r.path +
                                 ":" + std::to_string(r.line);
        checkInvariants(doc.files, r, s.anchor, what);
        // The oracle: the lines equal to the quote inside the family's functions now.
        std::set<std::pair<std::string, int>> truth;
        for (const auto& [path, name] : s.family) {
            const auto it = finalTexts.find(path);
            if (it == finalTexts.end()) continue;
            for (const auto& [first, last] : extentsOf(it->second, name, path.ends_with(".py"))) {
                int line = 0;
                for (std::size_t b = 0; b <= it->second.size();) {
                    const std::size_t e = std::min(it->second.find('\n', b), it->second.size());
                    ++line;
                    if (line >= first && line <= last && it->second.compare(b, e - b, s.quote) == 0 && e - b == s.quote.size()) truth.insert({path, line});
                    b = e + 1;
                }
            }
        }
        const auto bad = [&](const char* why) { std::fprintf(stderr, "%s -- %s\n", what.c_str(), why); CHECK(false); };
        if (truth.empty()) {
            ++absent;
            if (r.outcome == Outcome::resolved) bad("resolved but the quoted text is gone from its function family");
            if (r.outcome == Outcome::moved && r.confidence >= 0.8) bad("high confidence for text that is gone");
        } else {
            ++present;
            const bool right = truth.contains({r.path, r.line});
            if (r.outcome == Outcome::resolved) {
                if (!right) bad("resolved at a place the oracle does not know");
                else ++resolvedRight;
            }
            if (r.outcome == Outcome::moved && r.confidence >= 0.7 && truth.size() == 1 && !right) bad("moved with confidence 0.7 or more to a wrong place");
            if (r.outcome == Outcome::orphaned) bad("the text is still in its function family but the anchor is orphaned");
        }
    }
    CHECK_GE(present, std::size_t{10});
    CHECK_GE(absent, std::size_t{2});
    CHECK_GE(resolvedRight, std::size_t{8});
    std::printf("stream: %zu anchors, %zu with the text still present, %zu gone, %zu resolved and right\n", anchors.size(), present, absent, resolvedRight);
}

/// The window budget bounds the work on a large file with no identity to narrow it, and says so.
void testBudget()
{
    std::string big;
    for (int i = 0; i < 20000; ++i) big += "    value" + std::to_string(i) + " = other" + std::to_string(i * 7) + " + " + std::to_string(i) + ";\n";
    Doc d;
    d.put("src/big.cpp", big);
    SourceAnchor a;
    a.path = "src/big.cpp";
    a.revision = "r1";
    a.quote = "    zebra = quokka * giraffe - hippo;";
    a.line = 10000;
    const CanvasDocument view = d.view("r2");
    ResolvePolicy policy;
    const Resolution r = resolveAnchor(a, view, policy);
    CHECK(r.outcome == Outcome::orphaned);
    CHECK(r.truncated);
    CHECK_EQ(r.candidates, policy.fuzzyWindowBudget);
    policy.fuzzyWindowBudget = 100;
    CHECK_EQ(resolveAnchor(a, view, policy).candidates, std::size_t{100});
}

void testUnit2()
{
    testUnit();
    testDuplicates();
    testBudget();
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_resolver <fixtures dir>\n"); return 2; }
    testUnit2();
    testMock(argv[1]);
    testStream();
    return finish();
}
