#include "sub0canvas/session.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <format>

namespace sub0canvas {

namespace {

std::unexpected<SessionError> fail(std::string code, std::string message) { return std::unexpected(SessionError{std::move(code), {}, std::move(message), 0, 0}); }

std::unexpected<SessionError> quota(std::string limit, std::uint64_t current, std::uint64_t max)
{
    return std::unexpected(SessionError{"quota", limit, std::format("the session's limit of {} {} is reached ({} held)", max, limit, current), current, max});
}

std::string actorName(const WriterId& w) { return w.ns + "/" + w.writer; }

std::string joinLines(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& l : v) { s += l; s += '\n'; }
    return s;
}

std::string spanText(int from, int to)
{
    if (to < from) return std::format("after line {}", to);
    return from == to ? std::format("line {}", from) : std::format("lines {}-{}", from, to);
}

SessionLimits readLimits(simdjson::ondemand::value v, SessionLimits l)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "views") l.maxViews = fv.get_uint64();
        else if (k == "pendingBytes") l.maxPendingBytes = fv.get_uint64();
        else if (k == "subscriptions") l.maxSubscriptions = fv.get_uint64();
        else if (k == "idleTtl") l.idleTtl = fv.get_int64();
    }
    return l;
}

}  // namespace

// ---- the policy -------------------------------------------------------------------------------------------------------------------------------------------

SessionLimits DataSessionPolicy::limitsFor(const Identity& who) const
{
    const auto it = perActor.find(actorName(who.actor));
    return it == perActor.end() ? defaults : it->second;
}

std::string DataSessionPolicy::requiredCapability(std::string_view op) const
{
    const auto it = require.find(op);
    return it == require.end() ? std::string{} : it->second;
}

std::expected<DataSessionPolicy, std::string> parseSessionPolicy(std::string_view text)
{
    DataSessionPolicy p;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto fv = f.value();
                if (k == "limits") p.defaults = readLimits(fv.value(), p.defaults);
                else if (k == "actors")
                    for (auto af : fv.get_object()) {
                        const std::string actor{json::keyOf(af)};
                        p.perActor[actor] = readLimits(af.value(), p.defaults);
                    }
                else if (k == "require")
                    for (auto rf : fv.get_object()) {
                        const std::string op{json::keyOf(rf)};
                        p.require[op] = json::toString(rf.value());
                    }
                else if (k == "touchGranularity") p.granularity = fv.get_int64();
                else return;   // an unknown member is an error below
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::format("session policy: {}", simdjson::error_message(e.error())));
    }
    return p;
}

std::string anchorRevision(const ViewRecord& view) { return std::format("g{}", view.position.generation); }

// ---- the session ------------------------------------------------------------------------------------------------------------------------------------------

struct Session::Built
{
    Delta delta;
};

const SessionRecord& Session::record() const { return *ws_.views_.session(id_); }

Result<void> Session::allow(std::string_view op) const
{
    const std::string need = ws_.policy_.requiredCapability(op);
    if (!need.empty() && !who_.capabilities.contains(need)) return fail("capability", std::format("'{}' needs the capability '{}'", op, need));
    return {};
}

Result<const ViewRecord*> Session::mine(const std::string& view) const
{
    const ViewRecord* rec = ws_.views_.view(view);
    if (rec == nullptr || rec->session != id_ || rec->closed) return fail("unknown-view", std::format("this session has no open view '{}'", view));
    return rec;
}

Session::Runtime& Session::runtime(const std::string& view) const
{
    Runtime& rt = runtimes_[view];
    if (!rt.scratch) rt.scratch = std::make_unique<FileScratch>();
    return rt;
}

void Session::save(const ViewRecord& rec) const { ws_.views_.putView(rec); }

Result<void> Session::admitView() const
{
    const SessionLimits& lim = record().limits;
    std::uint64_t open = 0;
    for (const ViewRecord* v : ws_.views_.viewsOf(id_)) open += v->closed ? 0 : 1;
    if (open >= lim.maxViews) return quota("views", open, lim.maxViews);
    const std::uint64_t subs = subscriptionsOf(ws_.store_, id_).size();
    if (subs >= lim.maxSubscriptions) return quota("subscriptions", subs, lim.maxSubscriptions);
    return {};
}

