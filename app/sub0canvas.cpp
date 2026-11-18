// sub0canvas: the library's command line. It does the I/O the pure library may not (running git, reading files, writing stdout).
//
//   sub0canvas snapshot --repo DIR [--rev REV] [--history N] [--max-files N] [--max-file-bytes N] [--] PATHSPEC...
//
// Writes a viewer bundle to stdout: a JSON array of {path, text} holding layers/turn-N.json (layered-doc-1) and canvas.json. One turn per commit
// that touched the pathspec, oldest first: the last N on REV's first-parent line. Block identities are the library's (BlockIndex), carried across
// the commits, and a `commits` layer anchors each commit to the blocks it changed. Nothing is written to disk and nothing is copied out of the
// repository beyond the bundle on stdout, whose size the limits bound (docs/AUTHORING.md section 2: a view is bounded by attention, not data).
#include <sub0canvas/doc_layers.hpp>

#include "json_util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <format>
#include <random>
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

/// One line to stderr. Not std::println: <print> is missing from GCC 13, which some machines carry first on PATH.
template <class... A> void eprintln(std::format_string<A...> f, A&&... a)
{
    std::string line = std::format(f, std::forward<A>(a)...);
    line.push_back('\n');
    std::fputs(line.c_str(), stderr);
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

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string git(const Options& o, const std::string& args)
{
    const std::string cmd = "git -C " + quote(o.repo) + " " + args;
#ifdef _WIN32
    FILE* p = popen(cmd.c_str(), "rb");   // binary: no CRLF translation of blob bytes
#else
    FILE* p = popen(cmd.c_str(), "r");    // glibc's popen refuses "rb" (EINVAL); POSIX streams do not translate anyway
#endif
    if (!p) fail("cannot run git");
    std::string out;
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, p)) > 0;) out.append(buf, n);
    if (pclose(p) != 0) fail("git failed: " + args);
    return out;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) out.push_back(l);
    }
    return out;
}

/// The languages the block heuristic serves today (BLOCK_DISCOVERY.md section 2); everything else is not shown, so a view is code, not data.
bool isCode(const std::string& path)
{
    static const std::set<std::string> exts = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".cs", ".java", ".js", ".mjs", ".ts",
                                               ".py", ".go", ".rs", ".mm", ".m", ".swift", ".kt"};
    const auto dot = path.rfind('.');
    return dot != std::string::npos && exts.count(path.substr(dot));
}

/// Blob texts for `rev:path` names, read through one `git cat-file --batch` (its input from a temporary file: popen is one-directional).
std::map<std::string, std::string> readBlobs(const Options& o, const std::string& rev, const std::vector<std::string>& paths)
{
    std::map<std::string, std::string> out;
    if (paths.empty()) return out;
    std::random_device rd;
    const auto list = std::filesystem::temp_directory_path() / std::format("sub0canvas-{:08x}.txt", rd());
    {
        std::ofstream f(list, std::ios::binary);
        for (const auto& p : paths) f << rev << ':' << p << '\n';
    }
    const std::string raw = git(o, "cat-file --batch < " + quote(list.string()));
    std::filesystem::remove(list);
    std::size_t at = 0;
    for (const auto& p : paths) {
        const auto eol = raw.find('\n', at);
        if (eol == std::string::npos) break;
        const std::string header = raw.substr(at, eol - at);
        at = eol + 1;
        if (header.ends_with(" missing")) continue;
        const auto size = std::stoull(header.substr(header.rfind(' ') + 1));
        std::string text = raw.substr(at, size);
        at += size + 1;
        if (size <= o.maxFileBytes && text.find('\0') == std::string::npos) out[p] = std::move(text);
    }
    return out;
}

struct Commit { std::string sha, author, date, subject; };

std::string pathspecArgs(const Options& o)
{
    std::string s = " --";
    for (const auto& p : o.pathspecs) s += " " + quote(p);
    return s;
}

int snapshot(const Options& o)
{
    auto revs = lines(git(o, std::format("rev-list --first-parent --max-count={} {}{}", o.history, o.rev, pathspecArgs(o))));
    if (revs.empty()) fail("no commit on " + o.rev + " touches the pathspec");
    std::reverse(revs.begin(), revs.end());

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
    std::vector<LayerItem> commitItems;
    json::Writer bundle, log;
    bundle.beginArray();
    log.beginArray();
    std::size_t skipped = 0;

    for (std::size_t t = 0; t < revs.size(); ++t) {
        index.setTurn(static_cast<int>(t));
        std::vector<std::string> fetch, removed;
        if (t == 0) {
            // The first turn: the pathspec's code files at that commit, in path order, up to the limit (the rest are counted, never silently dropped).
            for (const auto& p : lines(git(o, "ls-tree -r --name-only " + revs[0] + pathspecArgs(o))))
                if (isCode(p)) (fetch.size() < o.maxFiles ? fetch.push_back(p) : void(++skipped));
        } else {
            for (const auto& l : lines(git(o, std::format("diff --name-status --no-renames {} {}{}", revs[t - 1], revs[t], pathspecArgs(o))))) {
                const auto tab = l.find('\t');
                if (tab == std::string::npos) continue;
                const std::string path = l.substr(tab + 1);
                if (!isCode(path)) continue;
                if (l[0] == 'D') { if (files.count(path)) removed.push_back(path); }
                else if (files.count(path) || files.size() + fetch.size() < o.maxFiles) fetch.push_back(path);
                else ++skipped;
            }
        }
        for (const auto& p : removed) { index.update(p, ""); files.erase(p); }
        for (auto& [p, text] : readBlobs(o, revs[t], fetch)) {
            index.update(p, text);
            ++revisions[p];
            log.beginObject().kv("path", p).kv("by", commits[t].sha.substr(0, 10) + " " + commits[t].subject).kv("revision", revisions[p])
                .kv("base", revisions[p] - 1).kv("from", 1).kv("to", static_cast<int>(std::count(text.begin(), text.end(), '\n'))).kv("delta", 0).endObject();
            files[p] = std::move(text);
        }

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
                if (const auto b = live.find(e.id); b != live.end()) item.anchors.push_back({b->second->id, b->second->path, b->second->first, Anchor::Confidence::exact});
            }
        }
        item.detail = std::format("{} by {} on {}\n{}\n{} blocks added or changed", commits[t].sha, commits[t].author, commits[t].date, commits[t].subject,
                                  t == 0 ? std::string("(the base: every block)") : std::to_string(item.anchors.size()));
        commitItems.push_back(item);
        std::vector<LayerItem> shown(commitItems.rbegin(), commitItems.rend());
        for (std::size_t i = 0; i < shown.size(); ++i) shown[i].number = static_cast<int>(i) + 1;

        const std::string turn = layeredJson(static_cast<int>(t), files, revisions, index, {{"commits", shown}});
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
    eprintln("sub0canvas snapshot: {} turns, {} files, {} code files over the limit not shown, {} bytes", revs.size(), files.size(), skipped,
                 out.size());
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

int main(int argc, char** argv)
{
    if (argc < 2 || std::string(argv[1]) != "snapshot") {
        eprintln("usage: sub0canvas snapshot --repo DIR [--rev REV] [--history N] [--max-files N] [--max-file-bytes N] [--] PATHSPEC...");
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);   // the bundle is bytes: no CRLF translation
#endif
    return snapshot(parse(argc, argv));
}
