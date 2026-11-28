// G1: block identity across a project's history (docs/REQUIREMENTS.md R-SRC-3, R-SRC-5, R-SRC-8, R-SCALE-1, R-SCALE-7, R-SCALE-9; docs/BACKBONE.md sections 5 and 6;
// docs/SCALE.md section 4). Everything here reads CAPTURED data (fixtures/git/tinycalc, written by tools/git-fixture.mjs from a small invented repository) and the
// hand-checked truth beside it (fixtures/scenarios/g1-tinycalc/truth.json): no test runs git or touches the network. The live git adapter is checked by
// test/history.test.js against a repository built from the same plan.
//
// Argument: the fixtures directory.
#include "sub0platform/testing/check.hpp"

#include "fixture_source.hpp"
#include "sub0canvas/block_table.hpp"
#include "sub0canvas/git_plumbing.hpp"
#include "sub0canvas/revision_index.hpp"

#include <simdjson.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace sub0canvas;
using namespace std::literals;
namespace fs = std::filesystem;
using fixture::FixtureSource;

namespace {

std::string carryName(Carry c)
{
    switch (c) {
        case Carry::kept: return "kept";
        case Carry::resigned: return "resigned";
        case Carry::renamed: return "renamed";
        case Carry::revived: return "revived";
        case Carry::added: return "added";
        case Carry::retired: return "retired";
        case Carry::merged: return "merged";
    }
    return "?";
}

// ---- the hand-checked truth ----------------------------------------------------------------------------------------------------------------------------

struct ExpectedFunction
{
    std::string name;
    std::vector<std::string> events;   // "label|carry|path|name|detail", a merged block's detail written "->survivor"
};
struct Scenario
{
    std::string name, revision;
    Scope scope;
    std::size_t maxRevisions = 0;
    bool shallow = false, historyCut = false;
    std::vector<ExpectedFunction> functions;
};

std::string text(simdjson::dom::element e) { return std::string(std::string_view(e)); }

std::vector<Scenario> loadTruth(const fs::path& file)
{
    std::vector<Scenario> out;
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.load(file.string()).get(doc) != simdjson::SUCCESS) { std::fprintf(stderr, "cannot parse %s\n", file.string().c_str()); std::exit(2); }
    for (simdjson::dom::element s : doc["scenarios"]) {
        Scenario sc;
        sc.name = text(s["name"]);
        sc.revision = text(s["revision"]);
        for (simdjson::dom::element p : s["scope"]) sc.scope.push_back(text(p));
        uint64_t n = 0;
        if (s["maxRevisions"].get(n) == simdjson::SUCCESS) sc.maxRevisions = n;
        bool b = false;
        if (s["historyCut"].get(b) == simdjson::SUCCESS) sc.historyCut = b;
        std::string_view source;
        if (s["source"].get(source) == simdjson::SUCCESS) sc.shallow = source == "shallow";
        for (simdjson::dom::element f : s["functions"]) {
            ExpectedFunction fn;
            fn.name = text(f["name"]);
            for (simdjson::dom::element ev : f["events"]) {
                std::vector<std::string> cells;
                for (simdjson::dom::element c : ev) cells.push_back(c.is_null() ? std::string() : text(c));
                while (cells.size() < 6) cells.emplace_back();
                const std::string detail = cells[5].empty() ? cells[4] : "->" + cells[5];
                fn.events.push_back(std::format("{}|{}|{}|{}|{}", cells[0], cells[1], cells[2], cells[3], detail));
            }
            sc.functions.push_back(std::move(fn));
        }
        out.push_back(std::move(sc));
    }
    return out;
}

struct Manifest
{
    std::string head, renameCommit;
    std::vector<std::pair<std::string, std::string>> windows;   // base revision, tree file
    std::vector<std::pair<std::string, Scope>> pathLogs;
};

