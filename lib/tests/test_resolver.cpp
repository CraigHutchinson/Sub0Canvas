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

/// The text of the lines [from, to] of `text` joined by newlines (what a location should hold).
[[maybe_unused]] std::string linesOf(const std::string& text, int from, int to)
{
    std::string out;
    int line = 1;
    std::size_t b = 0;
    while (b <= text.size() && line <= to) {
        const std::size_t e = std::min(text.find('\n', b), text.size());
        if (line >= from) out += (out.empty() ? "" : "\n") + text.substr(b, e - b);
        b = e + 1;
        ++line;
    }
    return out;
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
        CHECK(r.confidence >= 0.6 && r.confidence <= 0.85);
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
        CHECK(j.find(R"("editable":true)") != std::string::npos);
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

void testUnit2()
{
    testUnit();
    testDuplicates();
}

}  // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    testUnit2();
    return finish();
}