std::vector<const ViewRecord*> Session::views() const
{
    std::vector<const ViewRecord*> out;
    for (const ViewRecord* v : ws_.views_.viewsOf(id_))
        if (!v->closed) out.push_back(v);
    return out;
}

Result<const ViewRecord*> Session::view(const std::string& view) const { return mine(view); }

Result<std::string> Session::openView(const OpenSpec& spec)
{
    if (auto a = allow("view.open"); !a) return std::unexpected(a.error());
    if (auto a = admitView(); !a) return std::unexpected(a.error());
    ViewRecord rec;
    rec.id = std::format("{}/v{}", id_, ws_.views_.viewsOf(id_).size() + 1);
    rec.session = id_;
    rec.owner = who_.actor;
    rec.label = spec.label;
    rec.state = spec.state;
    rec.pin = pinAtTip(ws_.canvas_, spec.scope.paths);
    rec.position = currentPosition(ws_.canvas_, ws_.store_, ws_.index_);
    rec.scope = spec.scope;
    rec.detail = spec.detail;
    rec.budget = spec.budget;
    rec.compactBy = spec.compactBy;
    rec.notify = spec.notify;
    if (auto s = sub0canvas::subscribe(ws_.store_, viewSub(rec.id), id_, rec.scope, ws_.store_.head()); !s) return fail("bad-request", s.error());
    save(rec);
    return rec.id;
}

Result<void> Session::closeView(const std::string& view)
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    ViewRecord r = **rec;
    r.closed = true;
    (void)unsubscribe(ws_.store_, viewSub(view));
    save(r);
    runtimes_.erase(view);
    return {};
}

// ---- reads ------------------------------------------------------------------------------------------------------------------------------------------------

