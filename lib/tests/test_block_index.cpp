// The compact, lazy block index (S1, docs/SCALE.md sections 3-4; docs/BACKBONE.md A16): ids are deterministic birth ids; a lazy index (a TextSource, no held
// text) decides exactly what the retaining one decides; a revive probe compares only the retired blocks that can reach the threshold; the per-blob analysis is
// reused. The identity RULES are checked by test_doc_layers, test_golden_replay and test_corpus; this checks what S1 added.
#include "sub0platform/testing/check.hpp"
#include "bench/corpus.hpp"
#include "sub0canvas/doc_layers.hpp"

#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace sub0canvas;
using namespace sub0canvas::bench;

namespace {

/// Every text the stream ever produced, by content id: the document history a source (git) would serve, so an OLD text is available too.
struct History final : TextSource
{
    std::map<std::uint64_t, std::string> byId;
    std::size_t asked = 0;
    void add(const std::string& text) { byId.emplace(BlockIndex::textId(text), text); }
    [[nodiscard]] std::optional<std::string_view> text(std::string_view, std::uint64_t id) const override
    {
        const auto it = byId.find(id);
        if (it == byId.end()) return std::nullopt;
        return std::string_view{it->second};
    }
};

std::string describe(const EventLog& log)
{
    std::string s;
    for (const BlockEvent& e : log) s += std::to_string(e.turn) + ' ' + std::to_string(std::to_underlying(e.carry)) + ' ' + e.id + ' ' + e.path + ' ' + e.name + ' ' + e.detail + '\n';
    return s;
}

std::string blocksOf(const BlockIndex& index)
{
    std::string s;
    for (const Block* b : index.all()) s += b->id + ' ' + std::string{b->path} + ' ' + std::string{b->name} + ' ' + std::string{b->signature} + ' ' + std::to_string(b->first) + '-' + std::to_string(b->last) + '\n';
    return s;
}

}  // namespace

