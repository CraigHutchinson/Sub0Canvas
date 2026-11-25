// G1's benchmarks of the revision index (docs/SCALE.md sections 4 and 6.2), linked into Sub0Canvas_Bench and registered from its main
// (`historyBenchmarks`). Library only: a synthetic DocumentSource over the seeded corpus and its edit stream, so the 200-commit history is the same on every machine
// and no git runs. The deterministic metrics say that cost followed the changes (R-SCALE-1, R-SCALE-9): one `BlockIndex::update` per path a commit changed, a scope
// replays only the commits that touched it, a checkpoint bounds a replay; the timing rows are measured locally only.
#include "bench_runner.hpp"

#include "bench/corpus.hpp"
#include "sub0canvas/revision_index.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <vector>

using namespace sub0canvas;
namespace bench = sub0canvas::bench;

namespace {

bool isCode(const std::string& path) { return path.ends_with(".cpp") || path.ends_with(".hpp") || path.ends_with(".py"); }

/// A corpus and its edit stream as a document source: revision "r0" is the corpus's code files, "rN" the state after the N-th commit of the stream. No renames (the
/// stream edits, adds and deletes files); function moves between files are the revive rule's work, as in git.
class StreamSource final : public DocumentSource
{
public:
    StreamSource(const bench::Corpus& corpus, std::size_t commits, std::uint64_t seed)
    {
        for (const auto& [path, text] : corpus.files)
            if (isCode(path)) versions_[path].push_back({0, text});
        bench::EditStream stream(corpus, seed);
        for (std::size_t n = 1; n <= commits; ++n) {
            const bench::Commit c = stream.next();
            std::vector<PathChange> changes;
            for (const bench::FileChange& f : c.changes) {
                auto& v = versions_[f.path];
                const bool existed = !v.empty() && v.back().second.has_value();
                changes.push_back({!f.text ? ChangeKind::deleted : existed ? ChangeKind::modified : ChangeKind::added, f.path, {}, 0});
                v.push_back({n, f.text});
            }
            changes_.push_back(std::move(changes));
        }
    }

    [[nodiscard]] std::size_t commits() const { return changes_.size(); }
    [[nodiscard]] static std::string idOf(std::size_t n) { return "r" + std::to_string(n); }
    [[nodiscard]] const std::vector<std::vector<PathChange>>& stream() const { return changes_; }

    [[nodiscard]] History history(const std::string& to, const Scope& scope, std::size_t limit) const override
    {
        History out;
        const auto at = index(to);
        if (!at) return out;
        for (std::size_t n = *at; n >= 1; --n) {
            Commit c;
            c.info.id = idOf(n);
            c.info.parents = {idOf(n - 1)};
            for (const PathChange& ch : changes_[n - 1]) if (inScope(scope, ch.path)) c.changes.push_back(ch);
            if (c.changes.empty()) continue;
            out.commits.push_back(std::move(c));
            if (limit != 0 && out.commits.size() >= limit) return out;
        }
        out.commits.push_back({RevisionInfo{idOf(0), {}, {}, {}, {}}, {}});   // r0 is the corpus: every file of the scope is added in it (a root)
        for (const auto& [path, v] : versions_) if (inScope(scope, path) && v.front().first == 0) out.commits.back().changes.push_back({ChangeKind::added, path, {}, 0});
        if (out.commits.back().changes.empty()) out.commits.pop_back();
        if (limit != 0 && out.commits.size() > limit) out.commits.resize(limit);
        return out;
    }
    [[nodiscard]] std::optional<std::vector<PathChange>> changes(const std::string&, const std::string&) const override { return std::nullopt; }
    [[nodiscard]] std::vector<FileText> read(const std::vector<FileRef>& refs) const override
    {
        reads += refs.size();
        std::vector<FileText> out;
        for (const FileRef& r : refs) {
            const auto at = index(r.revision);
            const auto it = versions_.find(r.path);
            std::optional<std::string> text;
            if (at && it != versions_.end())
                for (const auto& [n, t] : it->second) if (n <= *at) text = t;
            out.push_back(text ? FileText::known(*text) : FileText::missing());
        }
        return out;
    }
    [[nodiscard]] Listing list(const std::string& revision, const Scope& scope) const override
    {
        Listing l;
        const auto at = index(revision);
        if (!at) return l;
        l.known = true;
        for (const auto& [path, v] : versions_) {
            std::optional<std::string> text;
            for (const auto& [n, t] : v) if (n <= *at) text = t;
            if (text && inScope(scope, path)) l.paths.push_back(path);
        }
        return l;
    }
    [[nodiscard]] std::optional<std::string> resolve(const std::string& name) const override
    {
        if (name == "HEAD") return idOf(changes_.size());
        return index(name) ? std::optional<std::string>(name) : std::nullopt;
    }

