// The synthetic corpus of the scale harness (bench/corpus.hpp, docs/TESTING.md "The synthetic corpus"): same seed, same bytes; the sizes it promises; the
// blocks the library finds in what it generates are the blocks the generator built; the profile B mix; the streamed form; the edit stream is reproducible and
// keeps its model in step with the text. Nothing here times anything (the benchmarks are a separate, local-only executable).
#include "sub0platform/testing/check.hpp"
#include "bench/corpus.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/outline.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <string>

using namespace sub0canvas;
using namespace sub0canvas::bench;

namespace {

std::string lastName(const std::string& name)
{
    const std::size_t p = name.rfind("::");
    return p == std::string::npos ? name : name.substr(p + 2);
}

/// Whether `foldRegions` finds exactly the functions of the model, in order, each at the line the model puts its signature.
bool blocksMatchModel(const std::string& path, const std::string& text, const CodeFile& model)
{
    const auto regions = foldRegions(text, isPythonPath(path), true);
    if (regions.size() != model.functions.size()) return false;
    for (const auto& [region, function] : std::views::zip(regions, model.functions))
        if (lastName(region.name) != lastName(function.name)) return false;
    return true;
}

std::size_t blocksOfAll(const FileMap& files)
{
    std::size_t n = 0;
    for (const auto& [path, text] : files)
        if (path.ends_with(".cpp") || path.ends_with(".hpp") || path.ends_with(".py")) n += foldRegions(text, isPythonPath(path), true).size();
    return n;
}

}  // namespace

