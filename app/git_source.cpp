// The git DocumentSource (git_source.hpp): process runner, one git command per question, output parsed by the library.
#include "git_source.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace sub0canvas::app {

namespace {

namespace gp = sub0canvas::gitplumbing;

/// Arguments reach a shell (popen): refuse every character that could leave its quotes or chain a command, rather than escape for two shells.
bool safe(const std::string& s) { return !s.empty() && s.find_first_of("\"'`$;&|<>^%!\r\n") == std::string::npos; }

std::string quote(const std::string& s) { return "\"" + s + "\""; }

void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

constexpr std::size_t kBatch = 2000;   ///< objects per `cat-file --batch` process

}  // namespace

std::optional<std::string> pathspecArgs(const Scope& scope)
{
    if (scope.empty()) return std::string{};
    std::string out = " --";
    for (const auto& p : scope) {
        if (!safe(p)) return std::nullopt;
        out += " " + quote(p);
    }
    return out;
}

std::optional<std::string> runGit(const std::string& repo, const std::string& args, const std::string& stdinFile)
{
    // The rename limit is git's maximum: above git's default of 1000 files a large commit silently skips rename detection and every moved file looks
    // like a delete plus an add, which breaks block identity across the move.
    std::string cmd = "git -C " + quote(repo) + " -c diff.renameLimit=32767 " + args;
    if (!stdinFile.empty()) cmd += " < " + quote(stdinFile);
#ifdef _WIN32
    FILE* p = popen(cmd.c_str(), "rb");   // binary: no CRLF translation of blob bytes
#else
    FILE* p = popen(cmd.c_str(), "r");    // glibc's popen refuses "rb" (EINVAL); POSIX streams do not translate anyway
#endif
    if (!p) return std::nullopt;
    std::string out;
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, p)) > 0;) out.append(buf, n);
    if (pclose(p) != 0) return std::nullopt;
    return out;
}

GitSource::GitSource(std::string repo) : repo_(std::move(repo))
{
    setEnv("GIT_NO_LAZY_FETCH", "1");        // a partial clone's missing object stays missing: unknown, never a download (A20)
    setEnv("GIT_TERMINAL_PROMPT", "0");
    setEnv("GIT_OPTIONAL_LOCKS", "0");       // read-only
}

std::optional<std::string> GitSource::run(const std::string& args, const std::string& stdinFile) const
{
    commands_.push_back(args);
    return runGit(repo_, "--literal-pathspecs " + args, stdinFile);
}

bool GitSource::shallow() const
{
    if (shallow_) return *shallow_;
    const auto answer = run("rev-parse --is-shallow-repository");
    shallow_ = answer && answer->starts_with("true");
    if (*shallow_) {
        if (const auto where = run("rev-parse --git-path shallow")) {
            std::string path = *where;
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
            std::filesystem::path file(path);
            if (file.is_relative()) file = std::filesystem::path(repo_) / file;
            std::ifstream in(file);
            for (std::string line; std::getline(in, line);) if (!line.empty()) boundary_.insert(line);
        }
    }
    return *shallow_;
}

History GitSource::history(const std::string& to, const Scope& scope, std::size_t limit) const
{
    History out;
    const auto spec = pathspecArgs(scope);
    if (!safe(to) || !spec) return out;
    const auto command = [&](const char* renames) {
        return std::format("log --first-parent --diff-merges=first-parent {} --name-status -z \"--format={}\"{} {}{}", renames, gp::kLogFormat,
                           limit == 0 ? std::string{} : std::format(" -n {}", limit), to, *spec);
    };
    // Rename and copy detection need both blobs: where one is missing (a partial clone) git fails, and the history is asked again without it, so that a
    // missing object costs the rename, never the view (A20).
    auto raw = run(command("-M -C"));
    if (!raw) raw = run(command("--no-renames"));
    if (!raw) return out;
    auto parsed = gp::parseLog(*raw);
    if (!parsed) return out;
    out.commits = std::move(*parsed);
    if (shallow()) {
        const bool limitHit = limit != 0 && out.commits.size() >= limit;
        // A shallow clone's first-parent walk ends at its boundary unless the limit stopped it first: the history is cut there.
        out.cut = !limitHit || (!out.commits.empty() && boundary_.contains(out.commits.back().info.id));
    }
    return out;
}