Result<ReadResult> Session::read(const std::string& view, const std::string& path, const ReadOptions& options) const
{
    if (auto a = allow("view.read"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    const ViewRecord& r = **rec;
    if (!inScope(r.scope.paths, path)) return fail("out-of-scope", std::format("'{}' is outside the scope of view '{}'", path, view));
    ReadResult out;
    out.path = path;
    if (r.state == PinState::unpinned) {
        if (!ws_.canvas_.has(path)) return fail("not-found", std::format("no file '{}'", path));
        out.text = ws_.canvas_.text(path);
        out.revision = ws_.canvas_.revision(path);
        out.labelled = out.text;
        return out;
    }
    const auto base = textAtPin(ws_.canvas_, r.pin, path);
    Runtime& rt = runtime(view);
    const auto pending = options.overlay ? rt.scratch->overlay(path) : std::nullopt;
    if (!base && !pending) return fail("not-found", std::format("no file '{}' at the pin of view '{}'", path, view));
    out.revision = r.pin.revisionOf(path);
    out.text = pending ? *pending : *base;
    if (pending) {
        const auto hunks = diffLines(base ? *base : std::string{}, *pending);
        std::string label;
        for (const LineHunk& h : hunks) {
            if (h.newTo < h.newFrom) continue;
            label += (label.empty() ? "" : ", ") + (h.newFrom == h.newTo ? std::to_string(h.newFrom) : std::format("{}-{}", h.newFrom, h.newTo));
        }
        out.overlaid = !label.empty();
        if (out.overlaid) out.labelled = std::format("[pending: lines {}]\n{}", label, out.text);
    }
    if (!out.overlaid) out.labelled = out.text;
    return out;
}

Result<std::vector<Event>> Session::events(const std::string& view) const
{
    if (auto a = allow("view.read"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    const std::uint64_t bound = (*rec)->state == PinState::pinned ? (*rec)->position.storeHead : ws_.store_.head();
    std::vector<Event> out;
    for (const Event& e : ws_.store_.events()) {
        if (e.lamport > bound) break;
        if (!isViewBookkeeping(e.kind)) out.push_back(e);
    }
    return out;
}

// ---- pending work -----------------------------------------------------------------------------------------------------------------------------------------

std::uint64_t Session::pendingBytesOf(const ViewRecord& rec, const ScratchLayer& scratch) const
{
    std::uint64_t bytes = 0;
    for (const std::string& path : scratch.paths()) {
        const auto base = textAtPin(ws_.canvas_, rec.pin, path);
        const std::string overlay = scratch.overlay(path).value_or("");
        if (!base) { bytes += overlay.size(); continue; }
        for (const LineHunk& h : diffLines(*base, overlay)) {
            bytes += 16;
            for (const std::string& l : h.after) bytes += l.size() + 1;
        }
    }
    return bytes;
}

std::uint64_t Session::pendingBytesTotal(const std::string& except, std::uint64_t replacement) const
{
    std::uint64_t total = replacement;
    for (const auto& [id, rt] : runtimes_) {
        if (id == except || !rt.scratch) continue;
        const ViewRecord* rec = ws_.views_.view(id);
        if (rec != nullptr && !rec->closed) total += pendingBytesOf(*rec, *rt.scratch);
    }
    return total;
}

std::vector<Overlap> Session::overlapsOf(const ViewRecord& rec, const ScratchLayer& scratch) const
{
    std::vector<Overlap> out;
    for (const std::string& path : scratch.paths()) {
        const auto base = textAtPin(ws_.canvas_, rec.pin, path);
        if (!base) continue;
        for (const LineHunk& h : diffLines(*base, scratch.overlay(path).value_or(""))) {
            if (const auto c = conflictSince(ws_.canvas_, path, rec.pin.revisionOf(path), h.baseFrom, h.baseTo))
                out.push_back({path, h.baseFrom, h.baseTo, c->by, c->revision});
        }
    }
    return out;
}

Result<StageResult> Session::stage(const std::string& view, const std::string& reply, const StageOptions& options)
{
    if (auto a = allow("edit"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    const ViewRecord& r = **rec;
    if (r.state != PinState::pinned) return fail("unpinned", std::format("view '{}' follows the tip: edits go through a pinned view", view));
    if (options.allowed.empty()) return fail("not-allowed", "no file is named as writable for this edit");
    const CanvasReply parsed = parseCanvasReply(reply);
    if (parsed.error.empty() && !parsed.hasEdits()) return fail("stage-refused", "the reply holds no edit blocks");
    Runtime& rt = runtime(view);
    auto trial = rt.scratch->clone();
    ApplyOptions ao;
    ao.allowed.insert(options.allowed.begin(), options.allowed.end());
    ao.by = actorName(who_.actor);
    ao.partial = options.partial;
    auto applied = trial->stage(parsed, ao, [&](const std::string& path) { return textAtPin(ws_.canvas_, r.pin, path); });
    if (!applied) return fail("stage-refused", applied.error());
    for (const std::string& path : trial->paths()) {   // a file edited back to its base holds nothing pending
        const auto base = textAtPin(ws_.canvas_, r.pin, path);
        if (base && diffLines(*base, trial->overlay(path).value_or("")).empty()) trial->set(path, std::nullopt);
    }
    const std::uint64_t bytes = pendingBytesOf(r, *trial);
    const std::uint64_t total = pendingBytesTotal(view, bytes);
    if (total > record().limits.maxPendingBytes) return quota("pendingBytes", total, record().limits.maxPendingBytes);
    rt.scratch = std::move(trial);
    StageResult out;
    out.skipped = std::move(applied->skipped);
    out.pendingBytes = total;
    out.overlaps = overlapsOf(r, *rt.scratch);
    return out;
}

Result<void> Session::discard(const std::string& view)
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    runtime(view).scratch = std::make_unique<FileScratch>();
    return {};
}

Result<PendingSummary> Session::pending(const std::string& view) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    const Runtime& rt = runtime(view);
    PendingSummary s;
    s.bytes = pendingBytesOf(**rec, *rt.scratch);
    for (const std::string& path : rt.scratch->paths()) {
        ++s.files;
        const auto base = textAtPin(ws_.canvas_, (*rec)->pin, path);
        s.hunks += base ? diffLines(*base, rt.scratch->overlay(path).value_or("")).size() : 1;
    }
    return s;
}

Result<std::vector<Refusal>> Session::kept(const std::string& view) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    return runtime(view).kept;
}

LandReport Session::landImpl(const ViewRecord& rec, Runtime& rt, bool atomic)
{
    struct Work
    {
        std::string path, overlay;
        int baseRev = 0;
        std::optional<std::string> base;
        std::vector<LineHunk> hunks;
        std::vector<std::optional<Conflict>> conflicts;   ///< per hunk, by the canvas's own rule against the log as it is now
        bool blocked = false;                              ///< a file someone else created after the pin: never overwritten
    };
    Canvas& canvas = ws_.canvas_;
    LandReport rep;
    std::vector<Work> works;
    for (const std::string& path : rt.scratch->paths()) {
        Work w;
        w.path = path;
        w.overlay = rt.scratch->overlay(path).value_or("");
        w.baseRev = rec.pin.revisionOf(path);
        w.base = textAtPin(canvas, rec.pin, path);
        if (w.base) {
            w.hunks = diffLines(*w.base, w.overlay);
            for (const LineHunk& h : w.hunks) w.conflicts.push_back(conflictSince(canvas, path, w.baseRev, h.baseFrom, h.baseTo));
        } else if (canvas.has(path)) {
            w.blocked = true;
        }
        works.push_back(std::move(w));
    }

    const auto refusalOf = [&](const Work& w, const LineHunk& h, const Conflict& c) {
        return Refusal{w.path, h.baseFrom, h.baseTo, c.by, c.revision,
                       std::format("{}: {} were changed by {} in revision {} ({}): read the current text and redo this edit", w.path, spanText(h.baseFrom, h.baseTo),
                                   c.by.empty() ? "another edit" : c.by, c.revision, spanText(c.from, c.to)),
                       joinLines(h.after)};
    };
    std::vector<Refusal> predicted;
    for (const Work& w : works) {
        if (w.blocked) {
            predicted.push_back({w.path, 0, 0, {}, canvas.revision(w.path), std::format("{}: the file was created by someone else after this view's pin", w.path), w.overlay});
            continue;
        }
        for (std::size_t i = 0; i < w.hunks.size(); ++i)
            if (w.conflicts[i]) predicted.push_back(refusalOf(w, w.hunks[i], *w.conflicts[i]));
    }

    if (atomic && !predicted.empty()) {   // the canvas would refuse: nothing is applied, and nothing was touched to find out
        rep.refused = std::move(predicted);
        rep.rolledBack = true;
        return rep;
    }

    std::map<int, std::vector<const Work*>> groups;
    for (const Work& w : works)
        if (!w.blocked) groups[w.base ? w.baseRev : 0].push_back(&w);
    std::optional<Canvas> backup;
    std::vector<Refusal> extra;
    if (atomic && groups.size() > 1) backup = canvas;
    const std::string by = actorName(who_.actor);
    for (const auto& [rev, members] : groups) {
        CanvasReply reply;
        ApplyOptions ao;
        ao.by = by;
        ao.partial = !atomic;
        ao.baseRevision = rev;
        std::size_t expectedSkips = 0;
        for (const Work* w : members) {
            ao.allowed.insert(w->path);
            if (!w->base) { reply.wholeFiles[w->path] = w->overlay; continue; }
            if (w->hunks.empty()) continue;
            Transaction tx;
            tx.path = w->path;
            tx.by = by;
            for (std::size_t i = 0; i < w->hunks.size(); ++i) {
                const LineHunk& h = w->hunks[i];
                CanvasOp op;
                if (h.baseTo >= h.baseFrom) { op.kind = CanvasOp::Kind::replaceLines; op.from = h.baseFrom; op.to = h.baseTo; }
                else { op.kind = CanvasOp::Kind::insertAfter; op.from = h.baseFrom - 1; }
                op.text = joinLines(h.after);
                tx.ops.push_back(std::move(op));
                expectedSkips += w->conflicts[i] ? 1 : 0;
            }
            reply.transactions.push_back(std::move(tx));
        }
        if (reply.transactions.empty() && reply.wholeFiles.empty()) continue;
        auto applied = applyReply(canvas, reply, ao);
        if (!applied) {
            rep.refused.push_back({{}, 0, 0, {}, 0, applied.error(), {}});
            if (atomic) {
                if (backup) canvas = std::move(*backup);
                rep.rolledBack = true;
                return rep;
            }
            continue;
        }
        if (applied->skipped.size() != expectedSkips)   // the canvas disagreed with the prediction: say what it said
            for (const std::string& s : applied->skipped) extra.push_back({{}, 0, 0, {}, 0, s, {}});
    }

    // The books: what landed, what was refused, and what stays pending.
    for (const Refusal& r : predicted) rep.refused.push_back(r);
    for (const Refusal& r : extra) rep.refused.push_back(r);
    for (const Work& w : works) {
        if (w.blocked) continue;
        std::vector<LineHunk> left;
        for (std::size_t i = 0; i < w.hunks.size(); ++i) {
            if (w.conflicts[i]) left.push_back(w.hunks[i]);
            else ++rep.applied;
        }
        if (!w.base) ++rep.applied;
        if (!w.base || left.size() < w.hunks.size()) rep.revisions[w.path] = canvas.revision(w.path);
        if (left.empty()) rt.scratch->set(w.path, std::nullopt);
        else rt.scratch->set(w.path, applyHunks(*w.base, left));
    }
    return rep;
}

Result<LandReport> Session::land(const std::string& view, const LandOptions& options)
{
    if (auto a = allow("edit"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->state != PinState::pinned) return fail("unpinned", std::format("view '{}' follows the tip and holds no pending work", view));
    return landImpl(**rec, runtime(view), options.atomic);
}

// ---- since, advance, fork, unpin --------------------------------------------------------------------------------------------------------------------------

Result<Session::Built> Session::build(const ViewRecord& rec, bool steps) const
{
    auto in = ws_.reader_->inbox(viewSub(rec.id));
    if (!in) return fail("unknown-subscription", in.error());
    DeltaSources src;
    src.canvas = &ws_.canvas_;
    src.store = &ws_.store_;
    src.index = ws_.index_;
    src.inbox = std::move(*in);
    return Built{makeDelta(src, rec.pin, rec.position, rec.scope.paths, steps)};
}

Result<RenderedDelta> Session::since(const std::string& view) const
{
    if (auto a = allow("view.read"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    auto built = build(**rec, false);
    if (!built) return std::unexpected(built.error());
    if ((*rec)->state == PinState::pinned) built->delta.overlaps = overlapsOf(**rec, *runtime(view).scratch);
    return renderDelta(built->delta, DeltaForm::brief, (*rec)->budget, ws_.compactor_, {(*rec)->compactBy});
}

std::vector<AnchorChange> Session::reanchor(ViewRecord& rec, const std::set<std::string>& extraPaths) const
{
    std::vector<AnchorChange> out;
    if (rec.held.empty()) return out;
    std::map<std::string, std::string> files;
    const auto add = [&](const std::string& path) {
        if (auto t = textAtPin(ws_.canvas_, rec.pin, path)) files[path] = std::move(*t);
    };
    for (const HeldAnchor& h : rec.held) add(h.anchor.path);
    for (const std::string& p : extraPaths) add(p);
    const BlockIndex& index = ws_.index_ != nullptr ? *ws_.index_ : ws_.emptyIndex_;
    const CanvasDocument doc(index, files, anchorRevision(rec));
    AnchorResolver resolver(doc);
    for (HeldAnchor& h : rec.held) {
        const Resolution res = resolver.resolve(h.anchor);
        h.status = res.outcome == Outcome::orphaned ? "orphaned" : (res.outcome == Outcome::resolved && res.editable ? "editable" : "moved");
        if (h.status != "editable") out.push_back({h.key, h.status, std::string{toString(res.outcome)}, std::string{toString(res.reason)}, res.path, res.line, res.confidence});
    }
    return out;
}

Result<AdvanceReport> Session::advance(const std::string& view, const AdvanceOptions& options)
{
    if (auto a = allow("advance"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->state != PinState::pinned) return fail("unpinned", std::format("view '{}' follows the tip: refresh it instead", view));
    Runtime& rt = runtime(view);
    AdvanceReport rep;
    rep.land = landImpl(**rec, rt, options.atomic);
    if (rep.land.rolledBack) {   // atomic refusal: the view, the canvas and the pending edits are as they were
        rep.pin = (*rec)->pin;
        return rep;
    }
    rt.kept = rep.land.refused;
    for (const std::string& path : rt.scratch->paths()) rt.scratch->set(path, std::nullopt);
    ViewRecord r = **rec;
    auto built = build(r, r.detail == DeltaForm::full);
    if (!built) return std::unexpected(built.error());
    rep.delta = renderDelta(built->delta, r.detail, r.budget, ws_.compactor_, {r.compactBy});
    std::set<std::string> changed;
    for (const FileDelta& f : built->delta.files) changed.insert(f.path);
    r.pin = pinAtTip(ws_.canvas_, r.scope.paths);
    r.position = built->delta.to;
    rep.anchors = reanchor(r, changed);
    (void)sub0canvas::acknowledge(ws_.store_, viewSub(view), built->delta.to.storeHead);
    save(r);
    rep.pin = r.pin;
    rep.moved = true;
    return rep;
}

Result<ForkResult> Session::fork(const std::string& view, const ForkOptions& options)
{
    if (auto a = allow("view.open"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->state != PinState::pinned) return fail("unpinned", std::format("view '{}' follows the tip: only a pinned view is forked", view));
    if (auto a = admitView(); !a) return std::unexpected(a.error());
    ViewRecord child = **rec;
    child.id = std::format("{}/v{}", id_, ws_.views_.viewsOf(id_).size() + 1);
    child.parent = view;
    child.label = options.label.empty() ? (*rec)->label + " (fork)" : options.label;
    child.fingerprint = pinFingerprint(ws_.canvas_, child.pin, child.scope.paths);
    ForkResult out;
    out.id = child.id;
    std::size_t n = 0;
    for (HeldAnchor& h : child.held) {   // addresses issued to one branch are refused in the other: the fork's own are new
        const std::string fresh = std::format("{}#{}", child.id, ++n);
        out.addresses[h.key] = fresh;
        h.key = fresh;
    }
    const Subscription* parentSub = findSubscription(ws_.store_, viewSub(view));
    if (auto s = sub0canvas::subscribe(ws_.store_, viewSub(child.id), id_, child.scope, parentSub != nullptr ? parentSub->cursor : ws_.store_.head()); !s)
        return fail("bad-request", s.error());
    save(child);
    Runtime& rt = runtime(child.id);
    if (options.withPending) rt.scratch = runtime(view).scratch->clone();
    return out;
}

Result<FingerprintCheck> Session::checkFingerprint(const std::string& view) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->fingerprint.empty()) return fail("bad-request", std::format("view '{}' is not a fork: it recorded no fingerprint", view));
    FingerprintCheck c;
    c.recorded = (*rec)->fingerprint;
    c.actual = pinFingerprint(ws_.canvas_, (*rec)->pin, (*rec)->scope.paths);
    c.ok = c.recorded == c.actual;
    return c;
}

Result<void> Session::unpin(const std::string& view)
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->state == PinState::unpinned) return {};
    if (!runtime(view).scratch->paths().empty()) return fail("pending-work", std::format("view '{}' has pending edits: land or discard them first", view));
    ViewRecord r = **rec;
    r.state = PinState::unpinned;
    save(r);
    return {};
}

// ---- tip views --------------------------------------------------------------------------------------------------------------------------------------------

Result<RenderedDelta> Session::refresh(const std::string& view, const RefreshOptions& options)
{
    if (auto a = allow("view.read"); !a) return std::unexpected(a.error());
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    ViewRecord r = **rec;
    if (r.state == PinState::pinned)
        return fail("pinned", std::format("view '{}' is pinned at generation {}: use since to learn what moved, advance to move it", view, r.position.generation));
    const DeltaForm form = options.form.value_or(r.detail);
    const DeltaBudget budget = options.budget.value_or(r.budget);
    auto built = build(r, form == DeltaForm::full);
    if (!built) return std::unexpected(built.error());
    RenderedDelta out = renderDelta(built->delta, form, budget, ws_.compactor_, {r.compactBy});
    if (options.acknowledge) {
        (void)sub0canvas::acknowledge(ws_.store_, viewSub(view), built->delta.to.storeHead);
        r.pin = pinAtTip(ws_.canvas_, r.scope.paths);
        r.position = built->delta.to;
        save(r);
    }
    return out;
}

Result<bool> Session::isStale(const std::string& view) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->notify != Notify::flag) return fail("notify-off", std::format("view '{}' asked for no stale flag (notify=none)", view));
    if (currentPosition(ws_.canvas_, ws_.store_, ws_.index_).generation != (*rec)->position.generation) return true;
    auto built = build(**rec, false);
    if (!built) return std::unexpected(built.error());
    return !built->delta.events.empty();
}

Result<void> Session::openChannel(const std::string& view) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    if ((*rec)->state == PinState::pinned)
        return fail("pinned", std::format("view '{}' is pinned at generation {}: a pinned view receives no channel", view, (*rec)->position.generation));
    return fail("not-supported", "this workspace has no live channel transport (VIEWS.md R-PIN-8)");
}

// ---- held anchors -----------------------------------------------------------------------------------------------------------------------------------------

Result<std::string> Session::hold(const std::string& view, const SourceAnchor& anchor)
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    ViewRecord r = **rec;
    HeldAnchor h;
    h.key = std::format("{}#{}", view, r.held.size() + 1);
    h.anchor = anchor;
    r.held.push_back(h);
    save(r);
    return h.key;
}