Manifest loadManifest(const fs::path& file)
{
    Manifest m;
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if (parser.load(file.string()).get(doc) != simdjson::SUCCESS) { std::fprintf(stderr, "cannot parse %s\n", file.string().c_str()); std::exit(2); }
    m.head = text(doc["head"]);
    m.renameCommit = text(doc["renameCommit"]);
    for (simdjson::dom::element w : doc["windows"]) m.windows.emplace_back(text(w["base"]), text(w["listing"]));
    for (simdjson::dom::element p : doc["pathLogs"]) {
        Scope scope;
        for (simdjson::dom::element s : p["scope"]) scope.push_back(text(s));
        m.pathLogs.emplace_back(text(p["name"]), scope);
    }
    return m;
}

/// A source with its window trees registered (the base revisions a baseline listing needs).
std::unique_ptr<FixtureSource> openSource(const fs::path& dir, const Manifest& m, bool shallow)
{
    auto src = std::make_unique<FixtureSource>(dir, shallow);
    if (!shallow) for (const auto& [base, treeFile] : m.windows) src->addTree(base, treeFile);
    return src;
}

/// An event as the truth writes it: label, carry, path, name, detail (a merged block's survivor as "->name", resolved at `at`).
std::vector<std::string> describe(const FixtureSource& src, const Replayed& at, const std::vector<HistoryStep>& steps)
{
    std::vector<std::string> out;
    for (const HistoryStep& s : steps) {
        std::string label = src.labelOf(s.revision);
        if (label.empty()) label = s.revision.substr(0, 8);
        std::string detail = s.event.detail;
        if (s.event.carry == Carry::merged) {
            const Block* survivor = at.index.resolve(s.event.detail);
            detail = "->" + (survivor ? std::string{survivor->name} : std::string("?"));
        }
        out.push_back(std::format("{}|{}|{}|{}|{}", label, carryName(s.event.carry), s.event.path, s.event.name, detail));
    }
    return out;
}

void expectSame(const std::string& what, const std::vector<std::string>& got, const std::vector<std::string>& want)
{
    if (got == want) { CHECK(true); return; }
    std::fprintf(stderr, "%s differs\n  got:\n", what.c_str());
    for (const auto& g : got) std::fprintf(stderr, "    %s\n", g.c_str());
    std::fprintf(stderr, "  want:\n");
    for (const auto& w : want) std::fprintf(stderr, "    %s\n", w.c_str());
    CHECK(false);
}

/// One truth scenario against a replay: each function's history, found by its name at the scenario's revision, equals the hand-checked events.
void checkScenario(const Scenario& sc, const fs::path& dir, const Manifest& m, std::size_t checkpointEvery)
{
    auto src = openSource(dir, m, sc.shallow);
    RevisionIndex::Options options;
    options.maxRevisions = sc.maxRevisions;
    options.checkpointEvery = checkpointEvery;
    RevisionIndex index(*src, options);
    const auto at = index.at(sc.scope, sc.revision);
    CHECK(at.has_value());
    if (!at) return;
    CHECK_EQ((*at)->historyCut, sc.historyCut);
    for (const ExpectedFunction& f : sc.functions) {
        const auto ids = index.named(sc.scope, sc.revision, f.name);
        CHECK(ids.has_value());
        if (!ids) continue;
        CHECK_EQ(ids->size(), std::size_t{1});
        if (ids->size() != 1) continue;
        const auto steps = index.history(sc.scope, sc.revision, ids->front());
        CHECK(steps.has_value());
        if (!steps) continue;
        expectSame(std::format("{} / {} (checkpoints every {})", sc.name, f.name, checkpointEvery), describe(*src, **at, *steps), f.events);
        for (const HistoryStep& s : *steps) CHECK_EQ(s.event.turn, s.ordinal);
    }
}