std::optional<std::vector<PathChange>> GitSource::changes(const std::string& parent, const std::string& revision) const
{
    if (!safe(parent) || !safe(revision)) return std::nullopt;
    auto raw = run(std::format("diff-tree -r -M -C --name-status -z {} {}", parent, revision));
    if (!raw) raw = run(std::format("diff-tree -r --no-renames --name-status -z {} {}", parent, revision));
    if (!raw) return std::nullopt;
    auto parsed = gp::parseNameStatus(*raw);
    if (!parsed) return std::nullopt;
    return std::move(*parsed);
}

std::vector<FileText> GitSource::read(const std::vector<FileRef>& refs) const
{
    std::vector<FileText> out(refs.size());   // every answer starts unknown
    for (std::size_t from = 0; from < refs.size(); from += kBatch) {
        const std::size_t to = std::min(refs.size(), from + kBatch);
        std::vector<FileRef> sent;
        std::vector<std::size_t> at;   // the position in `refs` of each request actually sent
        for (std::size_t i = from; i < to; ++i) {
            if (refs[i].revision.find_first_of("\r\n:") != std::string::npos || refs[i].path.find_first_of("\r\n") != std::string::npos) continue;   // cat-file's input is one request per line
            sent.push_back(refs[i]);
            at.push_back(i);
        }
        if (sent.empty()) continue;
        std::random_device rd;
        const auto list = std::filesystem::temp_directory_path() / std::format("sub0canvas-{:08x}.txt", rd());
        {
            std::ofstream f(list, std::ios::binary);
            f << gp::catFileRequests(sent);
        }
        const auto raw = run("cat-file --batch", list.string());
        std::filesystem::remove(list);
        if (!raw) continue;
        const auto texts = gp::parseCatFileBatch(*raw, sent.size());
        if (!texts) continue;
        for (std::size_t k = 0; k < sent.size(); ++k) out[at[k]] = (*texts)[k];
    }
    return out;
}

Listing GitSource::list(const std::string& revision, const Scope& scope) const
{
    const auto spec = pathspecArgs(scope);
    if (!safe(revision) || !spec) return {};
    const auto raw = run(std::format("ls-tree -r -z {}{}", revision, *spec));
    if (!raw) return {};
    const auto entries = gp::parseTree(*raw);
    if (!entries) return {};
    Listing l;
    l.known = true;
    l.paths = gp::documentPaths(*entries);
    return l;
}

std::optional<std::string> GitSource::subject(const std::string& revision) const
{
    if (!safe(revision)) return std::nullopt;
    auto raw = run(std::format("show -s \"--format=%s\" {}", revision));
    if (!raw) return std::nullopt;
    while (!raw->empty() && (raw->back() == '\n' || raw->back() == '\r')) raw->pop_back();
    return raw;
}

std::optional<std::string> GitSource::resolve(const std::string& name) const
{
    if (!safe(name)) return std::nullopt;
    if (const auto known = resolved_.find(name); known != resolved_.end()) return known->second;   // for the life of the source: a revision named once is not asked again
    auto raw = run(std::format("rev-parse --verify --quiet \"{}^{{commit}}\"", name));   // quoted: ^ is cmd.exe's escape character
    std::optional<std::string> id;
    if (raw) {
        while (!raw->empty() && (raw->back() == '\n' || raw->back() == '\r')) raw->pop_back();
        if (!raw->empty()) id = std::move(*raw);
    }
    resolved_.emplace(name, id);
    return id;
}

}  // namespace sub0canvas::app
