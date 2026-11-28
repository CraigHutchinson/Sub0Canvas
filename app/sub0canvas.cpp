// sub0canvas: the library's command line. It does the I/O the pure library may not (running git, reading files, writing stdout).
//
//   sub0canvas snapshot --repo DIR [--rev REV] [--history N] [--max-files N] [--max-file-bytes N] [--] PATHSPEC...
//
// Writes a viewer bundle to stdout: a JSON array of {path, text} holding layers/turn-N.json (layered-doc-1) and canvas.json. One turn per commit
// that touched the pathspec, oldest first: the last N on REV's first-parent line. Block identities are the library's (BlockIndex), carried across
// the commits, and a `commits` layer anchors each commit to the blocks it changed. Each turn carries only the events since the one before (`eventsFrom`) and each
// text once under its content id (`texts`). Nothing is written to disk or copied out of the
// repository beyond the bundle on stdout, whose size the limits bound (docs/AUTHORING.md section 2: a view is bounded by attention, not data).
#include <print>
#include <sub0canvas/content_id.hpp>
#include <sub0canvas/doc_layers.hpp>
#include <sub0canvas/hunks.hpp>

#include "git_source.hpp"
#include "json_util.hpp"
#include "scan.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <format>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif

namespace {

using namespace sub0canvas;

/// One line to stderr.
template <class... A> void eprintln(std::format_string<A...> f, A&&... a)
{
    std::println(stderr, f, std::forward<A>(a)...);
}

struct Options
{
    std::string repo, rev = "HEAD";
    int history = 8;
    std::size_t maxFiles = 150, maxFileBytes = 200000;
    std::vector<std::string> pathspecs;
};

[[noreturn]] void fail(const std::string& message)
{
    eprintln("sub0canvas: {}", message);
    std::exit(2);
}

/// Arguments reach a shell (popen): refuse every character that could leave its quotes or chain a command, rather than escape for two shells.
void requireSafe(const std::string& what, const std::string& value)
{
    if (value.empty() || value.find_first_of("\"'`$;&|<>^%!\r\n") != std::string::npos)
        fail(std::format("{} contains a character this tool does not pass to git: {}", what, value));
}

std::string git(const Options& o, const std::string& args, const std::string& stdinFile = {})
{
    auto out = app::runGit(o.repo, args, stdinFile);
    if (!out) fail("git failed: " + args);
    return std::move(*out);
}

/// The non-empty lines of git's output, without CRs.
std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    for (auto l : scan::splitLines(text)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) out.push_back(std::move(l));
    }
    return out;
}

/// Snapshot candidates use path detection before fetching blobs.
bool isSupportedPath(std::string_view path) { return !defaultLanguages().detect(path).empty(); }

/// Blob texts for `rev:path` names, read through one `git cat-file --batch` (its input from a temporary file: popen is one-directional).
std::map<std::string, std::string> readBlobs(const Options& o, const std::string& rev, const std::vector<std::string>& paths)
{
    std::map<std::string, std::string> out;
    if (paths.empty()) return out;
    std::random_device rd;
    const auto list = std::filesystem::temp_directory_path() / std::format("sub0canvas-{:08x}.txt", rd());
    std::vector<std::string> queried;
    {
        std::ofstream f(list, std::ios::binary);
        if (!f) fail("cannot write snapshot blob requests");
        for (const auto& path : paths) {
            if (path.find_first_of("\r\n") != std::string::npos) continue;
            queried.push_back(path);
            f << rev << ':' << path << '\n';
        }
    }
    // Check sizes before requesting bodies so large blobs never enter the snapshot buffer.
    const auto info = lines(git(o, "cat-file --batch-check", list.string()));
    std::vector<std::string> selected;
    {
        std::ofstream f(list, std::ios::binary | std::ios::trunc);
        if (!f) fail("cannot write snapshot blob requests");
        for (std::size_t i = 0; i < std::min(info.size(), queried.size()); ++i) {
            if (info[i].ends_with(" missing")) continue;
            std::istringstream row(info[i]);
            std::string id, type;
            std::size_t size = 0;
            if (!(row >> id >> type >> size) || type != "blob" || size > o.maxFileBytes) continue;
            selected.push_back(queried[i]);
            f << id << '\n';
        }
    }
    if (!selected.empty()) {
        const std::string raw = git(o, "cat-file --batch", list.string());
        if (auto texts = gitplumbing::parseCatFileBatch(raw, selected.size())) {
            for (std::size_t i = 0; i < selected.size(); ++i) {
                auto& text = (*texts)[i];
                if (text.presence == Presence::present && !text.text.contains('\0')) out.emplace(selected[i], std::move(text.text));
            }
        }
    }
    std::filesystem::remove(list);
    return out;
}

