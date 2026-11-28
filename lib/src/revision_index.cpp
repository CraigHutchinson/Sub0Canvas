// The revision-aware index (revision_index.hpp): per-scope replay of a DocumentSource's first-parent history through a BlockIndex, with checkpoints.
#include "sub0canvas/revision_index.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace sub0canvas {

namespace {

Scope normalised(const Scope& scope)
{
    Scope out = scope;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string keyOf(const Scope& scope)
{
    std::string key;
    for (const auto& s : scope) { key += s; key += '\n'; }
    return key;
}

/// The last component of a qualified name (`Parser::term` -> `term`).
std::string_view leafOf(std::string_view name)
{
    const auto c = name.rfind(':');
    return c == std::string_view::npos ? name : name.substr(c + 1);
}

/// A binary file has no blocks (docs/SCALE.md section 3, "kinds before blocks"): its text is indexed as empty.
bool isBinary(const std::string& text) { return text.contains('\0'); }

}  // namespace

RevisionIndex::RevisionIndex(const DocumentSource& source, Options options) : source_(source), options_(options)
{
    options_.checkpointEvery = std::max<std::size_t>(options_.checkpointEvery, 1);
}

namespace {

/// If `commit` adds files, asks the source for the revision's whole diff (a pathspec-limited log cannot see a rename from outside its pathspec) and turns each
/// added file that git sees as a rename into one. Returns the old names that lie outside `scope`: the scope must grow to follow the file backwards.
Scope renameOrigins(const DocumentSource& source, Commit& commit, const Scope& scope, std::size_t& changesCalls)
{
    const bool adds = std::any_of(commit.changes.begin(), commit.changes.end(), [](const PathChange& c) { return c.kind == ChangeKind::added; });
    if (!adds || commit.info.parents.empty()) return {};
    ++changesCalls;
    const auto diff = source.changes(commit.info.parents.front(), commit.info.id);
    if (!diff) return {};   // unknown: the file stays an addition, which is all that is known
    Scope origins;
    for (PathChange& added : commit.changes) {
        if (added.kind != ChangeKind::added) continue;
        for (const PathChange& d : *diff) {
            if (d.kind != ChangeKind::renamed || d.path != added.path) continue;
            added.kind = ChangeKind::renamed;
            added.from = d.from;
            added.similarity = d.similarity;
            if (!inScope(scope, d.from)) origins.push_back(d.from);
            break;
        }
    }
    return origins;
}

/// Indexes one file's text: unknown keeps the last known blocks and is recorded, binary and non-documents are indexed as empty.
void applyText(BlockIndex& index, std::map<std::string, std::size_t>& sizes, std::size_t& updates, const std::string& path, const FileText& text)
{
    const std::string empty;
    const std::string& body = text.presence == Presence::present && !isBinary(text.text) ? text.text : empty;
    index.update(path, body);
    ++updates;
    if (body.empty()) sizes.erase(path);
    else sizes[path] = body.size();
}

}  // namespace

RevisionIndex::Line& RevisionIndex::buildLine(const Scope& scope, const std::string& head)
{
    Line line;
    line.scope = scope;
    line.extended = scope;
    std::vector<Commit> newest;   // newest first, across every segment of a rename chase
    std::string to = head;
    const std::size_t limit = options_.maxRevisions;
    while (limit == 0 || newest.size() < limit) {
        History hist = source_.history(to, line.extended, limit == 0 ? 0 : limit - newest.size());
        ++stats_.historyCalls;
        line.cut = hist.cut;
        bool chased = false;
        for (std::size_t i = 0; i < hist.commits.size() && !chased; ++i) {
            Commit& c = hist.commits[i];
            if (options_.followRenames) {
                Scope origins = renameOrigins(source_, c, line.extended, stats_.changesCalls);
                if (!origins.empty()) {
                    line.extended.insert(line.extended.end(), origins.begin(), origins.end());
                    line.extended = normalised(line.extended);
                    chased = true;
                }
            }
            newest.push_back(std::move(c));
            if (chased) to = newest.back().info.parents.front();   // renameOrigins only reports for a revision that has a parent
        }
        if (!chased) break;
    }
    std::reverse(newest.begin(), newest.end());
    line.commits = std::move(newest);
    if (limit != 0 && line.commits.size() >= limit && !line.commits.front().info.parents.empty()) line.baseRevision = line.commits.front().info.parents.front();
    auto& slot = lines_[keyOf(scope)];
    // A head further along a line already replayed (the same revisions up to the old head, the same baseline) extends that line: its states, checkpoints and ids
    // stay, so ids from before and after remain comparable. (Fetching the history again from the new head costs the line's length once; a source that takes
    // a stopping revision would cut that, and nothing else changes.)
    for (Line& existing : slot) {
        if (existing.baseRevision != line.baseRevision || existing.commits.size() > line.commits.size()) continue;
        if (!std::equal(existing.commits.begin(), existing.commits.end(), line.commits.begin(),
                        [](const Commit& a, const Commit& b) { return a.info.id == b.info.id; }))
            continue;
        for (std::size_t i = existing.commits.size(); i < line.commits.size(); ++i) existing.ordinalOf.emplace(line.commits[i].info.id, static_cast<int>(i) + 1);
        existing.commits = std::move(line.commits);
        existing.extended.insert(existing.extended.end(), line.extended.begin(), line.extended.end());
        existing.extended = normalised(existing.extended);
        existing.cut = line.cut;
        return existing;
    }
    for (std::size_t i = 0; i < line.commits.size(); ++i) line.ordinalOf.emplace(line.commits[i].info.id, static_cast<int>(i) + 1);

    line.frontier = std::make_shared<State>();
    if (options_.languages) line.frontier->view.index = BlockIndex(options_.languages);
    line.frontier->view.historyCut = line.cut;
    line.frontier->view.revision = line.baseRevision;
    applyBaseline(line, *line.frontier);
    line.checkpoints[0] = line.frontier;
    ++stats_.checkpoints;
    ++stats_.linesBuilt;
    slot.push_back(std::move(line));
    return slot.back();
}

void RevisionIndex::applyBaseline(Line& line, State& state)
{
    if (line.baseRevision.empty()) return;
    const Listing listing = source_.list(line.baseRevision, line.extended);
    if (!listing.known) {
        state.view.unknown.push_back({line.baseRevision, {}});
        return;
    }
    std::vector<FileRef> refs;
    for (const auto& path : listing.paths) refs.push_back({line.baseRevision, path});
    if (refs.empty()) return;
    const std::vector<FileText> texts = source_.read(refs);
    ++stats_.readCalls;
    stats_.filesRead += refs.size();
    state.view.index.setTurn(0);
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (i < texts.size() && texts[i].presence != Presence::unknown) applyText(state.view.index, state.sizes, stats_.updates, refs[i].path, texts[i]);
        else state.view.unknown.push_back({line.baseRevision, refs[i].path});
    }
}