Result<HeldAnchor> Session::resolve(const std::string& view, const std::string& address) const
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    const std::string issuer = address.substr(0, address.find('#'));
    if (issuer != view) {
        const ViewRecord* other = ws_.views_.view(issuer);
        if (other != nullptr && other->session == id_)
            return fail("foreign-address", std::format("address '{}' was issued to view '{}', another branch: this view's own addresses are its own", address, issuer));
        return fail("unknown-address", std::format("no address '{}' in view '{}'", address, view));
    }
    for (const HeldAnchor& h : (*rec)->held) {
        if (h.key != address) continue;
        if (h.status != "editable") return fail("not-editable", std::format("address '{}' is {}: re-read it before editing", address, h.status));
        return h;
    }
    return fail("unknown-address", std::format("no address '{}' in view '{}'", address, view));
}

Result<HeldAnchor> Session::reread(const std::string& view, const std::string& address)
{
    auto rec = mine(view);
    if (!rec) return std::unexpected(rec.error());
    ViewRecord r = **rec;
    const auto it = std::find_if(r.held.begin(), r.held.end(), [&](const HeldAnchor& h) { return h.key == address; });
    if (it == r.held.end()) return fail("unknown-address", std::format("no address '{}' in view '{}'", address, view));
    std::map<std::string, std::string> files;
    if (auto t = textAtPin(ws_.canvas_, r.pin, it->anchor.path)) files[it->anchor.path] = *t;
    const BlockIndex& index = ws_.index_ != nullptr ? *ws_.index_ : ws_.emptyIndex_;
    const CanvasDocument doc(index, files, anchorRevision(r));
    const Resolution res = AnchorResolver(doc).resolve(it->anchor);
    if (res.outcome == Outcome::orphaned) return fail("orphaned", std::format("address '{}' resolves to nothing: {}", address, res.detail));
    const std::string& text = files[res.path.empty() ? it->anchor.path : res.path];
    const int endLine = res.endLine != 0 ? res.endLine : res.line;
    const int endColumn = res.endLine != 0 ? res.endColumn : res.column + static_cast<int>(it->anchor.quote.size());
    it->anchor = makeSourceAnchor(it->anchor.path, anchorRevision(r), text, res.line, res.column, endLine, endColumn, ws_.index_);
    it->status = "editable";
    const HeldAnchor out = *it;
    save(r);
    return out;
}

