// Golden replay (docs/TESTING.md, L1; docs/REQUIREMENTS.md, R-U1-1 to R-U1-4): every attempt Sub0Squad really produced (fixtures/golden) is fed back
// through the library the way the squad's canvas loop feeds it, turn by turn, and what comes out must AGREE with what the squad recorded:
//   * the same blocks per file (name and lines),
//   * identities that correspond one to one: an id the squad recorded at two turns maps to one id here at both (ids are compared, never spelled),
//   * the same identity history per turn (carry and which block),
//   * the same issues: keys in order, kinds, and each anchor on the block of the same name with the same confidence (or the same loose line).
// This is the U1 use case end to end on real data, and it does not care how the library stores anything: a store fed by events (B1), a compact index
// (S1) or other ids must still agree. When the producer's output changes ON PURPOSE, re-capture the fixtures (fixtures/golden/README.md).
//
// The verdict of each turn is rebuilt from the recorded items' `detail` (the gate's own FAIL lines); the golden set holds only single-line test
// failures, which the check below asserts so that a future capture with multi-place diagnostics is noticed rather than half-checked.
#include "sub0platform/testing/check.hpp"
#include "doc_driver.hpp"
#include "test_support.hpp"

#include <simdjson.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace req;
using testsupport::slurp;

namespace {

std::string str(simdjson::dom::element e, std::string_view key)
{
    std::string_view v;
    if (e[key].get_string().get(v)) return {};
    return std::string{v};
}
int num(simdjson::dom::element e, std::string_view key)
{
    int64_t v = 0;
    if (e[key].get_int64().get(v)) return 0;
    return static_cast<int>(v);
}

const char* confidenceName(Anchor::Confidence c)
{
    switch (c) {
        case Anchor::Confidence::exact: return "exact";
        case Anchor::Confidence::suspect: return "suspect";
        case Anchor::Confidence::none: return "none";
    }
    return "?";
}

/// The recorded-to-replayed identity correspondence: must stay one to one across the whole attempt.
struct IdMap
{
    std::map<std::string, std::string> there, back;
    bool bind(const std::string& recorded, const std::string& replayed)
    {
        const auto a = there.find(recorded), b = back.find(replayed);
        if (a != there.end() && a->second != replayed) return false;
        if (b != back.end() && b->second != recorded) return false;
        there[recorded] = replayed;
        back[replayed] = recorded;
        return true;
    }
};

int replayAttempt(const fs::path& dir)
{
    const std::string name = dir.filename().string();
    std::vector<std::pair<int, fs::path>> turns;
    for (const auto& e : fs::directory_iterator(dir / "layers"))
        if (e.path().extension() == ".json") turns.push_back({std::stoi(e.path().stem().string().substr(5)), e.path()});
    std::ranges::sort(turns);
    CHECK(!turns.empty());

    Doc d;
    IdMap ids;
    simdjson::dom::parser parser;
    int checkedItems = 0;
    for (const auto& [turnNo, file] : turns) {
        const std::string json = slurp(file);
        simdjson::dom::element t;
        if (parser.parse(json).get(t)) { std::fprintf(stderr, "%s: %s does not parse\n", name.c_str(), file.filename().string().c_str()); CHECK(false); continue; }
        const std::string where = name + " turn " + std::to_string(turnNo);

        // Feed the turn as the canvas loop does: the turn first, then every file's text.
        d.turn(turnNo);
        simdjson::dom::object files;
        CHECK(!t["files"].get_object().get(files));
        for (auto [path, f] : files) d.write(std::string{path}, str(f, "text"));

        // R-U1-1: the same blocks, by name and lines; R-U1-2: identities correspond one to one.
        std::map<std::string, std::string> recordedName;   // recorded id -> name
        simdjson::dom::array blocks;
        CHECK(!t["blocks"].get_array().get(blocks));
        std::map<std::string, std::vector<std::pair<std::string, std::pair<int, int>>>> recordedByPath;
        for (auto b : blocks) {
            recordedName[str(b, "id")] = str(b, "name");
            recordedByPath[str(b, "path")].push_back({str(b, "name"), {num(b, "first"), num(b, "last")}});
            const std::string replayed = d.id(str(b, "path"), leafOf(str(b, "name")));
            if (replayed.empty()) { std::fprintf(stderr, "%s: block %s is missing\n", where.c_str(), str(b, "name").c_str()); CHECK(false); continue; }
            if (!ids.bind(str(b, "id"), replayed)) { std::fprintf(stderr, "%s: block %s does not keep one identity\n", where.c_str(), str(b, "name").c_str()); CHECK(false); }
        }
        for (const auto& [path, want] : recordedByPath) {
            std::vector<std::pair<std::string, std::pair<int, int>>> got;
            for (const auto& s : d.blocks(path)) got.push_back({s.name, {s.first, s.last}});
            if (got != want) { std::fprintf(stderr, "%s: the blocks of %s differ\n", where.c_str(), path.c_str()); CHECK(false); }
        }

        // R-U1-3: the identity history of this turn (whether the file holds per-turn or cumulative events, the turn's own are compared).
        std::vector<std::pair<std::string, std::string>> wantEvents, gotEvents;
        simdjson::dom::array events;
        if (!t["events"].get_array().get(events))
            for (auto e : events)
                if (num(e, "turn") == turnNo) wantEvents.push_back({str(e, "carry"), str(e, "id")});
        for (const auto& e : d.historyOfTurn(turnNo)) {
            const auto back = ids.back.find(e.id);
            gotEvents.push_back({e.carry, back == ids.back.end() ? "(unmapped " + e.id + ")" : back->second});
        }
        if (gotEvents != wantEvents) {
            std::fprintf(stderr, "%s: the identity history differs (%zu recorded, %zu replayed)\n", where.c_str(), wantEvents.size(), gotEvents.size());
            CHECK(false);
        }

        // R-U1-4: the same issues from the same verdict.
        simdjson::dom::array recorded;
        if (t["layers"]["issues"].get_array().get(recorded)) continue;
        std::string verdict;
        std::size_t count = 0;
        for (auto it : recorded) {
            const std::string detail = str(it, "detail");
            CHECK(str(it, "kind") == "test" && !detail.contains('\n'));   // the rebuild below is exact only for these (see the header)
            verdict += detail + "\n";
            ++count;
        }
        const Items got = d.issues(verdict);
        if (got.size() != count) { std::fprintf(stderr, "%s: %zu issues recorded, %zu replayed\n", where.c_str(), count, got.size()); CHECK(false); continue; }
        std::size_t i = 0;
        for (auto it : recorded) {
            const LayerItem& g = got[i++];
            const std::string key = str(it, "id").empty() ? str(it, "key") : str(it, "id");
            if (g.key != key || g.kind != str(it, "kind")) { std::fprintf(stderr, "%s: issue %zu is %s, recorded %s\n", where.c_str(), i, g.key.c_str(), key.c_str()); CHECK(false); continue; }
            simdjson::dom::array anchors;
            CHECK(!it["anchors"].get_array().get(anchors));
            std::size_t k = 0;
            for (auto a : anchors) {
                if (k >= g.anchors.size()) { ++k; continue; }
                const Anchor& ga = g.anchors[k++];
                const std::string rid = str(a, "blockId");
                bool same = str(a, "confidence") == confidenceName(ga.confidence);
                if (!rid.empty()) {
                    const auto mapped = ids.there.find(rid);
                    same = same && mapped != ids.there.end() && mapped->second == ga.blockId;
                } else {
                    same = same && ga.blockId.empty() && ga.line == num(a, "line");
                }
                if (!same) { std::fprintf(stderr, "%s: %s anchor %zu lands elsewhere (recorded %s)\n", where.c_str(), key.c_str(), k, rid.empty() ? "a loose line" : recordedName[rid].c_str()); CHECK(false); }
            }
            if (k != g.anchors.size()) { std::fprintf(stderr, "%s: %s has %zu anchors, recorded %zu\n", where.c_str(), key.c_str(), g.anchors.size(), k); CHECK(false); }
            ++checkedItems;
        }
    }
    return checkedItems;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_golden_replay <fixtures/golden>\n"); return 2; }
    int attempts = 0, items = 0;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_directory() && fs::exists(e.path() / "layers")) { ++attempts; items += replayAttempt(e.path()); }
    CHECK_GE(attempts, 3);    // the golden set (fixtures/golden/README.md)
    CHECK_GE(items, 27);      // every recorded issue of every turn was compared
    std::printf("golden replay: %d attempts, %d issues compared\n", attempts, items);
    return finish();
}