struct Commit { std::string sha, author, date, subject; };

std::string pathspecArgs(const Options& o) { return app::pathspecArgs(o.pathspecs).value(); }   // every pathspec passed requireSafe at parse

int snapshot(const Options& o)
{
    auto revs = lines(git(o, std::format("rev-list --first-parent --max-count={} {}{}", o.history, o.rev, pathspecArgs(o))));
    if (revs.empty()) fail("no commit on " + o.rev + " touches the pathspec");
    std::ranges::reverse(revs);

    std::vector<Commit> commits;
    {
        std::string args = "show -s --format=%H%x1f%an%x1f%aI%x1f%s";
        for (const auto& r : revs) args += " " + r;
        for (const auto& l : lines(git(o, args))) {
            std::vector<std::string> f;
            std::size_t a = 0;
            for (std::size_t b; (b = l.find('\x1f', a)) != std::string::npos; a = b + 1) f.push_back(l.substr(a, b - a));
            f.push_back(l.substr(a));
            if (f.size() == 4) commits.push_back({f[0], f[1], f[2], f[3]});
        }
    }
    if (commits.size() != revs.size()) fail("could not read the commits' headers");

    BlockIndex index;
    std::map<std::string, std::string> files;
    std::map<std::string, int> revisions;
    std::map<std::string, std::string> revisionIds, textIds;   // by path: the commit a file was last read at (opaque, A1) and its content id (computed once per change)
    std::set<std::string> textsCarried;
    std::size_t eventsCarried = 0;
    std::vector<LayerItem> commitItems;
    json::Writer bundle, log;
    bundle.beginArray();
    log.beginArray();
    std::size_t skipped = 0, unavailable = 0;
    std::set<std::string> pending;
    const app::GitSource source(o.repo);

    for (std::size_t t = 0; t < revs.size(); ++t) {
        index.setTurn(static_cast<int>(t));
        std::vector<std::string> fetch, removed;
        if (t == 0) {
            const auto listing = source.list(revs[0], o.pathspecs);
            if (!listing.known) fail("could not list the snapshot scope");
            for (const auto& path : listing.paths) if (isSupportedPath(path)) pending.insert(path);
        } else {
            const auto changes = gitplumbing::parseNameStatus(git(o, std::format("diff --name-status -z --no-renames {} {}{}",
                                                                               revs[t - 1], revs[t], pathspecArgs(o))));
            if (!changes) fail(changes.error());
            for (const auto& change : *changes) {
                const auto& path = change.path;
                if (change.kind == ChangeKind::deleted) {
                    pending.erase(path);
                    if (files.contains(path)) removed.push_back(path);
                } else if (files.contains(path)) fetch.push_back(path);
                else if (isSupportedPath(path)) pending.insert(path);
            }
        }
        // The hunks of this turn: ONE `git diff -U0` between the two commits for the whole pathspec, mapped per file from the spans the blocks had before
        // the update and have after it. Only files the view holds count, so the totals reconcile with what the view shows.
        std::map<std::string, std::vector<DiffHunk>> hunks;
        if (t > 0) hunks = parseHunks(git(o, std::format("-c core.quotepath=off diff -U0 --no-renames --no-color --no-ext-diff {} {}{}", revs[t - 1], revs[t], pathspecArgs(o))));
        std::map<std::string, std::vector<BlockSpan>> spansBefore;
        auto spansOf = [&](const std::string& p) {
            std::vector<BlockSpan> spans;
            for (const Block* b : index.blocks(p)) spans.push_back({b->id, b->first, b->last});
            return spans;
        };
        if (t > 0) for (const auto& p : fetch) if (hunks.contains(p)) spansBefore[p] = spansOf(p);
        const auto remove = [&](const std::string& path) {
            index.update(path, "");
            files.erase(path);
            textIds.erase(path);
            revisionIds.erase(path);
        };
        const auto publish = [&](const std::string& p, std::string text) {
            index.update(p, text);
            ++revisions[p];
            revisionIds[p] = commits[t].sha;
            textIds[p] = contentId(text);
            log.beginObject().kv("path", p).kv("by", commits[t].sha.substr(0, 10) + " " + commits[t].subject).kv("revision", revisions[p])
                .kv("base", revisions[p] - 1).kv("from", 1).kv("to", static_cast<int>(std::ranges::count(text, '\n'))).kv("delta", 0).endObject();
            files[p] = std::move(text);
        };
        for (const auto& path : removed) remove(path);
        auto changed = readBlobs(o, revs[t], fetch);
        for (const auto& path : fetch) {
            const auto found = changed.find(path);
            if (found == changed.end()) {
                remove(path);
                removed.push_back(path);
                ++unavailable;
            } else publish(path, std::move(found->second));
        }
        while (!pending.empty() && files.size() < o.maxFiles) {
            std::vector<std::string> candidates;
            const std::size_t free = o.maxFiles - files.size();
            while (!pending.empty() && candidates.size() < free) {
                candidates.push_back(*pending.begin());
                pending.erase(pending.begin());
            }
            auto admitted = readBlobs(o, revs[t], candidates);
            unavailable += candidates.size() - admitted.size();
            for (auto& [path, text] : admitted) publish(path, std::move(text));
        }
        skipped += pending.size();

        // This commit as an item, anchored to every block it added or changed (the turn's events), so history is a layer like any other.
        LayerItem item;
        item.layer = "commits";
        item.key = commits[t].sha;
        item.kind = t == 0 ? "base" : "commit";
        item.title = commits[t].sha.substr(0, 10) + " " + commits[t].subject;
        // The base commit "changed" everything it shows: anchoring it to every block is noise, so it gets none. Later commits look their blocks up
        // in one map per turn: BlockIndex::resolve scans every live block, and a call per event was quadratic (229 s for 19,000 files).
        if (t > 0) {
            std::map<std::string, const Block*> live;
            for (const Block* b : index.all()) live.emplace(b->id, b);
            std::set<std::string> seen;
            for (const auto& e : index.events()) {
                if (e.turn != static_cast<int>(t) || e.carry == Carry::retired || e.carry == Carry::merged || !seen.insert(e.id).second) continue;
                if (const auto b = live.find(e.id); b != live.end()) item.anchors.push_back({b->second->id, std::string{b->second->path}, b->second->first, Anchor::Confidence::exact});
            }
        }
        item.detail = std::format("{} by {} on {}\n{}\n{} blocks added or changed", commits[t].sha, commits[t].author, commits[t].date, commits[t].subject,
                                  t == 0 ? std::string("(the base: every block)") : std::to_string(item.anchors.size()));
        commitItems.push_back(item);
        std::vector<LayerItem> shown(commitItems.rbegin(), commitItems.rend());
        for (auto [i, item] : std::views::enumerate(shown)) item.number = static_cast<int>(i) + 1;

        SnapshotSpec spec;
        spec.turn = static_cast<int>(t);
        if (t > 0) {
            std::size_t total = 0, outside = 0;
            for (const auto& [p, list] : hunks) {
                if (!isSupportedPath(p) || !(files.contains(p) || std::ranges::contains(removed, p))) continue;
                const auto before = spansBefore.find(p);
                const HunkMapping m = mapHunks(list, before == spansBefore.end() ? std::vector<BlockSpan>{} : before->second, spansOf(p));
                total += m.total;
                outside += m.outside;
                for (const auto& [id, spans] : m.byBlock) spec.blockHunks[id] = spans;
            }
            spec.hunkCounts = {{total, outside}};
        }
        spec.files = files;
        spec.revisions = revisions;
        spec.revisionIds = revisionIds;
        spec.textIds = textIds;
        spec.layers = {{"commits", shown}};
        spec.layerMeta = {{"commits", {"Commits", "The commits of the view, each anchored to the blocks it added or changed.", "sub0canvas snapshot"}}};
        spec.policies = {{"snapshot", "sub0canvas-snapshot@1"}, {"reconcile", "reconcile@1"}};
        spec.eventsFrom = eventsCarried;
        spec.textsCarried = &textsCarried;
        const std::string turn = layeredJson(spec, index);
        eventsCarried = index.events().size();
        bundle.beginObject().kv("path", std::format("layers/turn-{}.json", t)).kv("text", turn).endObject();
    }
    log.endArray();
    json::Writer canvas;
    canvas.beginObject().key("files").beginObject();
    for (const auto& [p, r] : revisions) canvas.kv(p, r);
    canvas.endObject().kvRaw("log", log.view()).endObject();
    bundle.beginObject().kv("path", "canvas.json").kv("text", canvas.take()).endObject().endArray();

    const std::string out = bundle.take();
    std::fwrite(out.data(), 1, out.size(), stdout);
    eprintln("sub0canvas snapshot: {} turns, {} files, {} file-turns omitted at capacity, {} unavailable, binary or oversized blob revisions, {} bytes",
             revs.size(), files.size(), skipped, unavailable, out.size());
    return 0;
}