// ---- subscriptions ----------------------------------------------------------------------------------------------------------------------------------------

Result<std::string> Session::subscribe(const SubscriptionQuery& query)
{
    if (auto a = allow("subscribe"); !a) return std::unexpected(a.error());
    const std::uint64_t subs = subscriptionsOf(ws_.store_, id_).size();
    if (subs >= record().limits.maxSubscriptions) return quota("subscriptions", subs, record().limits.maxSubscriptions);
    std::size_t free = 0;
    for (const Subscription* s : subscriptionsOf(ws_.store_, id_)) free += s->id.starts_with(id_ + "/s") ? 1 : 0;
    const std::string id = std::format("{}/s{}", id_, free + 1);
    if (auto s = sub0canvas::subscribe(ws_.store_, id, id_, query, ws_.store_.head()); !s) return fail("bad-request", s.error());
    return id;
}

Result<Inbox> Session::inbox(const std::string& subscription) const
{
    const Subscription* sub = findSubscription(ws_.store_, subscription);
    if (sub == nullptr || sub->owner != id_) return fail("unknown-subscription", std::format("this session has no subscription '{}'", subscription));
    auto in = ws_.reader_->inbox(subscription);
    if (!in) return fail("unknown-subscription", in.error());
    std::erase_if(in->events, [](const Event& e) { return isViewBookkeeping(e.kind); });
    return std::move(*in);
}