namespace {

/// One revision's changes into `state`, `texts` holding the answers for its non-deleted changes from `cursor` on, in order. Order matters for identity: removals
/// and renames' old names first (so their blocks are retired), then the other files by growth, smallest first, so a function moved from a file that shrinks
/// to one that grows is retired before the revive rule looks for it.
void applyCommit(BlockIndex& index, std::map<std::string, std::size_t>& sizes, std::vector<Unknown>& unknown, std::size_t& updates, const Commit& commit,
                 int ordinal, const std::vector<FileText>& texts, std::size_t& cursor)
{
    index.setTurn(ordinal);
    struct Item
    {
        const PathChange* change;
        const FileText* text;   // null for a deletion
    };
    std::vector<Item> items;
    for (const PathChange& c : commit.changes) items.push_back({&c, c.kind == ChangeKind::deleted ? nullptr : &texts[cursor++]});
    for (const Item& it : items) {
        const bool gone = it.change->kind == ChangeKind::deleted;
        const bool renamedAway = it.change->kind == ChangeKind::renamed && it.text->presence != Presence::unknown;
        if (!gone && !renamedAway) continue;
        const std::string& old = gone ? it.change->path : it.change->from;
        index.update(old, {});
        ++updates;
        sizes.erase(old);
    }
    std::vector<const Item*> todo;
    for (const Item& it : items) if (it.text != nullptr) todo.push_back(&it);
    const auto growth = [&](const Item* it) {
        const std::size_t now = it->text->presence == Presence::present && !isBinary(it->text->text) ? it->text->text.size() : 0;
        const auto before = sizes.find(it->change->path);
        return static_cast<std::ptrdiff_t>(now) - static_cast<std::ptrdiff_t>(before == sizes.end() ? 0 : before->second);
    };
    std::stable_sort(todo.begin(), todo.end(), [&](const Item* a, const Item* b) {
        const auto ga = growth(a), gb = growth(b);
        return ga != gb ? ga < gb : a->change->path < b->change->path;
    });
    for (const Item* it : todo) {
        if (it->text->presence == Presence::unknown) { unknown.push_back({commit.info.id, it->change->path}); continue; }
        applyText(index, sizes, updates, it->change->path, *it->text);
    }
}

}  // namespace

