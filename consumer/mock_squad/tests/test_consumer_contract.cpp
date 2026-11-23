// The consumer contract (docs/MOCK_SQUAD.md section 3): Sub0Canvas as the squad would meet it. This executable is built by the consumer project, which fetches the
// library through CPM and links only its public target, so what compiles here is what a consumer can rely on. It checks, for every scenario of
// fixtures/scenarios/squad: (c) the outcome of each reply, the final files, the named refusal texts and the identity expectations, all written by hand in the
// scenario; (d) that the persisted attempt is byte-identical to fixtures/mock/<name>; (e) the invariants of every turn. Then (b) that every row of REQUIREMENTS.md
// "Sub0Squad's use, mapped" is exercised by a scenario or explicitly deferred, and (f) that the library's version is the release's.
//
// argv: <fixtures/scenarios/squad> <fixtures/mock> <docs/REQUIREMENTS.md> <package.json>
#include "mock_squad/loop.hpp"
#include "mock_squad/record.hpp"
#include "mock_squad/scenario.hpp"
#include "sub0platform/testing/check.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <string>
#include <vector>

// (a) The project builds through CPM against public headers only, under the consumer's own -Werror: both are facts of compilation, asserted here so that
// loosening either breaks this file rather than passing silently.
#ifndef MOCK_SQUAD_WERROR
#error "the consumer project must compile with -Werror (consumer/mock_squad/CMakeLists.txt)"
#endif
#if __has_include("json_util.hpp")
#error "the consumer sees a library-private header: only public headers may be on its include path"
#endif

namespace fs = std::filesystem;
using namespace mock_squad;