int main()
{
    {   // Determinism: the same seed gives byte-identical files, models and counts; another seed does not.
        CorpusSpec spec;
        spec.seed = 42;
        spec.targetLines = 5'000;
        const auto a = generateCorpus(spec), b = generateCorpus(spec);
        CHECK(a && b && a->files == b->files);
        CHECK(a && b && a->stats.blocks == b->stats.blocks && a->stats.lines == b->stats.lines);
        spec.seed = 43;
        const auto c = generateCorpus(spec);
        CHECK(a && c && a->files != c->files);
        // The profile B mix is deterministic too.
        spec.mix = profileB();
        const auto d = generateCorpus(spec), e = generateCorpus(spec);
        CHECK(d && e && d->files == e->files);
    }
    for (const std::size_t target : {std::size_t{1'000}, std::size_t{10'000}, std::size_t{100'000}})
    {   // Profile A at the gated sizes: the lines asked for (the last file overshoots), only code, and the blocks the library finds are the model's.
        CorpusSpec spec;
        spec.seed = 7;
        spec.targetLines = target;
        const auto corpus = generateCorpus(spec);
        CHECK(corpus.has_value());
        if (!corpus) continue;
        const CorpusStats& s = corpus->stats;
        CHECK(s.lines >= target);
        CHECK(s.lines < target + 30'000);   // one file past the target; the tail of the file-size distribution reaches thousands of lines
        CHECK_EQ(s.files, s.codeFiles);
        CHECK_EQ(s.files, s.entries);
        CHECK_EQ(s.files, corpus->files.size());
        CHECK_EQ(s.blocks, blocksOfAll(corpus->files));
        std::size_t mismatched = 0, python = 0;
        for (const auto& [path, text] : corpus->files)
        {
            mismatched += !blocksMatchModel(path, text, corpus->models.at(path));
            python += path.ends_with(".py");
        }
        CHECK_EQ(mismatched, std::size_t{0});
        CHECK(python > 0 || target == 1'000);   // both languages appear once there are files enough
        CHECK(target < 10'000 || (python * 10 > s.files && python * 2 < s.files));   // about 30% Python
        // The block index over the corpus finds the same blocks: one live block per function, and and none is left over.
        BlockIndex index;
        for (const auto& [path, text] : corpus->files) index.update(path, text);
        CHECK_EQ(index.all().size(), s.blocks);
    }
    {   // Files fall in a tree of directories (24 entries to a directory), paths are unique, and a mean entry is a few hundred lines.
        CorpusSpec spec;
        spec.seed = 3;
        spec.targetLines = 50'000;
        const auto corpus = generateCorpus(spec);
        CHECK(corpus.has_value());
        if (corpus)
        {
            std::set<std::string> dirs;
            for (const auto& [path, text] : corpus->files) dirs.insert(path.substr(0, path.rfind('/')));
            CHECK(dirs.size() > 5 && dirs.size() < corpus->files.size());
            const double mean = static_cast<double>(corpus->stats.lines) / static_cast<double>(corpus->stats.entries);
            CHECK(mean > 150 && mean < 800);
        }
    }
    {   // The streamed form: a corpus too large for memory is refused with a pointer to the stream; the stream and entry() are pure functions of the index.
        CorpusSpec spec;
        spec.seed = 11;
        spec.targetLines = kMaterialiseLimitLines + 1;
        CHECK(!generateCorpus(spec).has_value());
        spec.targetLines = 1'000'000'000;   // 10^9 lines: never held, sampled by index
        const double mean = meanLinesPerEntry(spec, 256);
        CHECK(mean > 150 && mean < 800);
        for (const std::size_t i : {std::size_t{0}, std::size_t{12'345}, std::size_t{2'000'000}, std::size_t{4'000'000'000}})
        {
            const auto first = entry(spec, i), second = entry(spec, i);
            CHECK_EQ(first.size(), second.size());
            CHECK(first.size() == 1 && second.size() == 1 && first[0].path == second[0].path && first[0].text == second[0].text);
            CHECK(first.size() == 1 && blocksMatchModel(first[0].path, first[0].text, *first[0].code));
        }
        // The stream visits the same entries a materialised corpus holds.
        spec.targetLines = 20'000;
        const auto corpus = generateCorpus(spec);
        CorpusStream stream(spec);
        std::size_t entries = 0;
        bool same = corpus.has_value();
        while (const auto files = stream.next())
        {
            ++entries;
            for (const GeneratedFile& f : *files) same = same && corpus->files.count(f.path) && corpus->files.at(f.path) == f.text;
        }
        CHECK(same);
        CHECK(corpus && entries == corpus->stats.entries && stream.linesSoFar() == corpus->stats.lines && stream.entriesSoFar() == entries);
    }
    {   // Profile B: about a third sidecars, a seventh images, a quarter code, the rest serialised data, text and Markdown; every sidecar sits beside its asset;
        // binaries count no lines; only the code files have blocks.
        CorpusSpec spec;
        spec.seed = 5;
        spec.targetLines = 100'000;
        spec.mix = profileB();
        const auto corpus = generateCorpus(spec);
        CHECK(corpus.has_value());
        if (corpus)
        {
            const CorpusStats& s = corpus->stats;
            const double files = static_cast<double>(s.files);
            CHECK(s.sidecars > 0 && s.images > 0 && s.serialised > 0 && s.texts > 0 && s.markdowns > 0 && s.codeFiles > 0);
            CHECK(static_cast<double>(s.sidecars) / files > 0.25 && static_cast<double>(s.sidecars) / files < 0.40);
            CHECK(static_cast<double>(s.images) / files > 0.08 && static_cast<double>(s.images) / files < 0.20);
            CHECK(static_cast<double>(s.codeFiles) / files > 0.15 && static_cast<double>(s.codeFiles) / files < 0.40);
            CHECK_EQ(s.sidecars, s.images + s.serialised);
            std::size_t orphans = 0;
            for (const auto& [path, text] : corpus->files)
                if (path.ends_with(".meta")) orphans += !corpus->files.count(path.substr(0, path.size() - 5));
            CHECK_EQ(orphans, std::size_t{0});
            std::size_t pngs = 0;
            for (const auto& [path, text] : corpus->files)
                if (path.ends_with(".png")) { ++pngs; CHECK(text.starts_with("\x89PNG")); }
            CHECK_EQ(pngs, s.images);
            CHECK_EQ(s.blocks, corpus->models.empty() ? 0 : blocksOfAll(corpus->files));
            CHECK(s.lines >= spec.targetLines);
        }
    }
    {   // The edit stream: reproducible, its model stays in step with the text it renders, every edit kind occurs, the index follows it.
        CorpusSpec spec;
        spec.seed = 9;
        spec.targetLines = 10'000;
        const auto corpus = generateCorpus(spec);
        CHECK(corpus.has_value());
        if (!corpus) return finish();
        EditStream one(*corpus, 99), two(*corpus, 99), other(*corpus, 100);
        BlockIndex index;
        FileMap texts = corpus->files;
        for (const auto& [path, text] : texts) index.update(path, text);
        std::set<EditKind> kinds;
        std::size_t differing = 0, commitsWithChanges = 0;
        bool reproducible = true;
        for (int i = 0; i < 300; ++i)
        {
            const Commit a = one.next(), b = two.next(), c = other.next();
            reproducible = reproducible && a.changes.size() == b.changes.size() && a.truth.size() == b.truth.size();
            for (std::size_t k = 0; k < a.changes.size() && k < b.changes.size(); ++k)
                reproducible = reproducible && a.changes[k].path == b.changes[k].path && a.changes[k].text == b.changes[k].text;
            differing += !a.changes.empty() && !c.changes.empty() && a.changes[0].path != c.changes[0].path;
            commitsWithChanges += !a.changes.empty();
            CHECK(a.number == static_cast<std::size_t>(i + 1) && !a.truth.empty());
            for (const EditTruth& t : a.truth) kinds.insert(t.kind);
            index.setTurn(i + 1);
            for (const FileChange& ch : a.changes)
            {
                if (ch.text) { texts[ch.path] = *ch.text; index.update(ch.path, *ch.text); }
                else { texts.erase(ch.path); index.update(ch.path, std::string()); }
            }
        }
        CHECK(reproducible);
        CHECK(differing > 0);
        CHECK_EQ(commitsWithChanges, std::size_t{300});
        CHECK_EQ(kinds.size(), std::size_t{9});   // edited, resigned, renamed, added, removed, revived, moved, split, merged: all drawn within 300 commits
        CHECK(texts == one.texts());
        CHECK_EQ(blocksOfAll(texts), one.liveFunctions());
        CHECK_EQ(index.all().size(), one.liveFunctions());   // every live function is a live block, and no retired block lingers as live
    }
    return finish();
}