void RevisionIndex::advance(Line& line, std::shared_ptr<State>& holder, int to, bool retain)
{
    const std::size_t every = options_.checkpointEvery;
    int from = holder->view.ordinal;
    while (from < to) {
        const auto step = std::min(static_cast<std::size_t>(to - from), every - static_cast<std::size_t>(from) % every);
        const int chunkEnd = from + static_cast<int>(step);
        std::vector<FileRef> refs;
        for (int ord = from + 1; ord <= chunkEnd; ++ord) {
            const Commit& c = line.commits[static_cast<std::size_t>(ord) - 1];
            for (const PathChange& ch : c.changes) if (ch.kind != ChangeKind::deleted) refs.push_back({c.info.id, ch.path});
        }
        std::vector<FileText> texts;
        if (!refs.empty()) {
            texts = source_.read(refs);
            ++stats_.readCalls;
            stats_.filesRead += refs.size();
            texts.resize(refs.size());   // a source that answers short leaves the rest unknown (the default state)
        }
        std::size_t cursor = 0;
        for (int ord = from + 1; ord <= chunkEnd; ++ord) {
            if (holder.use_count() > 1) holder = std::make_shared<State>(*holder);   // a retained checkpoint or a caller's answer is never mutated
            State& s = *holder;
            applyCommit(s.view.index, s.sizes, s.view.unknown, stats_.updates, line.commits[static_cast<std::size_t>(ord) - 1], ord, texts, cursor);
            s.view.revision = line.commits[static_cast<std::size_t>(ord) - 1].info.id;
            s.view.ordinal = ord;
            ++stats_.commitsReplayed;
            if (retain && static_cast<std::size_t>(ord) % every == 0) {
                line.checkpoints[ord] = holder;
                ++stats_.checkpoints;
            }
        }
        from = chunkEnd;
    }
}

std::shared_ptr<const RevisionIndex::State> RevisionIndex::stateAt(Line& line, int ordinal)
{
    if (line.frontier->view.ordinal == ordinal) return line.frontier;
    if (line.frontier->view.ordinal < ordinal) {
        advance(line, line.frontier, ordinal, true);
        return line.frontier;
    }
    auto at = line.checkpoints.upper_bound(ordinal);   // the baseline (0) is always a checkpoint
    --at;
    auto copy = std::make_shared<State>(*at->second);
    advance(line, copy, ordinal, false);
    return copy;
}