Options parse(int argc, char** argv)
{
    Options o;
    bool rest = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) fail(a + " needs a value"); return argv[++i]; };
        if (rest || a.empty() || a[0] != '-') o.pathspecs.push_back(a);
        else if (a == "--") rest = true;
        else if (a == "--repo") o.repo = next();
        else if (a == "--rev") o.rev = next();
        else if (a == "--history") o.history = std::stoi(next());
        else if (a == "--max-files") o.maxFiles = std::stoul(next());
        else if (a == "--max-file-bytes") o.maxFileBytes = std::stoul(next());
        else fail("unknown option " + a);
    }
    if (o.repo.empty()) fail("snapshot needs --repo DIR");
    if (o.pathspecs.empty()) fail("snapshot needs at least one pathspec: a view is scoped, never the whole repository");
    if (o.history < 1 || o.history > 200) fail("--history is 1..200");
    requireSafe("--repo", o.repo);
    requireSafe("--rev", o.rev);
    for (const auto& p : o.pathspecs) requireSafe("a pathspec", p);
    return o;
}

}  // namespace

namespace sub0canvas::app { int historyCommand(int argc, char** argv); }

int main(int argc, char** argv)
{
    const std::string command = argc < 2 ? std::string{} : argv[1];
    if (command != "snapshot" && command != "history") {
        eprintln("usage: sub0canvas snapshot --repo DIR [--rev REV] [--history N] [--max-files N] [--max-file-bytes N] [--] PATHSPEC...");
        eprintln("       sub0canvas history --repo DIR [--rev REV] [--max N] [--checkpoint N] [--no-follow] [--name NAME]... [--blocks | --blocks-binary] [--] PATH...");
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);   // the output is bytes: no CRLF translation
#endif
    if (command == "history") return sub0canvas::app::historyCommand(argc, argv);
    return snapshot(parse(argc, argv));
}