namespace {

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::vector<std::string> wordsOf(const std::string& s)
{
    std::vector<std::string> v;
    std::istringstream in(s);
    for (std::string w; in >> w;) v.push_back(w);
    return v;
}

/// "" when the outcome of every reply is the one the scenario's `# expect` names (its kind, and the text it says the refusal or detail contains).
std::string checkOutcomes(const Scenario& sc, const Attempt& a)
{
    if (a.outcomes.size() != sc.expect.size()) return "expected " + std::to_string(sc.expect.size()) + " outcome(s), the loop produced " + std::to_string(a.outcomes.size());
    for (std::size_t i = 0; i < sc.expect.size(); ++i) {
        const Outcome& o = a.outcomes[i];
        const ExpectedOutcome& e = sc.expect[i];
        if (o.kind != e.kind) return "reply " + std::to_string(i) + ": expected '" + e.kind + "', got '" + o.kind + "' (" + o.text + ")";
        if ((o.text + "\n" + o.verdict).find(e.needle) == std::string::npos) return "reply " + std::to_string(i) + " (" + o.kind + "): neither '" + o.text + "' nor its verdict contains '" + e.needle + "'";
    }
    if (a.unusedReplies != 0) return std::to_string(a.unusedReplies) + " scripted reply(ies) were never sent";
    return {};
}

/// "" when the final files are exactly the `--- want` ones: each equal to its text, and no file beside them and the base files.
std::string checkFiles(const Scenario& sc, const Attempt& a)
{
    for (const auto& [path, want] : sc.want) {
        const auto it = a.files.find(path);
        if (it == a.files.end()) return "'" + path + "' was not written";
        if (it->second != want) return "'" + path + "' differs from the intended text:\n" + it->second;
    }
    for (const auto& [path, text] : a.files)
        if (!sc.want.count(path) && !sc.base.count(path)) return "'" + path + "' exists, but neither base nor want names it";
    return {};
}

/// "" when the identity expectations hold. Lines (turn N = after N replies were handled, the numbering of `layers/turn-N.json`):
///   `id @N <path> <leaf> = <label>`   blocks that share a label share an identity, and blocks with different labels differ: ids are compared, never spelled
///   `gone @N <path> <leaf>`           no live block of that path has that last name
///   `issue @N <number> = <label>`     the same rule for an issue's key
///   `retitled <label>`                the issue labelled so is listed under at least two different titles
///   `anchor @N <number> <path> <leaf>`  the issue's first anchor lies in that block
std::string checkIds(const Scenario& sc, const Attempt& a)
{
    std::map<std::string, std::string> idOf, keyOf;
    std::map<std::string, std::set<std::string>> titles;
    const auto turn = [&](const std::string& at) -> const LayerTurn* {
        if (at.size() < 2 || at[0] != '@') return nullptr;
        const auto it = a.layers.find(std::atoi(at.c_str() + 1));
        return it == a.layers.end() ? nullptr : &it->second;
    };
    const auto bind = [&](std::map<std::string, std::string>& m, const std::string& label, const std::string& value) {
        const auto [it, fresh] = m.emplace(label, value);
        return fresh || it->second == value;
    };
    for (const auto& line : sc.expectIds) {
        const auto w = wordsOf(line);
        if (w.size() == 2 && w[0] == "retitled") continue;
        if (w.size() < 3) return "not an identity expectation: " + line;
        const LayerTurn* t = turn(w[1]);
        if (!t) return "no layer file for " + w[1] + " in: " + line;
        if (w[0] == "id" && w.size() == 6 && w[4] == "=") {
            const auto b = std::find_if(t->blocks.begin(), t->blocks.end(), [&](const IdAt& x) { return x.path == w[2] && x.leaf == w[3]; });
            if (b == t->blocks.end()) return "no block '" + w[3] + "' in " + w[2] + " at " + w[1];
            if (!bind(idOf, w[5], b->id)) return "'" + w[3] + "' at " + w[1] + " should keep the identity labelled " + w[5] + " but has another";
        } else if (w[0] == "gone" && w.size() == 4) {
            for (const auto& b : t->blocks)
                if (b.path == w[2] && b.leaf == w[3]) return "'" + w[3] + "' is still a live block of " + w[2] + " at " + w[1];
        } else if (w[0] == "issue" && w.size() == 5 && w[3] == "=") {
            const std::size_t n = static_cast<std::size_t>(std::atoi(w[2].c_str()));
            if (n < 1 || n > t->issues.size()) return "no issue " + w[2] + " at " + w[1];
            const IssueAt& it = t->issues[n - 1];
            if (!bind(keyOf, w[4], it.key)) return "issue " + w[2] + " at " + w[1] + " should keep the key labelled " + w[4] + " but has another";
            titles[w[4]].insert(it.title);
        } else if (w[0] == "anchor" && w.size() == 5) {
            const std::size_t n = static_cast<std::size_t>(std::atoi(w[2].c_str()));
            if (n < 1 || n > t->issues.size() || t->issues[n - 1].anchors.empty()) return "issue " + w[2] + " at " + w[1] + " has no anchor";
            if (t->issues[n - 1].anchors[0] != w[3] + " " + w[4]) return "issue " + w[2] + " at " + w[1] + " is anchored at '" + t->issues[n - 1].anchors[0] + "', expected '" + w[3] + " " + w[4] + "'";
        } else {
            return "not an identity expectation: " + line;
        }
    }
    for (const auto& line : sc.expectIds) {
        const auto w = wordsOf(line);
        if (w.size() == 2 && w[0] == "retitled" && titles[w[1]].size() < 2) return "the issue labelled " + w[1] + " never changed its title";
    }
    for (const auto* m : {&idOf, &keyOf}) {
        std::map<std::string, std::string> byValue;   // two labels on one value would mean a merge the scenario did not expect
        for (const auto& [label, value] : *m)
            if (!byValue.emplace(value, label).second) return "labels '" + byValue[value] + "' and '" + label + "' are one identity, but the scenario names them apart";
    }
    return {};
}

/// "" when the persisted attempt equals the files under `dir`, both ways.
std::string checkRecord(const Attempt& a, const fs::path& dir)
{
    const auto stored = readTree(dir);
    if (stored.empty()) return "fixtures/mock has no attempt for it: run mock_squad --update and review the files";
    for (const auto& [rel, bytes] : a.record) {
        const auto it = stored.find(rel);
        if (it == stored.end()) return "'" + rel + "' is not in the stored attempt";
        if (it->second != bytes) return "'" + rel + "' differs from the stored attempt";
    }
    for (const auto& [rel, bytes] : stored)
        if (!a.record.count(rel)) return "'" + rel + "' is stored but the loop no longer writes it";
    return {};
}

struct Version { int major = -1, minor = 0, patch = 0; };
Version parseVersion(const std::string& s)
{
    Version v;
    const char* p = s.c_str() + (!s.empty() && s[0] == 'v' ? 1 : 0);
    if (std::sscanf(p, "%d.%d.%d", &v.major, &v.minor, &v.patch) != 3) v.major = -1;
    return v;
}
bool operator<(const Version& a, const Version& b) { return std::tie(a.major, a.minor, a.patch) < std::tie(b.major, b.minor, b.patch); }
bool operator==(const Version& a, const Version& b) { return std::tie(a.major, a.minor, a.patch) == std::tie(b.major, b.minor, b.patch); }

/// One row of REQUIREMENTS.md "Sub0Squad's use, mapped", with the library calls the loop must make for it and why anything is not exercised yet. A row of the
/// document that is not here, a row here that the document lost, or a row with neither calls nor a reason, fails the test.
struct Row
{
    std::string key;                      ///< a part of the row's first cell
    std::vector<std::string> called;      ///< library functions some scenario must make the loop call
    std::string deferred;                 ///< what the baseline does not exercise, and where it will be (required when `called` is empty)
};
const std::vector<Row> kRows = {
    {"`canvas_agent.cpp`: the layered verdict", {"parseIssues", "layerSummary", "layerInline", "layerFocus", "layeredJson", "BlockIndex::update"}, ""},
    {"`canvas_agent.cpp`: READ ISSUE answered", {"layerDetail", "parseIssueReads"}, ""},
    {"reads (`parseReadRequests`", {"answerRead"}, "parseReadRequests: the loop reads through parseCanvasReply, which finds the READ blocks itself; llm_agent's own path is not mocked"},
    {"`parseCanvasReply`, `applyEdits`", {"parseCanvasReply", "Canvas::apply", "Canvas::revertTo", "Canvas::logJson"},
     "applyEdits and replaceText are reached through applyReply, never called by the loop; textAt has no loop use yet (fork.cpp compares revisions: MS4)"},
    {"a parsed reply applied all or nothing", {"applyReply"}, ""},
    {"`joinContinuation`", {"joinContinuation", "continuationPrompt", "detectRunaway", "detectEditOverrun", "cutOffInsideEdit", "completeCutFile"},
     "continuationText belongs to llm_agent's prefill path, which the loop does not mirror (MOCK_SQUAD.md section 6)"},
    {"`expandSkeleton`", {}, "the fork and contract paths are not part of the canvas loop; the fork executor is MS4"},
    {"`foldedView`", {}, "the folded view the next message carries is not built by the MS0 loop (it would be asserted only by the writer's own bytes)"},
    {"19 calls of `isPythonPath`", {"isPythonPath"}, ""},
    {"the adaptive threshold", {}, "squad policy, not library behaviour: it is a field of LoopPolicy (issueMode, adaptiveIssues), not a row to cover"},
};

/// The first cells of the rows of the section "Sub0Squad's use, mapped".
std::vector<std::string> mappedRows(const std::string& requirements)
{
    std::vector<std::string> cells;
    std::istringstream in(requirements);
    bool inSection = false;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("## ", 0) == 0) inSection = line.find("Sub0Squad's use, mapped") != std::string::npos;
        if (!inSection || line.rfind("| ", 0) != 0 || line.rfind("| Sub0Squad use", 0) == 0 || line.rfind("|---", 0) == 0) continue;
        const auto end = line.find(" |", 2);
        cells.push_back(line.substr(2, end == std::string::npos ? std::string::npos : end - 2));
    }
    return cells;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: test_consumer_contract <fixtures/scenarios/squad> <fixtures/mock> <docs/REQUIREMENTS.md> <package.json>\n");
        return 2;
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.path().extension() == ".squad") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    CHECK_GE(files.size(), static_cast<std::size_t>(10));   // G1, G2, G4, G6, B1, B3, B8, B9 (two), B12

    std::set<std::string> libraryCalls;
    std::map<std::string, Attempt> attempts;
    for (const auto& f : files) {
        const auto sc = parseScenario(slurp(f));
        if (!sc) { std::fprintf(stderr, "%s: unreadable scenario: %s\n", f.filename().string().c_str(), sc.error().c_str()); CHECK(false); continue; }
        CHECK(sc->name == f.stem().string());
        const auto attempt = runScenario(*sc);
        if (!attempt) { std::fprintf(stderr, "%s: %s\n", f.filename().string().c_str(), attempt.error().c_str()); CHECK(false); continue; }
        const auto report = [&](const char* what, const std::string& why) {
            if (why.empty()) return;
            std::fprintf(stderr, "%s: %s: %s\n", sc->name.c_str(), what, why.c_str());
            CHECK(false);
        };
        report("outcomes", checkOutcomes(*sc, *attempt));   // (c)
        report("files", checkFiles(*sc, *attempt));
        report("identities", checkIds(*sc, *attempt));
        report("stored attempt", checkRecord(*attempt, fs::path(argv[2]) / sc->name));   // (d)
        for (const auto& v : attempt->violations) report("invariant", v);               // (e) a refusal that changed the canvas, an anchor that does not resolve
        const auto again = runScenario(*sc);                                            // (e) the same scenario twice gives the same bytes
        CHECK(again && again->record == attempt->record);
        libraryCalls.insert(attempt->libraryCalls.begin(), attempt->libraryCalls.end());
        attempts[sc->name] = *attempt;
    }
    {   // The stored fixtures are exactly the scenarios': a directory left behind by a deleted scenario is a stale file.
        std::set<std::string> names;
        for (const auto& f : files) names.insert(f.stem().string());
        for (const auto& e : fs::directory_iterator(argv[2]))
            if (e.is_directory() && !names.count(e.path().filename().string())) { std::fprintf(stderr, "fixtures/mock/%s has no scenario\n", e.path().filename().string().c_str()); CHECK(false); }
    }
    {   // Behaviour the scenarios were written to show: each library rule that matters to the squad has at least one scenario that reaches it.
        CHECK(attempts.count("b9-file-and-bad-edit-atomic") && attempts["b9-file-and-bad-edit-atomic"].refused == 1);
        CHECK(attempts.count("b3-loop-rolled-back") && attempts["b3-loop-rolled-back"].aborted == 1);
        CHECK(attempts.count("g6-worse-is-reverted") && attempts["g6-worse-is-reverted"].reverted == 1);
        CHECK(attempts.count("g4-rename-resign-move") && attempts["g4-rename-resign-move"].staged == 3);
    }
    {   // (b) every row of REQUIREMENTS.md "Sub0Squad's use, mapped" is exercised by a scenario or deferred with its reason.
        const auto cells = mappedRows(slurp(argv[3]));
        CHECK_GE(cells.size(), static_cast<std::size_t>(10));
        for (const auto& cell : cells) {
            const auto row = std::find_if(kRows.begin(), kRows.end(), [&](const Row& r) { return cell.find(r.key) != std::string::npos; });
            if (row == kRows.end()) { std::fprintf(stderr, "REQUIREMENTS.md row not covered or deferred in the consumer contract: %s\n", cell.c_str()); CHECK(false); }
        }
        for (const auto& row : kRows) {
            const bool inDocument = std::any_of(cells.begin(), cells.end(), [&](const std::string& c) { return c.find(row.key) != std::string::npos; });
            if (!inDocument) { std::fprintf(stderr, "the consumer contract lists a row REQUIREMENTS.md no longer has: %s\n", row.key.c_str()); CHECK(false); }
            if (row.called.empty() && row.deferred.empty()) { std::fprintf(stderr, "row '%s' is neither exercised nor deferred with a reason\n", row.key.c_str()); CHECK(false); }
            for (const auto& fn : row.called)
                if (!libraryCalls.count(fn)) { std::fprintf(stderr, "row '%s': no scenario makes the loop call %s\n", row.key.c_str(), fn.c_str()); CHECK(false); }
        }
    }
    {   // The checkers themselves can fail: a wrong expectation is reported (a test that has never failed proves little, docs/TESTING.md).
        const auto sc = parseScenario(slurp(fs::path(argv[1]) / "g4-rename-resign-move.squad"));
        CHECK(sc.has_value());
        if (sc && attempts.count(sc->name)) {
            const Attempt& a = attempts[sc->name];
            Scenario wrong = *sc;
            wrong.expect[0].kind = "passed";
            CHECK(!checkOutcomes(wrong, a).empty());
            wrong = *sc;
            wrong.want["src/calc.cpp"] += "// extra\n";
            CHECK(!checkFiles(wrong, a).empty());
            wrong = *sc;
            wrong.expectIds = {"id @1 src/calc.cpp safeDivide = d", "id @1 src/calc.cpp square = d"};   // two different blocks under one label
            CHECK(!checkIds(wrong, a).empty());
            wrong = *sc;
            wrong.expectIds = {"id @1 src/calc.cpp safeDivide = d", "id @0 src/calc.cpp divide = other"};   // the renamed block must be the same identity as divide was
            CHECK(!checkIds(wrong, a).empty());
            Attempt altered = a;
            altered.record["canvas.json"] += " ";
            CHECK(!checkRecord(altered, fs::path(argv[2]) / sc->name).empty());
        }
    }
    {   // (f) the library says the release it is: the same as package.json, and not older than the newest tag of this checkout (v0.3.0 shipped saying 0.2.1).
        const Version lib = parseVersion(MOCK_SQUAD_LIBRARY_VERSION);
        CHECK(lib.major >= 0);
        const std::string pkg = slurp(argv[4]);
        const auto at = pkg.find("\"version\"");
        const auto q1 = at == std::string::npos ? at : pkg.find('"', pkg.find(':', at));
        const auto q2 = q1 == std::string::npos ? q1 : pkg.find('"', q1 + 1);
        const Version npm = q2 == std::string::npos ? Version{} : parseVersion(pkg.substr(q1 + 1, q2 - q1 - 1));
        if (!(lib == npm)) { std::fprintf(stderr, "CMakeLists.txt says %s, package.json says another version\n", MOCK_SQUAD_LIBRARY_VERSION); CHECK(false); }
        const std::string tag = MOCK_SQUAD_LATEST_TAG;
        if (!tag.empty()) {
            const Version released = parseVersion(tag);
            if (released.major >= 0 && lib < released) { std::fprintf(stderr, "the library says %s but %s is already tagged\n", MOCK_SQUAD_LIBRARY_VERSION, tag.c_str()); CHECK(false); }
        }
    }
    return finish();
}