Result<void> Session::acknowledge(const std::string& subscription, std::uint64_t upTo)
{
    const Subscription* sub = findSubscription(ws_.store_, subscription);
    if (sub == nullptr || sub->owner != id_) return fail("unknown-subscription", std::format("this session has no subscription '{}'", subscription));
    if (auto r = sub0canvas::acknowledge(ws_.store_, subscription, upTo); !r) return fail("bad-request", r.error());
    return {};
}

// ---- the workspace ----------------------------------------------------------------------------------------------------------------------------------------

Workspace::Workspace(Canvas& canvas, Store& store, ViewStore& views, const SessionPolicy& policy, const Compactor& compactor, const BlockIndex* index)
    : canvas_(canvas), store_(store), views_(views), policy_(policy), compactor_(compactor), index_(index), reader_(std::make_unique<InboxReader>(store))
{
}

Workspace::~Workspace() = default;

Result<std::string> Workspace::open(const Identity& who, Instant now)
{
    SessionRecord rec;
    rec.id = std::format("s{}", views_.sessions().size() + 1);
    rec.actor = who.actor;
    rec.limits = policy_.limitsFor(who);
    rec.created = rec.lastSeen = now;
    views_.putSession(rec);
    if (views_.session(rec.id) == nullptr) return fail("bad-request", "the store has no view reducers: call registerViewReducers where it is made");
    auto session = std::unique_ptr<Session>(new Session(*this, rec.id, who));
    session->lastSeenLive_ = now;
    sessions_[rec.id] = std::move(session);
    return rec.id;
}

