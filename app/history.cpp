// `sub0canvas history`: the identity history of blocks across a repository's commits (docs/BACKBONE.md section 6, "As built (G1)"), through the git DocumentSource and the
// library's RevisionIndex. Read-only and local.
//
//   sub0canvas history --repo DIR [--rev REV] [--max N] [--checkpoint N] [--no-follow] [--name NAME]... [--blocks | --blocks-binary] [--] SCOPE...
//
// SCOPE is one or more files or directories (a view is scoped, never the whole repository). Writes one JSON object to stdout: the revision, whether the history is
// cut (a shallow clone), every (revision, path) git could not supply, the replay's counters, the git commands run, and for each --name the ids of the blocks of
// that name at REV with their events, each event beside its revision and that commit's subject. `--blocks` lists the live blocks at REV.
#include "git_source.hpp"

#include <print>
#include <sub0canvas/block_table.hpp>

#include "json_util.hpp"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace sub0canvas::app {

namespace {

template <class... A> void eprintln(std::format_string<A...> f, A&&... a)
{
    std::println(stderr, f, std::forward<A>(a)...);
}

[[noreturn]] void fail(const std::string& message)
{
    eprintln("sub0canvas: {}", message);
    std::exit(2);
}

const char* carryName(Carry c)
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

struct Options
{
    std::string repo, rev = "HEAD";
    std::size_t max = 0, checkpoint = 50;
    bool follow = true, blocks = false, binary = false;
    std::vector<std::string> names;
    Scope scope;
};

Options parse(int argc, char** argv)
{
    Options o;
    bool rest = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) fail(a + " needs a value"); return argv[++i]; };
        if (rest || a.empty() || a[0] != '-') o.scope.push_back(a);
        else if (a == "--") rest = true;
        else if (a == "--repo") o.repo = next();
        else if (a == "--rev") o.rev = next();
        else if (a == "--max") o.max = std::stoul(next());
        else if (a == "--checkpoint") o.checkpoint = std::stoul(next());
        else if (a == "--no-follow") o.follow = false;
        else if (a == "--name") o.names.push_back(next());
        else if (a == "--blocks") o.blocks = true;
        else if (a == "--blocks-binary") o.binary = true;
        else fail("unknown option " + a);
    }
    if (o.binary && (o.blocks || !o.names.empty())) fail("--blocks-binary cannot be combined with --blocks or --name");
    if (o.repo.empty()) fail("history needs --repo DIR");
    if (o.scope.empty()) fail("history needs at least one path: an identity history is scoped, never the whole repository");
    // Arguments reach a shell (popen): refuse every character that could leave its quotes or chain a command, rather than escape for two shells.
    const auto requireSafe = [](const std::string& what, const std::string& value) {
        if (value.empty() || value.find_first_of("\"'`$;&|<>^%!\r\n") != std::string::npos) fail(std::format("{} contains a character this tool does not pass to git: {}", what, value));
    };
    requireSafe("--repo", o.repo);
    requireSafe("--rev", o.rev);
    for (const auto& p : o.scope) requireSafe("a path", p);
    return o;
}

}  // namespace

int historyCommand(int argc, char** argv)
{
    const Options o = parse(argc, argv);
    GitSource source(o.repo);
    RevisionIndex::Options options;
    options.checkpointEvery = o.checkpoint;
    options.maxRevisions = o.max;
    options.followRenames = o.follow;
    RevisionIndex index(source, options);

    const auto revision = source.resolve(o.rev);
    if (!revision) fail("no commit named " + o.rev);
    const auto state = index.at(o.scope, *revision);
    if (!state) fail(state.error());
    if (o.binary) {
        const auto bytes = writeBlockTable(*revision, **state);
        if (!bytes) fail(bytes.error());
        if (std::fwrite(bytes->data(), 1, bytes->size(), stdout) != bytes->size() || std::fflush(stdout) != 0)
            fail("could not write the block table");
        return 0;
    }

    std::map<std::string, std::string> subjects;   // by revision: its subject, asked once
    const auto subjectOf = [&](const std::string& id) -> const std::string& {
        auto it = subjects.find(id);
        if (it == subjects.end()) {
            std::string subject;
            if (const auto raw = source.subject(id)) subject = *raw;
            it = subjects.emplace(id, std::move(subject)).first;
        }
        return it->second;
    };

    json::Writer out;
    out.beginObject().kv("revision", *revision).kvBool("historyCut", (*state)->historyCut).key("unknown").beginArray();
    for (const Unknown& u : (*state)->unknown) out.beginObject().kv("revision", u.revision).kv("path", u.path).endObject();
    out.endArray();
    out.key("functions").beginArray();
    for (const std::string& name : o.names) {
        const auto ids = index.named(o.scope, *revision, name);
        if (!ids) fail(ids.error());
        out.beginObject().kv("name", name).key("blocks").beginArray();
        for (const std::string& id : *ids) {
            const auto steps = index.history(o.scope, *revision, id);
            if (!steps) fail(steps.error());
            out.beginObject().kv("id", id).key("events").beginArray();
            for (const HistoryStep& s : *steps) {
                out.beginObject()
                    .kv("revision", s.revision)
                    .kv("message", subjectOf(s.revision))
                    .kv("ordinal", s.ordinal)
                    .kv("carry", carryName(s.event.carry))
                    .kv("id", s.event.id)
                    .kv("path", s.event.path)
                    .kv("name", s.event.name)
                    .kv("detail", s.event.detail);
                if (s.event.carry == Carry::merged)
                    if (const Block* survivor = (*state)->index.resolve(s.event.detail)) out.kv("mergedInto", survivor->name);
                out.endObject();
            }
            out.endArray().endObject();
        }
        out.endArray().endObject();
    }
    out.endArray();
    if (o.blocks) {
        out.key("blocks").beginArray();
        for (const Block* b : (*state)->index.all())
            out.beginObject().kv("id", b->id).kv("path", b->path).kv("name", b->name).kv("first", b->first).kv("last", b->last).endObject();
        out.endArray();
    }
    const ReplayStats& st = index.stats();
    out.key("stats").beginObject()
        .kv("historyCalls", static_cast<std::int64_t>(st.historyCalls)).kv("changesCalls", static_cast<std::int64_t>(st.changesCalls))
        .kv("readCalls", static_cast<std::int64_t>(st.readCalls)).kv("filesRead", static_cast<std::int64_t>(st.filesRead))
        .kv("commitsReplayed", static_cast<std::int64_t>(st.commitsReplayed)).kv("updates", static_cast<std::int64_t>(st.updates))
        .kv("checkpoints", static_cast<std::int64_t>(st.checkpoints)).endObject();
    out.key("commands").beginArray();
    for (const std::string& c : source.commands()) out.str(c);
    out.endArray().endObject();
    const std::string text = out.take();
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

}  // namespace sub0canvas::app