std::string changeText(const PathChange& c)
{
    return std::format("{}|{}|{}|{}", std::to_underlying(c.kind), c.path, c.from, c.similarity);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_revision_index FIXTURES_DIR\n"); return 2; }
    const fs::path fixtures = argv[1];
    const fs::path dir = fixtures / "git" / "tinycalc";
    const Manifest manifest = loadManifest(dir / "manifest.json");
    const std::vector<Scenario> truth = loadTruth(fixtures / "scenarios" / "g1-tinycalc" / "truth.json");
    CHECK_EQ(truth.size(), std::size_t{7});

    // ---- R-SRC-8: the parsers read what git printed, byte for byte -----------------------------------------------------------------------------------------
    {
        const auto log = gitplumbing::parseLog(fixture::slurp(dir / "log.z"));
        CHECK(log.has_value());
        if (log) {
            CHECK_EQ(log->size(), std::size_t{200});
            CHECK(log->front().info.message.starts_with("c200:"));
            CHECK_EQ(log->front().info.id, manifest.head);
            CHECK_EQ(log->back().info.parents.size(), std::size_t{0});                 // c001: a root
            CHECK_EQ(log->back().changes.size(), std::size_t{9});
            for (const Commit& c : *log) {
                if (c.info.message.starts_with("c110:")) { CHECK_EQ(c.info.parents.size(), std::size_t{2}); CHECK_EQ(c.changes.size(), std::size_t{1}); }   // the merge, its first-parent diff
                if (c.info.message.starts_with("c092:")) {
                    CHECK_EQ(c.changes.size(), std::size_t{1});
                    CHECK(c.changes[0].kind == ChangeKind::renamed && c.changes[0].from == "src/stats.cpp" && c.changes[0].path == "src/statistics.cpp" && c.changes[0].similarity == 100);
                }
                if (c.info.message.starts_with("c057:")) {   // git sees the move of clamp as a copy of util.cpp plus a smaller util.cpp, not as a rename
                    CHECK_EQ(c.changes.size(), std::size_t{2});
                    CHECK(c.changes[0].kind == ChangeKind::copied && c.changes[0].from == "src/util.cpp" && c.changes[0].path == "src/mathx.cpp" && c.changes[0].similarity == 62);
                    CHECK(c.changes[1].kind == ChangeKind::modified && c.changes[1].path == "src/util.cpp");
                }
                if (c.info.message.starts_with("c130:")) CHECK(c.changes.size() == 1 && c.changes[0].kind == ChangeKind::modified && c.changes[0].path == "src/numeric.cpp");
                CHECK_EQ(c.info.author, std::string("Test Author"));
            }
        }
        const auto diff = gitplumbing::parseNameStatus(fixture::slurp(dir / "diff-rename.z"));
        CHECK(diff.has_value() && diff->size() == 1 && (*diff)[0].kind == ChangeKind::renamed && (*diff)[0].path == "src/statistics.cpp");

        const auto tree = gitplumbing::parseTree(fixture::slurp(dir / manifest.windows[0].second));
        CHECK(tree.has_value());
        if (tree) {
            const auto paths = gitplumbing::documentPaths(*tree);
            CHECK_EQ(paths.size(), std::size_t{6});   // geometry, main, mathx, parse, statistics, util at the window's base
            CHECK_EQ(paths.front(), std::string("src/geometry.cpp"));
            CHECK_EQ(tree->front().type, std::string("blob"));
        }

        const auto refs = fixture::nonEmptyLines(fixture::slurp(dir / "blobs.refs"));
        const auto texts = gitplumbing::parseCatFileBatch(fixture::slurp(dir / "blobs.batch"), refs.size());
        CHECK(texts.has_value() && texts->size() == refs.size());
        if (texts) for (const FileText& t : *texts) CHECK(t.presence == Presence::present && !t.text.empty());
        CHECK(!gitplumbing::parseCatFileBatch(fixture::slurp(dir / "blobs.batch"), refs.size() + 1).has_value());   // a request that got no answer
        CHECK(!gitplumbing::parseCatFileBatch(fixture::slurp(dir / "blobs.batch"), refs.size() - 1).has_value());   // an answer nobody asked for

        // The shapes git prints that the captures do not hold, written out: a missing object, a directory, an empty blob, a truncated object.
        const auto odd = gitplumbing::parseCatFileBatch("abc blob 3\nx\ny\nabc123 missing\n0f0f tree 0\n\n0e0e blob 0\n\n", 4);
        CHECK(odd.has_value());
        if (odd) {
            CHECK(((*odd)[0].presence == Presence::present && (*odd)[0].text == "x\ny"));
            CHECK((*odd)[1].presence == Presence::unknown);   // R-SCALE-7: a missing object is unknown, not an error
            CHECK((*odd)[2].presence == Presence::absent);
            CHECK(((*odd)[3].presence == Presence::present && (*odd)[3].text.empty()));
        }
        CHECK(!gitplumbing::parseCatFileBatch("abc blob 9\nshort\n", 1).has_value());
        CHECK(!gitplumbing::parseCatFileBatch("abc blob nine\nshort\n", 1).has_value());
        CHECK(!gitplumbing::parseLog("not a log").has_value());
        CHECK(!gitplumbing::parseLog("\x01" "id\x02p\x02" "a\x02t\x02" "subject\0\nZ\0x\0"sv).has_value());   // an unsupported status
        CHECK(!gitplumbing::parseNameStatus("R100\0only-one-path\0"sv).has_value());
        CHECK(!gitplumbing::parseTree("100644 blob abc no-tab\0"sv).has_value());
        CHECK(gitplumbing::parseLog("").has_value());
        // -z keeps a path byte-exact: a space and a line feed inside it.
        const auto odd2 = gitplumbing::parseLog("\x01" "id\x02" "p1 p2\x02" "a\x02t\x02" "subject: x\0\nM\0we ird\nname.txt\0A\0two\0"sv);
        CHECK(odd2.has_value() && odd2->size() == 1 && (*odd2)[0].changes.size() == 2 && (*odd2)[0].changes[0].path == "we ird\nname.txt" && (*odd2)[0].info.parents.size() == 2);
        CHECK_EQ(gitplumbing::catFileRequests({{"abc", "src/a b.cpp"}, {"def", "x"}}), std::string("abc:src/a b.cpp\ndef:x\n"));
    }

    // ---- the test source's emulation of a pathspec equals git's own path-limited logs (so the scenarios below stand on git's behaviour) --------------------
    {
        FixtureSource src(dir);
        for (const auto& [name, scope] : manifest.pathLogs) {
            const auto git = gitplumbing::parseLog(fixture::slurp(dir / ("log-path-" + name + ".z")));
            CHECK(git.has_value());
            if (!git) continue;
            const History mine = src.history(manifest.head, scope, 0);
            CHECK_EQ(mine.commits.size(), git->size());
            for (std::size_t i = 0; i < std::min(mine.commits.size(), git->size()); ++i) {
                CHECK_EQ(mine.commits[i].info.id, (*git)[i].info.id);
                std::vector<std::string> a, b;
                for (const auto& c : mine.commits[i].changes) a.push_back(changeText(c));
                for (const auto& c : (*git)[i].changes) b.push_back(changeText(c));
                CHECK(a == b);
            }
            const History limited = src.history(manifest.head, scope, 3);   // `limit` is the newest N
            const std::size_t wantLimited = std::min<std::size_t>(3, git->size());
            CHECK_EQ(limited.commits.size(), wantLimited);
        }
    }

    // ---- R-SRC-3: the identity history of the five functions against hand-checked truth, whatever the checkpoint spacing ---------------------------------
    for (const Scenario& sc : truth)
        for (const std::size_t every : {std::size_t{1}, std::size_t{7}, std::size_t{50}, std::size_t{1000}}) checkScenario(sc, dir, manifest, every);

    // ---- R-SCALE-1, R-SCALE-9: cost follows the paths changed; a revision that did not change the scope is never read or replayed -------------------------
    {
        FixtureSource all(dir);
        const auto touches = [&](const Commit& c, const std::set<std::string>& paths) {
            for (const PathChange& ch : c.changes) if (paths.count(ch.path) || (!ch.from.empty() && paths.count(ch.from))) return true;
            return false;
        };
        // One path (renamed at c121 from mathx.cpp): five of the 200 revisions touched it or its old name.
        const Scope numeric{"src/numeric.cpp"};
        FixtureSource src(dir);
        RevisionIndex index(src);
        CHECK(index.at(numeric, "HEAD").has_value());
        std::set<std::string> touching;
        std::size_t wantReads = 0;
        for (const Commit& c : all.commits())
            if (touches(c, {"src/numeric.cpp", "src/mathx.cpp"})) {
                touching.insert(c.info.id);
                for (const PathChange& ch : c.changes) if (ch.kind != ChangeKind::deleted && (ch.path == "src/numeric.cpp" || ch.path == "src/mathx.cpp")) ++wantReads;
            }
        CHECK_EQ(touching.size(), std::size_t{5});
        CHECK_EQ(index.stats().commitsReplayed, touching.size());
        CHECK_EQ(src.reads.size(), wantReads);
        for (const FileRef& r : src.reads) CHECK(touching.count(r.revision) == 1);   // no text of any other revision was requested
        CHECK_EQ(index.stats().filesRead, wantReads);
        CHECK(src.historyCalls <= 3);                                                  // the log is asked for the scope, not walked per revision

        // The directory: updates are exactly the changes under it (a rename retires one path and indexes another).
        FixtureSource srcDir(dir);
        RevisionIndex indexDir(srcDir);
        CHECK(indexDir.at({"src/"}, "HEAD").has_value());
        std::size_t wantUpdates = 0, wantFiles = 0, commits = 0;
        for (const Commit& c : all.commits()) {
            std::size_t here = 0;
            for (const PathChange& ch : c.changes) {
                if (!ch.path.starts_with("src/")) continue;
                here += ch.kind == ChangeKind::renamed ? 2 : 1;
                wantFiles += ch.kind != ChangeKind::deleted;
            }
            wantUpdates += here;
            commits += here > 0;
        }
        CHECK_EQ(indexDir.stats().updates, wantUpdates);
        CHECK_EQ(indexDir.stats().filesRead, wantFiles);
        CHECK_EQ(indexDir.stats().commitsReplayed, commits);
        CHECK(commits < 200);   // the revisions that only touched notes, tests or data were never part of it
    }

    // ---- checkpoints bound a replay: an earlier revision replays from the nearest checkpoint, not from the start ----------------------------------------
    {
        FixtureSource src(dir);
        RevisionIndex::Options options;
        options.checkpointEvery = 10;
        RevisionIndex index(src, options);
        CHECK(index.at({"src/"}, "HEAD").has_value());
        const std::size_t full = index.stats().commitsReplayed;
        CHECK(index.at({"src/"}, "c100").has_value());
        CHECK(index.stats().commitsReplayed - full < 10);          // from the checkpoint at or before it
        const std::size_t afterOnce = index.stats().commitsReplayed;
        CHECK(index.at({"src/"}, "HEAD").has_value());
        CHECK_EQ(index.stats().commitsReplayed, afterOnce);        // the head is the frontier: no replay at all
        CHECK(index.stats().checkpoints >= full / 10);
        // Asking for the same revision twice costs nothing, and an answer stays valid however the index is used afterwards.
        const auto early = index.at({"src/"}, "c040");
        const std::size_t blocks = early ? (*early)->index.all().size() : 0;
        CHECK(index.at({"src/"}, "HEAD").has_value());
        CHECK(index.at({"src/"}, "c150").has_value());
        CHECK_EQ(early ? (*early)->index.all().size() : 1, blocks);
    }

    // ---- R-SRC-3: carry follows an id between revisions; branches are not mixed ---------------------------------------------------------------------------
    {
        FixtureSource src(dir);
        RevisionIndex index(src);
        const Scope scope{"src/"};
        const auto id = [&](const std::string& rev, const std::string& name) {
            const auto ids = index.named(scope, rev, name);
            return ids && ids->size() == 1 ? ids->front() : std::string();
        };
        const std::string average = id("c020", "average"), normalizeNow = id("c200", "normalize"), wrap = id("c150", "wrapAngle"), parse = id("c200", "parseNumber");
        CHECK(!average.empty() && !normalizeNow.empty() && !wrap.empty() && !parse.empty());
        const auto carries = [&](const std::string& from, const std::string& block, const std::string& to, const std::optional<std::string>& want) {
            const auto r = index.carry(scope, from, block, to);
            return r.has_value() && *r == want;
        };
        CHECK_EQ(id("c200", "average"), average);                                                        // renamed from mean at c018, the same block
        CHECK(carries("c020", average, "c200", average));
        CHECK(carries("c200", average, "c020", average));   // looking back
        CHECK(carries("c150", wrap, "c200", normalizeNow));  // merged into normalize at c185
        CHECK(carries("c200", normalizeNow, "c150", normalizeNow));
        CHECK(carries("c030", parse, "c200", std::nullopt));   // retired from c022 to c070: not a block at c030
        CHECK(carries("c010", parse, "c030", std::nullopt));   // retired without a successor by c030
        CHECK(carries("c010", parse, "c020", parse));
        CHECK(!index.carry(scope, "c020", average, "no-such-revision").has_value());                    // a caller's mistake is an error
        CHECK(!index.at(scope, "no-such-revision").has_value());
        // The merge of c110 is one revision on the line; the side branch's own commits are not.
        const auto geometry = index.named({"src/geometry.cpp"}, "c200", "normalize");
        CHECK(geometry.has_value() && geometry->size() == 1);
        if (geometry && geometry->size() == 1) {
            const auto steps = index.history({"src/geometry.cpp"}, "c200", geometry->front());
            CHECK(steps.has_value());
            int atMerge = 0;
            if (steps) for (const HistoryStep& s : *steps) atMerge += src.labelOf(s.revision) == "c110";
            CHECK_EQ(atMerge, 1);
        }
        // Different windows of one scope are different replays: their ids are not comparable and carry says so.
        RevisionIndex::Options window;
        window.maxRevisions = 40;
        RevisionIndex windowed(src, window);
        CHECK(windowed.at(scope, "c200").has_value());
        CHECK(windowed.at(scope, "c150").has_value());   // c150 lies inside the window: the same line
    }

    // ---- followRenames off: a single path begins where it appears -----------------------------------------------------------------------------------------
    {
        FixtureSource src(dir);
        RevisionIndex::Options options;
        options.followRenames = false;
        RevisionIndex index(src, options);
        const Scope numeric{"src/numeric.cpp"};
        const auto ids = index.named(numeric, "c200", "clamp");
        CHECK(ids.has_value() && ids->size() == 1);
        if (ids && ids->size() == 1) {
            const auto steps = index.history(numeric, "c200", ids->front());
            CHECK(steps.has_value() && steps->size() == 1 && src.labelOf(steps->front().revision) == "c121" && steps->front().event.carry == Carry::added);
        }
        CHECK_EQ(src.changesCalls, std::size_t{0});
    }

    // ---- R-SCALE-7: a missing object makes an answer unknown, never an error; the view still renders ------------------------------------------------------
    {
        // A text that is not there: clamp's edit at c083 is missing, and the replay goes on (the later rename carries its identity).
        FixtureSource src(dir);
        const std::string at83 = src.idOfLabel("c083");
        src.dropBlob(at83, "src/mathx.cpp");
        RevisionIndex index(src);
        const Scope scope{"src/"};
        const auto at = index.at(scope, "c200");
        CHECK(at.has_value());
        if (at) {
            CHECK_EQ((*at)->unknown.size(), std::size_t{1});
            if (!(*at)->unknown.empty()) { CHECK_EQ((*at)->unknown[0].revision, at83); CHECK_EQ((*at)->unknown[0].path, std::string("src/mathx.cpp")); }
            const auto ids = index.named(scope, "c200", "clamp");
            CHECK(ids.has_value() && ids->size() == 1);
            if (ids && ids->size() == 1) {
                const auto steps = index.history(scope, "c200", ids->front());
                CHECK(steps.has_value());
                std::vector<std::string> want;
                for (const auto& line : truth[0].functions[0].events) if (!line.starts_with("c083|")) want.push_back(line);
                if (steps) expectSame("clamp with c083's text missing", describe(src, **at, *steps), want);
            }
        }
        // The text of the rename's target is missing (c092): the old file's blocks stay as they were, the unknown is listed, nothing fails.
        FixtureSource src2(dir);
        const std::string at92 = src2.idOfLabel("c092");
        src2.dropBlob(at92, "src/statistics.cpp");
        RevisionIndex index2(src2);
        const auto at2 = index2.at(scope, "c200");
        CHECK(at2.has_value());
        if (at2) {
            CHECK(!(*at2)->unknown.empty() && (*at2)->unknown[0].revision == at92);
            const auto ids = index2.named(scope, "c200", "average");
            CHECK(ids.has_value() && !ids->empty());
        }
        // A window whose base tree is not available: the baseline is unknown (an empty path says the whole tree), the window replays on.
        auto src3 = openSource(dir, manifest, false);
        src3->dropTrees();
        RevisionIndex::Options options;
        options.maxRevisions = 40;
        RevisionIndex index3(*src3, options);
        const auto at3 = index3.at(scope, "c200");
        CHECK(at3.has_value());
        if (at3) {
            CHECK(!(*at3)->unknown.empty() && (*at3)->unknown[0].path.empty() && (*at3)->unknown[0].revision == manifest.windows[0].first);
            CHECK(!(*at3)->index.all().empty());
        }
    }

    {   // A client-defined provider survives checkpoint copies and reaches the same native / binary block backend.
        struct Provider final : OutlineProvider
        {
            std::string name() const override { return "sections@1"; }
            Outline outline(std::string_view text) const override
            {
                if (!text.starts_with("group outer\nsection inner\n")) return {};
                OutlineNode child{"section", "inner", "outer::inner", 2, 3, 2, 2, 3, "section inner", {}};
                OutlineNode parent{"group", "outer", "outer", 1, 4, 1, 1, 4, "group outer", {child}};
                return {"", name(), {parent}, false};
            }
        };
        Canvas canvas;
        canvas.addFile("notes.section", "group outer\nsection inner\noriginal\nend\n");
        CHECK(canvas.replaceText("notes.section", "group outer\nsection inner\nupdated\nend\n", "test").ok);
        fixture::CanvasSource source(canvas, {{{"notes.section", 1}}, {{"notes.section", 2}}});
        auto registry = std::make_shared<Languages>(defaultLanguages());
        registry->detects("sections", {".section"});
        registry->add("sections", std::make_shared<Provider>(), 0);
        RevisionIndex::Options options;
        options.checkpointEvery = 1;
        options.languages = registry;
        RevisionIndex index(source, options);
        const Scope scope{"notes.section"};
        const auto head = index.at(scope, "HEAD");
        const auto base = index.at(scope, "r0");
        CHECK(head && base && (*head)->index.liveCount() == 2 && (*base)->index.liveCount() == 2);
        const Block* child = (*head)->index.at("notes.section", 3);
        CHECK(child && child->name == "outer::inner" && child->first == 2 && child->last == 3);
        CHECK((*base)->index.at("notes.section", 3)->id == child->id);
        const auto bytes = writeBlockTable((*head)->revision, **head);
        CHECK(bytes);
        const auto table = readBlockTable(*bytes);
        CHECK(table && table->blocks.size() == 2 && table->blocks[1].id == child->id);
    }

    // ---- R-SRC-5: revision ids are opaque strings; a canvas's revisions are one kind among others (the second DocumentSource) -----------------------------
    {
        // The persisted attempt of the mock squad's g4 scenario (fixtures/mock/g4-rename-resign-move): a canvas whose files went through five calc revisions and a
        // second file. Rebuilt here from its persisted texts, in the order its turn files record (turn 3 holds calc r4 and geometry r1 together).
        const fs::path attempt = fixtures / "mock" / "g4-rename-resign-move" / "canvas";
        Canvas canvas;
        canvas.addFile("src/calc.cpp", fixture::slurp(attempt / sub0canvas::revisionFileName("src/calc.cpp", 1)));
        for (int r = 2; r <= 5; ++r) CHECK(canvas.replaceText("src/calc.cpp", fixture::slurp(attempt / sub0canvas::revisionFileName("src/calc.cpp", r)), "attempt").ok);
        canvas.addFile("src/geometry.cpp", fixture::slurp(attempt / sub0canvas::revisionFileName("src/geometry.cpp", 1)));
        fixture::CanvasSource source(canvas, {{{"src/calc.cpp", 1}}, {{"src/calc.cpp", 2}}, {{"src/calc.cpp", 3}}, {{"src/calc.cpp", 4}, {"src/geometry.cpp", 1}}, {{"src/calc.cpp", 5}}});
        RevisionIndex index(source);
        const Scope scope{"src/"};
        const auto head = index.at(scope, "HEAD");
        CHECK(head.has_value());
        const auto events = [&](const std::string& name) {
            const auto ids = index.named(scope, "HEAD", name);
            std::vector<std::string> out;
            if (!ids || ids->size() != 1) { out.push_back("not found"); return out; }
            const auto steps = index.history(scope, "HEAD", ids->front());
            for (const HistoryStep& s : *steps)
                out.push_back(std::format("{}|{}|{}|{}|{}", s.revision, carryName(s.event.carry), s.event.path, s.event.name, s.event.detail));
            return out;
        };
        // Hand-written from the scenario (fixtures/scenarios/squad/g4-rename-resign-move.squad: `id @1 safeDivide = d`, `id @2 area = a`, `id @3 src/geometry.cpp
        // square = s`, `gone @3 src/calc.cpp square`) and its turn files: divide is renamed at the first reply, area re-signed at the second, square moved at the third.
        expectSame("g4 safeDivide", events("safeDivide"), {"r0|added|src/calc.cpp|divide|", "r1|renamed|src/calc.cpp|safeDivide|divide"});
        expectSame("g4 area", events("area"),
                   {"r0|added|src/calc.cpp|area|", "r2|resigned|src/calc.cpp|area|double area(double r)", "r4|kept|src/calc.cpp|area|edited"});
        expectSame("g4 square", events("square"),
                   {"r0|added|src/calc.cpp|square|", "r3|retired|src/calc.cpp|square|", "r3|revived|src/geometry.cpp|square|src/calc.cpp"});
        // The same questions work through the same interface: carry, an earlier revision, the merge-free line.
        const auto square = index.named(scope, "HEAD", "square");
        CHECK(square.has_value() && square->size() == 1);
        if (square && square->size() == 1) {
            { const auto r = index.carry(scope, "r0", square->front(), "HEAD"); CHECK(r.has_value() && r->has_value() && **r == square->front()); }
            const auto early = index.at(scope, "r1");
            CHECK(early.has_value() && (*early)->index.blocks("src/geometry.cpp").empty());   // not on the canvas yet
        }
        CHECK_EQ((*head)->index.blocks("src/geometry.cpp").size(), std::size_t{1});
    }

    return finish();
}