    mutable std::size_t reads = 0;

private:
    [[nodiscard]] std::optional<std::size_t> index(const std::string& id) const
    {
        if (id.size() < 2 || id[0] != 'r') return std::nullopt;
        const std::size_t n = std::strtoul(id.c_str() + 1, nullptr, 10);
        return n <= changes_.size() ? std::optional<std::size_t>(n) : std::nullopt;
    }
    std::map<std::string, std::vector<std::pair<std::size_t, std::optional<std::string>>>> versions_;   // by path: (revision, text or deleted), oldest first
    std::vector<std::vector<PathChange>> changes_;
};

constexpr std::size_t kCommits = 200;

struct History200
{
    bench::Corpus corpus;
    std::unique_ptr<StreamSource> source;
    History200()
    {
        bench::CorpusSpec spec;
        spec.seed = 1;
        spec.targetLines = 20'000;
        auto made = bench::generateCorpus(spec);
        if (!made) { std::println(stderr, "corpus: {}", made.error()); std::exit(2); }
        corpus = std::move(*made);
        source = std::make_unique<StreamSource>(corpus, kCommits, 11);
    }
};

History200& history200() { static History200 h; return h; }

/// Runs `body` once per process however often the workload repeats (a metric is a fact, not a rate).
struct Once
{
    bool done = false;
    void operator()(const std::function<void()>& body) { if (!done) { done = true; body(); } }
};

}  // namespace