void Workspace::finish(const std::string& id, Instant now, const std::string& reason)
{
    for (const ViewRecord* v : views_.viewsOf(id)) {
        if (v->closed) continue;
        ViewRecord r = *v;
        r.closed = true;
        (void)unsubscribe(store_, "view:" + r.id);
        views_.putView(r);
    }
    std::vector<std::string> subs;
    for (const Subscription* s : subscriptionsOf(store_, id)) subs.push_back(s->id);
    for (const std::string& s : subs) (void)unsubscribe(store_, s);
    if (const SessionRecord* rec = views_.session(id)) {
        SessionRecord r = *rec;
        r.closed = true;
        r.closeReason = reason;
        r.lastSeen = std::max(r.lastSeen, now);
        views_.putSession(r);
    }
    sessions_.erase(id);
}

Result<Session*> Workspace::attach(std::string_view id, const Identity& who, Instant now)
{
    const SessionRecord* rec = views_.session(id);
    if (rec == nullptr) return fail("unknown-session", std::format("no session '{}'", id));
    if (rec->actor != who.actor) return fail("not-your-session", std::format("session '{}' belongs to another actor", id));
    if (rec->closed) return fail(rec->closeReason == "expired" ? "expired" : "closed", std::format("session '{}' is {}", id, rec->closeReason));
    const auto live = sessions_.find(id);
    const Instant last = live != sessions_.end() ? std::max(rec->lastSeen, live->second->lastSeenLive_) : rec->lastSeen;
    if (now - last > rec->limits.idleTtl) {
        finish(std::string{id}, now, "expired");
        return fail("expired", std::format("session '{}' was idle past its limit of {} s", id, rec->limits.idleTtl));
    }
    Session* session = nullptr;
    if (live == sessions_.end()) {   // known to the log only (a restarted process): same views and positions, no pending edits
        auto made = std::unique_ptr<Session>(new Session(*this, std::string{id}, who));
        session = made.get();
        sessions_[std::string{id}] = std::move(made);
    } else session = live->second.get();
    session->who_ = who;
    session->lastSeenLive_ = std::max(last, now);
    if (now - rec->lastSeen >= policy_.touchGranularity()) {
        SessionRecord r = *rec;
        r.lastSeen = now;
        views_.putSession(r);
    }
    return session;
}

Result<void> Workspace::close(std::string_view id, const Identity& who, Instant now)
{
    auto s = attach(id, who, now);
    if (!s) return std::unexpected(s.error());
    finish(std::string{id}, now, "closed");
    return {};
}

std::size_t Workspace::sweep(Instant now)
{
    std::vector<std::string> due;
    for (const SessionRecord* rec : views_.sessions()) {
        if (rec->closed) continue;
        const auto live = sessions_.find(rec->id);
        const Instant last = live != sessions_.end() ? std::max(rec->lastSeen, live->second->lastSeenLive_) : rec->lastSeen;
        if (now - last > rec->limits.idleTtl) due.push_back(rec->id);
    }
    for (const std::string& id : due) finish(id, now, "expired");
    return due.size();
}

}  // namespace sub0canvas