int main()
{
    CorpusSpec spec;
    spec.seed = 5;
    spec.targetLines = 8'000;
    const auto corpus = generateCorpus(spec);
    CHECK(corpus.has_value());
    if (!corpus) return finish();

    // The stream, applied three ways: retaining index, retaining index again (determinism), lazy index over a history source.
    BlockIndex retaining, again, lazy;
    History history;
    lazy.setTextSource(&history);
    for (const auto& [path, text] : corpus->files) history.add(text);
    EditStream stream(*corpus, 21);
    for (const auto& [path, text] : corpus->files) { retaining.update(path, text); again.update(path, text); lazy.update(path, text); }
    std::vector<Commit> commits;
    for (int i = 0; i < 150; ++i) commits.push_back(stream.next());
    for (const Commit& c : commits) for (const FileChange& ch : c.changes) if (ch.text) history.add(*ch.text);
    history.add(std::string());
    for (const auto& [i, commit] : std::views::enumerate(commits)) {
        for (BlockIndex* x : {&retaining, &again, &lazy}) x->setTurn(static_cast<int>(i) + 1);
        for (const FileChange& ch : commit.changes)
            for (BlockIndex* x : {&retaining, &again, &lazy}) x->update(ch.path, ch.text ? *ch.text : std::string());
    }
    CHECK(retaining.events().size() > 1000);

    // A16: the same history gives the same ids (no counter, no coordination); the id is opaque but fixed in form.
    CHECK_EQ(describe(retaining.events()), describe(again.events()));
    CHECK_EQ(blocksOf(retaining), blocksOf(again));
    for (const Block* b : retaining.all()) { CHECK_EQ(b->id.size(), std::size_t{13}); CHECK(b->id[0] == 'b'); CHECK(retaining.resolve(b->id) == b); }
    {   // ids are unique among live blocks
        std::map<std::string, int> seen;
        for (const Block* b : retaining.all()) CHECK_EQ(++seen[b->id], 1);
    }
    {   // a different birth revision gives different ids; the same one gives the same
        BlockIndex a, b, c;
        a.setBirthRevision("r1");
        b.setBirthRevision("r2");
        c.setBirthRevision("r1");
        const std::string text = "int f(int x)\n{\n    return x + 1;\n}\nint g(int x)\n{\n    return x + 1;\n}\n";
        a.update("a.cpp", text);
        b.update("a.cpp", text);
        c.update("a.cpp", text);
        CHECK(blocksOf(a) != blocksOf(b));
        CHECK_EQ(blocksOf(a), blocksOf(c));
        CHECK_EQ(a.liveCount(), std::size_t{2});
    }
    {   // identical births in one file (the same signature twice) get distinct ids: the ordinal of A16
        BlockIndex x;
        x.update("a.cpp", "void run()\n{\n    go();\n}\nvoid run()\n{\n    go();\n}\n");
        const auto bs = x.blocks("a.cpp");
        CHECK_EQ(bs.size(), std::size_t{2});
        CHECK(bs.size() == 2 && bs[0]->id != bs[1]->id);
    }

    // The lazy index decides exactly what the retaining one does (it reads the old texts back instead of holding them).
    CHECK_EQ(describe(retaining.events()), describe(lazy.events()));
    CHECK_EQ(blocksOf(retaining), blocksOf(lazy));
    CHECK(lazy.stats().textFetches > 0);       // it did read texts back
    CHECK_EQ(retaining.retiredCount(), lazy.retiredCount());

    // A source that cannot supply an old text degrades, never fails: the blocks are retired, none renamed or merged on text it does not have.
    {
        History empty;
        BlockIndex blind;
        blind.setTextSource(&empty);
        blind.update("a.cpp", "int alpha(int x)\n{\n    int y = x * 2;\n    return y + x;\n}\n");
        blind.update("a.cpp", "int beta(int x)\n{\n    int y = x * 2;\n    return y + x;\n}\n");
        CHECK_EQ(blind.liveCount(), std::size_t{1});
        CHECK(blind.events().back().carry == Carry::retired || blind.events().back().carry == Carry::added);
        BlockIndex sighted;   // the same edit with the texts available is a rename
        sighted.update("a.cpp", "int alpha(int x)\n{\n    int y = x * 2;\n    return y + x;\n}\n");
        sighted.update("a.cpp", "int beta(int x)\n{\n    int y = x * 2;\n    return y + x;\n}\n");
        bool renamed = false;
        for (const BlockEvent& e : sighted.events()) renamed = renamed || e.carry == Carry::renamed;
        CHECK(renamed);
    }

    // The index holds no text: a cold build with a source reads none back (no rule needs an old text), and a lazy index is much smaller than a retaining one.
    {
        BlockIndex cold;
        History h;
        cold.setTextSource(&h);
        for (const auto& [path, text] : corpus->files) cold.update(path, text);
        CHECK_EQ(cold.stats().textFetches, std::size_t{0});
        CHECK_EQ(cold.stats().parses, corpus->files.size());
    }

    // A revive probe does not scan the retired set: retire every block of the corpus, then bring one file's blocks back in a form that is NOT the same signature
    // (so only the similarity rule can find them): the candidates compared stay a small fraction of retired x probes.
    {
        BlockIndex x;
        for (const auto& [path, text] : corpus->files) x.update(path, text);
        const std::size_t live = x.liveCount();
        History h;
        for (const auto& [path, text] : corpus->files) x.update(path, std::string());
        CHECK_EQ(x.liveCount(), std::size_t{0});
        CHECK_EQ(x.retiredCount(), live);
        const auto& [path, text] = *corpus->files.begin();
        std::string renamedText = text;
        for (std::size_t p = 0; (p = renamedText.find("(", p)) != std::string::npos; p += 2) renamedText.insert(p, "X");   // every function name gains a suffix: signatures differ
        const std::size_t before = x.stats().reviveCandidates;
        x.update(path, renamedText);
        const std::size_t compared = x.stats().reviveCandidates - before;
        CHECK(x.liveCount() > 0);
        CHECK(compared < x.liveCount() * 20);              // not liveCount x retiredCount
        CHECK(compared * 10 < x.liveCount() * live);
    }

    // The thresholds are met exactly, at the edge, by the filters in front of the rules (they may over-report, never under-report):
    //   revive: a block of 10 tokens retired, a new block holding 8 of them (Jaccard 0.8 exactly, signature different) takes its id back;
    //   merge: a block of 10 tokens retired while a new block holds 6 of them (containment 0.6 exactly) is merged into it, and with 5 it is not.
    {
        const auto body = [](const std::string& tag, int from, int to) {
            std::string s;
            for (int i = from; i < to; ++i) s += "    u" + tag + "_" + std::to_string(i) + "();\n";
            return s;
        };
        BlockIndex x;
        const int trials = 30;
        for (int k = 0; k < trials; ++k) x.update("r" + std::to_string(k) + ".cpp", "void f" + std::to_string(k) + "()\n{\n" + body(std::to_string(k), 0, 10) + "}\n");
        std::map<std::string, std::string> ids;
        for (int k = 0; k < trials; ++k) ids["r" + std::to_string(k) + ".cpp"] = x.blocks("r" + std::to_string(k) + ".cpp")[0]->id;
        for (int k = 0; k < trials; ++k) x.update("r" + std::to_string(k) + ".cpp", std::string());
        for (int k = 0; k < trials; ++k) x.update("q" + std::to_string(k) + ".cpp", "void g" + std::to_string(k) + "()\n{\n" + body(std::to_string(k), 0, 8) + "}\n");
        int revived = 0;
        for (int k = 0; k < trials; ++k) {
            const auto bs = x.blocks("q" + std::to_string(k) + ".cpp");
            revived += bs.size() == 1 && bs[0]->id == ids["r" + std::to_string(k) + ".cpp"];
        }
        CHECK_EQ(revived, trials);

        BlockIndex m;
        int merged = 0, orphaned = 0;
        for (int k = 0; k < trials; ++k) {
            const std::string t = std::to_string(k), a = "a" + t + ".cpp";
            m.update(a, "void x" + t + "()\n{\n" + body("x" + t, 0, 10) + "}\n");
            const std::string before = m.blocks(a)[0]->id;
            const int shared = k % 2 == 0 ? 6 : 5;   // 0.6 merges, 0.5 does not
            m.update(a, "void y" + t + "()\n{\n" + body("x" + t, 0, shared) + body("y" + t, 0, 10) + "}\n");
            const Block* now = m.resolve(before);
            const bool follows = now != nullptr && now->name == "y" + t;
            merged += shared == 6 && follows;
            orphaned += shared == 5 && now == nullptr;
        }
        CHECK_EQ(merged, trials / 2);
        CHECK_EQ(orphaned, trials / 2);
    }

    // The per-blob analysis: the same text at the same path again (a revert) is not folded again, and the ids come out as they would have.
    {
        BlockIndex x;
        x.setAnalysisCacheLimit(8);
        const std::string a = "int f(int x)\n{\n    return x;\n}\n", b = "int f(int x)\n{\n    return x + 1;\n}\n";
        x.update("p.cpp", a);
        x.update("p.cpp", b);
        const std::size_t parses = x.stats().parses;
        x.update("p.cpp", a);
        CHECK_EQ(x.stats().parses, parses);
        CHECK_EQ(x.stats().analysisHits, std::size_t{1});
        CHECK_EQ(x.liveCount(), std::size_t{1});
    }

    // Literals are read back from the text, per block, in file order.
    {
        BlockIndex x;
        x.update("l.cpp", "int f()\n{\n    return parse(\"asin\");\n}\nint g()\n{\n    return 1;\n}\n");
        const auto lits = x.literals("l.cpp");
        CHECK_EQ(lits.size(), std::size_t{2});
        CHECK(lits.size() == 2 && lits[0].count("asin") == 1 && lits[1].empty());
    }

    // A copy of an index is independent: updating it leaves the original alone, and ids agree where the history does.
    {
        BlockIndex x;
        x.update("p.cpp", "int f(int x)\n{\n    return x;\n}\n");
        BlockIndex y = x;
        y.update("q.cpp", "int g(int x)\n{\n    return x + 1;\n}\n");
        CHECK_EQ(x.liveCount(), std::size_t{1});
        CHECK_EQ(y.liveCount(), std::size_t{2});
        CHECK(y.resolve(x.blocks("p.cpp")[0]->id) != nullptr);
        CHECK(x.resolve(y.blocks("q.cpp")[0]->id) == nullptr);
    }
    return finish();
}