void historyBenchmarks(sub0bench::BenchRunner& runner)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    bench.warmup(1).epochs(5).minEpochIterations(1);

    // The 200-commit replay: the whole corpus is the scope, so the baseline is the corpus (a cold build) and each commit adds only its changed files.
    {
        Once once;
        runner.run("history replay, 200 commits over 2e4 lines", [&] {
            History200& h = history200();
            once([&] {
                RevisionIndex::Options options;
                options.maxRevisions = kCommits;   // 200 commits, with r0 (the corpus) as the baseline: a window that starts mid-history, as a real one does
                RevisionIndex index(*h.source, options);
                const auto at = index.at({}, "HEAD");
                const ReplayStats& st = index.stats();
                std::size_t changed = 0;
                for (const auto& c : h.source->stream()) changed += c.size();
                std::size_t baseline = h.source->list(StreamSource::idOf(0), {}).paths.size();
                runner.metric("history replay 200: commits in the stream", static_cast<double>(h.source->commits()), "n");
                runner.metric("history replay 200: paths changed over the commits", static_cast<double>(changed), "n");
                runner.metric("history replay 200: baseline files", static_cast<double>(baseline), "files");
                runner.metric("history replay 200: commits replayed", static_cast<double>(st.commitsReplayed), "n");
                // Cost per commit follows the paths it changed: after the baseline, one update per changed path and no more.
                runner.metric("history replay 200: updates after the baseline", static_cast<double>(st.updates - baseline), "n");
                runner.metric("history replay 200: files read after the baseline", static_cast<double>(st.filesRead - baseline), "n");
                runner.metric("history replay 200: checkpoints", static_cast<double>(st.checkpoints), "n");
                runner.metric("history replay 200: events", static_cast<double>((*at)->index.events().size()), "n");
                runner.metric("history replay 200: live blocks at the head", static_cast<double>((*at)->index.all().size()), "blocks");
            });
            RevisionIndex::Options options;
            options.maxRevisions = kCommits;
            RevisionIndex index(*h.source, options);
            auto at = index.at({}, "HEAD");
            ankerl::nanobench::doNotOptimizeAway(at);
        });
    }

    // A sparse scope: five files of the corpus. Only the commits that touched them are replayed, only their texts are read.
    {
        Once once;
        static Scope scope;
        runner.run("history replay, a scope of five files over 200 commits", [&] {
            History200& h = history200();
            if (scope.empty()) {
                std::map<std::string, std::size_t> touches;
                for (const auto& c : h.source->stream()) for (const PathChange& ch : c) ++touches[ch.path];
                std::vector<std::pair<std::size_t, std::string>> ranked;
                for (const auto& [p, n] : touches) ranked.push_back({n, p});
                std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
                for (std::size_t i = 0; i < 5 && i < ranked.size(); ++i) scope.push_back(ranked[i].second);
            }
            once([&] {
                StreamSource& src = *h.source;
                src.reads = 0;
                RevisionIndex index(src);
                const auto at = index.at(scope, "HEAD");
                std::size_t commits = 0, changes = 0;
                for (const auto& c : src.stream()) {
                    std::size_t here = 0;
                    for (const PathChange& ch : c) here += inScope(scope, ch.path);
                    commits += here > 0;
                    changes += here;
                }
                const std::size_t rootFiles = src.list(StreamSource::idOf(0), scope).paths.size();   // the root revision adds each file the scope already has
                const std::size_t root = rootFiles > 0 ? 1 : 0;
                runner.metric("history sparse scope: paths in the scope", static_cast<double>(scope.size()), "n");
                runner.metric("history sparse scope: commits that touched it", static_cast<double>(commits), "n");
                runner.metric("history sparse scope: commits replayed, the root excluded", static_cast<double>(index.stats().commitsReplayed - root), "n");
                runner.metric("history sparse scope: paths changed in it", static_cast<double>(changes), "n");
                runner.metric("history sparse scope: updates, the root's excluded", static_cast<double>(index.stats().updates - rootFiles), "n");
                runner.metric("history sparse scope: texts read, the root's excluded", static_cast<double>(src.reads - rootFiles), "n");
                runner.metric("history sparse scope: events", static_cast<double>((*at)->index.events().size()), "n");
            });
            RevisionIndex index(*h.source);
            auto at = index.at(scope, "HEAD");
            ankerl::nanobench::doNotOptimizeAway(at);
        });
    }

    // A question about an earlier revision replays from the nearest checkpoint (every 50 revisions): ordinal 125 of the line costs 25 commits, not 125.
    {
        Once once;
        static std::unique_ptr<RevisionIndex> index;
        runner.run("history at() of revision 125 from a checkpoint, 2e4 lines", [&] {
            History200& h = history200();
            if (!index) {
                index = std::make_unique<RevisionIndex>(*h.source);
                (void)index->at({}, "HEAD");   // every revision in order: the line is whole commit ordinals 1..200 (the stream touches some file each commit)
            }
            once([&] {
                const std::size_t before = index->stats().commitsReplayed;
                const auto at = index->at({}, StreamSource::idOf(125));
                runner.metric("history at 125: commits replayed from the checkpoint", static_cast<double>(index->stats().commitsReplayed - before), "n");
                runner.metric("history at 125: ordinal of the state", static_cast<double>((*at)->ordinal), "n");
            });
            auto at = index->at({}, StreamSource::idOf(125));
            ankerl::nanobench::doNotOptimizeAway(at);
        });
    }
}