RevisionIndex::Result<RevisionIndex::Located> RevisionIndex::locate(const Scope& scope, const std::string& revision)
{
    const auto id = source_.resolve(revision);
    if (!id) return std::unexpected(std::format("'{}' names no revision of this source", revision));
    const Scope norm = normalised(scope);
    auto& lines = lines_[keyOf(norm)];
    for (Line& l : lines) {
        if (const auto it = l.ordinalOf.find(*id); it != l.ordinalOf.end()) return Located{&l, it->second};
        if (const auto it = l.askedAt.find(*id); it != l.askedAt.end()) return Located{&l, it->second};
    }
    // A revision that changed nothing of the scope: the state is that of the newest ancestor on the first-parent line that did (one bounded history query).
    for (Line& l : lines) {
        const History h = source_.history(*id, l.extended, 1);
        ++stats_.historyCalls;
        if (!h.commits.empty()) {
            if (const auto it = l.ordinalOf.find(h.commits.front().info.id); it != l.ordinalOf.end()) {
                l.askedAt[*id] = it->second;
                return Located{&l, it->second};
            }
        } else if (l.baseRevision.empty()) {
            l.askedAt[*id] = 0;   // nothing of the scope in its past, and the line is not windowed: empty
            return Located{&l, 0};
        }
    }
    Line& built = buildLine(norm, *id);
    const int ordinal = static_cast<int>(built.commits.size());
    if (!built.ordinalOf.contains(*id)) built.askedAt[*id] = ordinal;   // the head did not change the scope itself: remember its answer
    return Located{&built, ordinal};
}

RevisionIndex::Result<std::shared_ptr<const Replayed>> RevisionIndex::at(const Scope& scope, const std::string& revision)
{
    const auto loc = locate(scope, revision);
    if (!loc) return std::unexpected(loc.error());
    const auto state = stateAt(*loc->line, loc->ordinal);
    return std::shared_ptr<const Replayed>(state, &state->view);
}

RevisionIndex::Result<std::vector<std::string>> RevisionIndex::named(const Scope& scope, const std::string& revision, const std::string& name, const std::string& path)
{
    const auto state = at(scope, revision);
    if (!state) return std::unexpected(state.error());
    std::vector<std::string> ids;
    const bool qualified = name.contains(':');
    for (const Block* b : (*state)->index.all()) {
        if (!path.empty() && b->path != path) continue;
        if (b->name == name || (!qualified && leafOf(b->name) == name)) ids.push_back(b->id);
    }
    return ids;
}

RevisionIndex::Result<std::vector<HistoryStep>> RevisionIndex::history(const Scope& scope, const std::string& revision, const std::string& id)
{
    const auto loc = locate(scope, revision);
    if (!loc) return std::unexpected(loc.error());
    const auto state = stateAt(*loc->line, loc->ordinal);
    std::vector<HistoryStep> steps;
    for (const BlockEvent& e : blockHistory(state->view.index, id)) steps.push_back({loc->line->revisionAt(e.turn), e.turn, e});
    return steps;
}

RevisionIndex::Result<std::optional<std::string>> RevisionIndex::carry(const Scope& scope, const std::string& from, const std::string& id, const std::string& to)
{
    const auto a = locate(scope, from);
    if (!a) return std::unexpected(a.error());
    const auto b = locate(scope, to);
    if (!b) return std::unexpected(b.error());
    if (a->line != b->line) return std::unexpected(std::string("the two revisions lie on different replay lines of the scope: ids are only comparable within one"));
    const auto atFrom = stateAt(*a->line, a->ordinal);
    const auto atTo = stateAt(*b->line, b->ordinal);
    const Block* was = atFrom->view.index.resolve(id);
    if (was == nullptr || was->id != id) return std::optional<std::string>{};   // not a live block at `from`
    const Block* now = atTo->view.index.resolve(id);
    if (now == nullptr) return std::optional<std::string>{};
    if (b->ordinal < a->ordinal && now->id != id) return std::optional<std::string>{};   // looking back, a successor is not the same block
    return std::optional<std::string>{now->id};
}

}  // namespace sub0canvas
