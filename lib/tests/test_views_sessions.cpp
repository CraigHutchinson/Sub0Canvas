// Pinned and unpinned views, sessions and the workspace (docs/VIEWS.md; requirements R-PIN-1..9 and the session requirements of section 7, docs/REQUIREMENTS.md).
// The expected values are written by hand from the requirements: the text of each revision, which lines each actor changed, which edit lands and which is refused
// and naming whom, the exact bytes of every refresh form on a hand-built delta, never read back from the code under test.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/session.hpp"
#include "sub0canvas/view.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

// CHECK_EQ over values copied out: the helper's reference binding dangles when the value is a member of a temporary expected.
#undef CHECK_EQ
#define CHECK_EQ(a, b) do { const auto _a = (a); const auto _b = (b); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

/// CHECK_EQ for strings that prints both sides.
#define CHECK_STR(a, b)                                                                                                  \
    do {                                                                                                                 \
        const std::string _a = (a), _b = (b);                                                                            \
        if (_a != _b) { std::fprintf(stderr, "%s:%d: CHECK_STR failed\n got: %s\nwant: %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++gFailures; } \
    } while (0)

// The one file of most scenarios: three functions, eleven lines (blocks alpha 1-3, beta 5-7, gamma 9-11).
const std::string kT1 = "int alpha() {\n    return 1;\n}\n\nint beta() {\n    return 2;\n}\n\nint gamma() {\n    return 3;\n}\n";
const std::string kT2 = "int alpha() {\n    return 10;\n}\n\nint beta() {\n    return 2;\n}\n\nint gamma() {\n    return 3;\n}\n";           // line 2 changed
const std::string kT3 = "int alpha() {\n    return 10;\n}\n\nint beta() {\n    return 2;\n}\n\nint gamma() {\n    return 30;\n}\n";          // and line 10

std::string editLines(int a, int b, const std::string& text) { return std::format("=== EDIT: a.cpp LINES {}-{} ===\n{}=== END EDIT ===\n", a, b, text); }

Identity who(const std::string& name, std::set<std::string, std::less<>> caps = {}) { return Identity{{"t", name}, std::move(caps)}; }

struct Rig
{
    Canvas canvas;
    BlockIndex index;
    MemoryLog log;
    std::unique_ptr<Store> store;
    std::unique_ptr<LogViewStore> views;
    DataSessionPolicy policy;
    BlockCompactor compactor;
    std::unique_ptr<Workspace> ws;

    explicit Rig(DataSessionPolicy p = {}) : policy(std::move(p))
    {
        StoreOptions o;
        o.writer = {"proj", "srv"};
        o.sink = &log;
        registerViewReducers(o.registry);
        store = std::make_unique<Store>(std::move(o));
        views = std::make_unique<LogViewStore>(*store);
        canvas.addFile("a.cpp", kT1);
        index.update("a.cpp", kT1);
        ws = std::make_unique<Workspace>(canvas, *store, *views, policy, compactor, &index);
    }
    /// Another actor's edit made straight on the canvas (a published change), the index fed as a consumer would.
    int edit(const std::string& by, int from, int to, const std::string& text, CanvasOp::Kind kind = CanvasOp::Kind::replaceLines)
    {
        const TxResult r = canvas.apply({"a.cpp", canvas.revision("a.cpp"), by, {CanvasOp{kind, from, to, "", text}}});
        index.update("a.cpp", canvas.text("a.cpp"));
        return r.ok ? r.revision : -1;
    }
    Session* open(const Identity& id, Instant now = 0)
    {
        auto sid = ws->open(id, now);
        if (!sid) return nullptr;
        auto s = ws->attach(*sid, id, now);
        return s ? *s : nullptr;
    }
};

std::string briefOf(const std::string& delta)
{
    const auto at = delta.find("\"brief\":");
    return delta.substr(at + 8, delta.size() - 1 - (at + 8));
}

LayerItem item(const std::string& key)
{
    LayerItem it;
    it.key = key;
    it.kind = "error";
    it.title = "t";
    return it;
}

}  // namespace

int main()
{
    {   // R-PIN-1: every read through a pinned view returns the same bytes until it is re-pinned, whatever is written meanwhile (canvas and store alike)
        Rig r;
        const Identity alice = who("alice");
        Session* A = r.open(alice);
        const std::string pinned = *A->openView({});
        const std::string tip = *A->openView({.state = PinState::unpinned});
        CHECK_EQ(pinned, std::string{"s1/v1"});
        CHECK_STR(A->read(pinned, "a.cpp")->text, kT1);
        CHECK_EQ(r.edit("t/bob", 2, 2, "    return 10;\n"), 2);
        r.canvas.addFile("b.txt", "later\n");
        (void)r.store->putLayer("issues", "src", std::vector<LayerItem>{item("k1")});
        const auto again = A->read(pinned, "a.cpp");
        CHECK_STR(again->text, kT1);
        CHECK_EQ(again->revision, 1);
        CHECK(!A->read(pinned, "b.txt").has_value());                       // born after the pin: absent at it
        CHECK_EQ(A->read(pinned, "b.txt").error().code, std::string{"not-found"});
        CHECK_EQ(A->events(pinned)->size(), std::size_t{0});                // the item event is above the pinned head
        CHECK_STR(A->read(tip, "a.cpp")->text, kT2);                        // a tip view reads the tip
        CHECK_EQ(A->read(tip, "a.cpp")->revision, 2);
        CHECK_EQ(A->events(tip)->size(), std::size_t{1});
    }

    {   // R-PIN-2, B8 and B14 through a pinned view: disjoint edits both land (rebased); an overlapping one is refused naming the other actor and revision, and
        // its work is kept. R-PIN-3: since is a brief with the overlap, changes nothing; a pinned view gets no channel.
        Rig r;
        const Identity alice = who("alice"), bob = who("bob"), carol = who("carol");
        Session *A = r.open(alice), *B = r.open(bob), *C = r.open(carol);
        const std::string va = *A->openView({}), vb = *B->openView({}), vc = *C->openView({});
        CHECK_EQ(vb, std::string{"s2/v1"});
        const StageOptions all{{"a.cpp"}, false};

        CHECK(A->stage(va, editLines(2, 2, "    return 10;\n"), all).has_value());
        const auto landedA = A->land(va);
        CHECK_EQ(landedA->applied, std::size_t{1});
        CHECK(landedA->refused.empty());
        CHECK_EQ(landedA->revisions.at("a.cpp"), 2);
        CHECK_STR(r.canvas.text("a.cpp"), kT2);

        CHECK(B->stage(vb, editLines(10, 10, "    return 30;\n"), all).has_value());   // line numbers of the pin (r1), not of r2
        const auto landedB = B->land(vb);
        CHECK_EQ(landedB->applied, std::size_t{1});
        CHECK_EQ(landedB->revisions.at("a.cpp"), 3);
        CHECK_STR(r.canvas.text("a.cpp"), kT3);                              // both edits, each where it belongs
        CHECK_EQ(r.canvas.log().back().by, std::string{"t/bob"});
        CHECK_EQ(B->pending(vb)->hunks, std::size_t{0});                     // landed work is no longer pending

        const auto staged = C->stage(vc, editLines(2, 2, "    return 100;\n"), all);   // overlaps alice's line 2
        CHECK_EQ(staged->overlaps.size(), std::size_t{1});                   // told at stage time, advisory
        CHECK_EQ(staged->overlaps[0].by, std::string{"t/alice"});
        CHECK_EQ(staged->overlaps[0].revision, 2);

        // R-PIN-3 before the refused land: since is a pull, never a push, and moves nothing.
        const std::string recordBefore = viewJson(**C->view(vc));
        const auto since = C->since(vc);
        CHECK_STR(briefOf(since->json),
                  R"({"files":1,"blocks":2,"edits":2,"items":0,"events":0,"kinds":{},"actors":{"t/alice":1,"t/bob":1},)"
                  R"("overlaps":[{"path":"a.cpp","lines":"2","by":"t/alice","revision":2}],"overlapCount":1})");
        CHECK_EQ(since->form, DeltaForm::brief);
        CHECK_STR(viewJson(**C->view(vc)), recordBefore);
        CHECK_STR(C->read(vc, "a.cpp", {.overlay = false})->text, kT1);      // the pin did not move
        CHECK_EQ(C->openChannel(vc).error().code, std::string{"pinned"});
        CHECK(C->openChannel(vc).error().message.find("pinned") != std::string::npos);

        const auto refused = C->land(vc);
        CHECK_EQ(refused->applied, std::size_t{0});
        CHECK_EQ(refused->refused.size(), std::size_t{1});
        CHECK_EQ(refused->refused[0].by, std::string{"t/alice"});
        CHECK_EQ(refused->refused[0].revision, 2);
        CHECK(refused->refused[0].message.find("t/alice") != std::string::npos && refused->refused[0].message.find("revision 2") != std::string::npos);
        CHECK_STR(refused->refused[0].replacement, "    return 100;\n");
        CHECK_STR(r.canvas.text("a.cpp"), kT3);                              // the canvas is exactly as it was
        CHECK_EQ(r.canvas.revision("a.cpp"), 3);
        CHECK_EQ(C->pending(vc)->hunks, std::size_t{1});                     // C's scratch history is kept
        // The canvas's own refusal agrees with the one named (the same actor and revision).
        const TxResult own = r.canvas.apply({"a.cpp", 1, "t/carol", {CanvasOp{CanvasOp::Kind::replaceLines, 2, 2, "", "    return 100;\n"}}});
        CHECK(!own.ok && own.error.find("t/alice") != std::string::npos && own.error.find("revision 2") != std::string::npos);
    }

    {   // Containment: one session cannot read another's pending edits or overlays, only published state (section 7).
        Rig r;
        Session *A = r.open(who("alice")), *B = r.open(who("bob"));
        const std::string va = *A->openView({}), vb = *B->openView({}), vbTip = *B->openView({.state = PinState::unpinned});
        CHECK(A->stage(va, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        const auto mineRead = A->read(va, "a.cpp");
        CHECK(mineRead->overlaid);
        CHECK_STR(mineRead->text, kT2);
        CHECK_EQ(mineRead->marks.size(), std::size_t{1});
        CHECK_EQ(mineRead->marks[0].from, 2);
        CHECK_EQ(mineRead->marks[0].to, 2);
        CHECK_STR(mineRead->labelled, "[pending: lines 2]\n" + kT2);
        const auto pure = A->read(va, "a.cpp", {.overlay = false});            // overlay=off: the pure base
        CHECK_STR(pure->text, kT1);
        CHECK(!pure->overlaid);
        CHECK_STR(B->read(vb, "a.cpp")->text, kT1);                           // bob sees none of it, pinned or at the tip
        CHECK_STR(B->read(vbTip, "a.cpp")->text, kT1);
        CHECK_EQ(B->read(va, "a.cpp").error().code, std::string{"unknown-view"});   // nor can he name her view
        CHECK_EQ(B->land(va).error().code, std::string{"unknown-view"});
        CHECK_EQ(B->discard(va).error().code, std::string{"unknown-view"});
        CHECK_EQ(B->pending(va).error().code, std::string{"unknown-view"});
        CHECK_EQ(B->stage(va, editLines(1, 1, "x\n"), {{"a.cpp"}, false}).error().code, std::string{"unknown-view"});
        CHECK(A->land(va).has_value());                                        // published: now everyone reads it
        CHECK_STR(B->read(vbTip, "a.cpp")->text, kT2);
        CHECK_STR(B->read(vb, "a.cpp")->text, kT1);                           // bob's pin still holds
    }

    {   // R-PIN-4 and decision 6: advance lands what it can and reports the rest; held anchors are re-anchored, authority following the resolution.
        Rig r;
        Session* A = r.open(who("alice"));
        const std::string va = *A->openView({});
        const ViewRecord& rec = **A->view(va);
        const auto beta = A->hold(va, makeSourceAnchor("a.cpp", anchorRevision(rec), kT1, 6, 5, 6, 14, &r.index));    // "return 2;" in beta
        const auto gamma = A->hold(va, makeSourceAnchor("a.cpp", anchorRevision(rec), kT1, 10, 5, 10, 14, &r.index));  // "return 3;" in gamma
        CHECK_EQ(*beta, std::string{"s1/v1#1"});
        CHECK(A->stage(va, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        CHECK_EQ(r.edit("t/bob", 0, 0, "// header\n", CanvasOp::Kind::insertAfter), 2);   // everything below moves down one line
        CHECK_EQ(r.edit("t/carol", 7, 7, "    return 22;\n"), 3);                          // beta's body, in r2's numbering

        const auto adv = A->advance(va);
        CHECK(adv->moved);
        CHECK_EQ(adv->land.applied, std::size_t{1});                       // alice's line 2 landed, rebased to line 3
        CHECK(adv->land.refused.empty());
        CHECK_STR(r.canvas.text("a.cpp"), "// header\nint alpha() {\n    return 10;\n}\n\nint beta() {\n    return 22;\n}\n\nint gamma() {\n    return 3;\n}\n");
        CHECK_EQ(adv->pin.revisionOf("a.cpp"), 4);
        CHECK_EQ(adv->anchors.size(), std::size_t{1});                     // only beta's anchor needs saying
        CHECK_EQ(adv->anchors[0].key, *beta);
        CHECK_EQ(adv->anchors[0].status, std::string{"moved"});
        CHECK_EQ(adv->delta.form, DeltaForm::compacted);                   // the view's own form
        CHECK_EQ((**A->view(va)).held[1].status, std::string{"editable"});   // gamma shifted down one line and kept its authority
        CHECK_EQ(A->resolve(va, *gamma)->key, *gamma);
        CHECK_EQ(A->resolve(va, *beta).error().code, std::string{"not-editable"});   // no edit until re-read
        const auto again = A->reread(va, *beta);
        CHECK(again.has_value());
        CHECK_EQ(again->status, std::string{"editable"});
        CHECK_EQ(A->resolve(va, *beta)->status, std::string{"editable"});
        CHECK_STR(A->read(va, "a.cpp")->text, r.canvas.text("a.cpp"));     // the pin is now the tip

        // Partial and atomic over one conflict and one clean hunk.
        Rig q;
        Session* P = q.open(who("pat"));
        const std::string vp = *P->openView({});
        CHECK(P->stage(vp, editLines(2, 2, "    return 10;\n") + editLines(6, 6, "    return 20;\n"), {{"a.cpp"}, false}).has_value());
        CHECK_EQ(P->pending(vp)->hunks, std::size_t{2});
        CHECK_EQ(q.edit("t/bob", 6, 6, "    return 99;\n"), 2);
        const std::string before = q.canvas.text("a.cpp");
        const auto atomic = P->advance(vp, {.atomic = true});
        CHECK(!atomic->moved && atomic->land.rolledBack);
        CHECK_EQ(atomic->land.applied, std::size_t{0});
        CHECK_EQ(atomic->land.refused.size(), std::size_t{1});
        CHECK_EQ(atomic->land.refused[0].by, std::string{"t/bob"});
        CHECK_STR(q.canvas.text("a.cpp"), before);                            // nothing applied
        CHECK_EQ(P->pending(vp)->hunks, std::size_t{2});                      // pending intact
        CHECK_EQ((**P->view(vp)).pin.revisionOf("a.cpp"), 1);                 // and the pin did not move
        const auto partial = P->advance(vp);
        CHECK(partial->moved);
        CHECK_EQ(partial->land.applied, std::size_t{1});
        CHECK_EQ(partial->land.refused.size(), std::size_t{1});
        CHECK_EQ(partial->land.refused[0].revision, 2);
        CHECK_STR(q.canvas.text("a.cpp"), "int alpha() {\n    return 10;\n}\n\nint beta() {\n    return 99;\n}\n\nint gamma() {\n    return 3;\n}\n");
        CHECK_EQ(P->kept(vp)->size(), std::size_t{1});                        // the refused edit is kept, text and all
        CHECK_STR((*P->kept(vp))[0].replacement, "    return 20;\n");
        CHECK_EQ(P->pending(vp)->hunks, std::size_t{0});
    }

    {   // R-PIN-5: a fork records parent, position and fingerprint; addresses issued to one branch are refused in the other; a mismatched checkpoint is reported;
        // the viewpoint-clone case: fork a stuck turn, advance the original, the fork's same edit is refused.
        Rig r;
        Session* A = r.open(who("alice"));
        const std::string va = *A->openView({});
        const std::string addr = *A->hold(va, makeSourceAnchor("a.cpp", anchorRevision(**A->view(va)), kT1, 2, 5, 2, 14, &r.index));
        CHECK(A->stage(va, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        const auto fork = A->fork(va, {.withPending = true});
        const ViewRecord& f = **A->view(fork->id);
        CHECK_EQ(f.parent, va);
        CHECK(f.position == (**A->view(va)).position);
        CHECK(f.pin == (**A->view(va)).pin);
        CHECK_EQ(f.fingerprint.size(), std::size_t{40});
        CHECK(A->checkFingerprint(fork->id)->ok);
        CHECK_EQ(A->resolve(fork->id, addr).error().code, std::string{"foreign-address"});
        CHECK_EQ(A->resolve(va, fork->addresses.at(addr)).error().code, std::string{"foreign-address"});
        CHECK(A->resolve(fork->id, fork->addresses.at(addr)).has_value());
        CHECK_EQ(A->pending(fork->id)->hunks, std::size_t{1});               // a scratch clone carries the pending edit
        CHECK(A->advance(va)->moved);                                        // the original lands it
        const auto stuck = A->land(fork->id);
        CHECK_EQ(stuck->refused.size(), std::size_t{1});                     // the fork's identical edit meets the first
        CHECK_EQ(stuck->refused[0].revision, 2);
        const auto clean = A->fork(va);                                      // a viewpoint clone has no pending work
        CHECK_EQ(A->pending(clean->id)->hunks, std::size_t{0});
        CHECK(A->checkFingerprint(clean->id)->ok);
        r.canvas.addFile("a.cpp", "a different history\n");                  // the checkpoint no longer matches
        CHECK(!A->checkFingerprint(clean->id)->ok);
        CHECK_EQ(A->checkFingerprint(va).error().code, std::string{"bad-request"});   // not a fork
    }

    {   // R-PIN-6: a refresh in any form applied to the old state gives the new state (compacted and full agree); a reverted edit leaves no trace in the net form.
        Rig r;
        Session* A = r.open(who("alice"));
        const std::string tip = *A->openView({.state = PinState::unpinned});
        const Pin p0 = pinAtTip(r.canvas);
        const Position pos0 = currentPosition(r.canvas, *r.store, &r.index);
        CHECK_EQ(r.edit("t/bob", 2, 2, "    return 10;\n"), 2);
        CHECK_EQ(r.edit("t/bob", 2, 2, "    return 1;\n"), 3);                  // reverted
        CHECK_EQ(r.edit("t/carol", 6, 6, "    return 20;\n"), 4);
        const Delta d = makeDelta({&r.canvas, r.store.get(), &r.index, std::nullopt}, p0, pos0, {}, true);
        CHECK_EQ(d.files.size(), std::size_t{1});
        const FileDelta& f = d.files[0];
        CHECK_EQ(f.regions.size(), std::size_t{1});                          // only line 6 changed in net
        CHECK_EQ(f.regions[0].hunk.baseFrom, 6);
        CHECK_EQ(f.regions[0].blockName, std::string{"beta"});
        CHECK_EQ(f.steps.size(), std::size_t{3});                            // full keeps every intermediate edit
        std::vector<LineHunk> net;
        for (const Region& g : f.regions) net.push_back(g.hunk);
        CHECK_STR(applyHunks(f.oldText, net), f.newText);                    // compacted: the old state plus the net hunks is the new state
        std::string replay = f.oldText;
        for (const EditStep& s : f.steps) replay = applyHunks(replay, s.hunks);
        CHECK_STR(replay, f.newText);                                        // full: every step in order reaches the same state
        CHECK_STR(r.canvas.text("a.cpp"), f.newText);

        const auto compacted = A->refresh(tip, {.form = DeltaForm::compacted});
        CHECK(compacted->json.find(R"("changes":[{"path":"a.cpp","block":"b2","name":"beta","hunks":[{"at":6,"before":["    return 2;"],"after":["    return 20;"]}]}])") != std::string::npos);
        CHECK_STR(A->refresh(tip, {.form = DeltaForm::compacted})->json, compacted->json);   // not acknowledged: the same answer again
        CHECK_EQ(compacted->files, std::size_t{1});
        (void)A->refresh(tip, {.form = DeltaForm::full, .acknowledge = true});
        const auto quiet = A->refresh(tip, {.form = DeltaForm::compacted});
        CHECK_EQ(quiet->files, std::size_t{0});                              // acknowledged: nothing since
        CHECK_EQ(quiet->events, std::size_t{0});
        CHECK(briefOf(quiet->json).find(R"({"files":0,"blocks":0,"edits":0)") == 0);
        CHECK_EQ(A->refresh(*A->openView({}), {}).error().code, std::string{"pinned"});   // a pinned view is not refreshed
        CHECK_EQ(A->openChannel(tip).error().code, std::string{"not-supported"});
    }

    {   // R-PIN-7: a refresh costs what changed in scope, not the document or the log; brief is bounded whatever the edit size.
        Rig r;
        Session* A = r.open(who("alice"));
        SubscriptionQuery q;
        q.layers = {"issues"};
        const std::string tip = *A->openView({.state = PinState::unpinned, .scope = q});
        std::vector<LayerItem> five, other;
        for (int i = 0; i < 5; ++i) five.push_back(item("k" + std::to_string(i)));
        for (int i = 0; i < 100; ++i) { other.push_back(item("o" + std::to_string(i))); other.back().layer = "other"; }
        (void)r.store->putLayer("issues", "src", five);
        (void)r.store->putLayer("other", "src", other);
        const auto got = A->refresh(tip, {.form = DeltaForm::compacted});
        CHECK_EQ(got->events, std::size_t{5});                               // the events in scope, not the 105 in the log
        std::size_t smallBytes = 0;
        {
            Rig s;
            Session* B = s.open(who("bob"));
            const std::string t2 = *B->openView({.state = PinState::unpinned});
            for (int i = 0; i < 10; ++i) CHECK(s.edit("w" + std::to_string(i % 12), 2, 2, i % 2 ? "    return 1;\n" : "    return 10;\n") > 0);
            smallBytes = B->refresh(t2, {.form = DeltaForm::brief})->json.size();
        }
        Rig big;
        Session* C = big.open(who("carol"));
        const std::string t3 = *C->openView({.state = PinState::unpinned});
        for (int i = 0; i < 300; ++i) CHECK(big.edit("w" + std::to_string(i % 12), 2, 2, i % 2 ? "    return 1;\n" : "    return 10;\n") > 0);
        const std::size_t bigBytes = C->refresh(t3, {.form = DeltaForm::brief})->json.size();
        CHECK(smallBytes <= 520);
        CHECK(bigBytes <= 520);                                              // 300 edits by 12 actors: the same bound as 10
        CHECK(bigBytes < smallBytes + 40);
    }

    {   // The four forms on a hand-built delta, exact bytes (docs/VIEWS.md section 3). Old text l1..l10; l2 became L2 (by w1, r2) and l9 was deleted (by w2, r3).
        Delta d;
        d.from = {1, 0, 5, 5};
        d.to = {3, 0, 9, 9};
        FileDelta f;
        f.path = "a.cpp";
        f.fromRevision = 1;
        f.toRevision = 3;
        f.oldText = "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\n";
        f.newText = "l1\nL2\nl3\nl4\nl5\nl6\nl7\nl8\nl10\n";
        LineHunk h1{2, 2, 2, 2, {"l2"}, {"L2"}}, h2{9, 9, 9, 8, {"l9"}, {}};
        f.regions = {{h1, "b1", "f"}, {h2, "b2", "g"}};
        f.steps = {{2, "w1", 2, 2, 0, {h1}}, {3, "w2", 9, 9, -1, {h2}}};
        d.files = {f};
        Event e1;
        e1.kind = "item.put@1";
        e1.writer = {"proj", "srv"};
        e1.lamport = 7;
        e1.subject = "k1";
        Event e2 = e1;
        e2.kind = "item.state@1";
        e2.lamport = 8;
        d.events = {e1, e2};
        d.items = {{"k1", "issues", "error", "t", "done", "b1"}};
        const DeltaBudget budget;
        const BlockCompactor bc;
        const std::string head = R"({"schema":"view-delta-1","form":")";
        const std::string pos = R"("from":{"generation":1,"indexEvents":0,"storeHead":5,"storeEvents":5},"to":{"generation":3,"indexEvents":0,"storeHead":9,"storeEvents":9},)";
        const std::string brief = R"("brief":{"files":1,"blocks":2,"edits":2,"items":1,"events":2,"kinds":{"item.put@1":1,"item.state@1":1},"actors":{"proj/srv":2,"w1":1,"w2":1}})";

        CHECK_STR(renderDelta(d, DeltaForm::brief, budget, bc, {}).json, head + "brief\"," + pos + brief + "}");
        CHECK_STR(renderDelta(d, DeltaForm::elided, budget, bc, {}).json,
                  head + "elided\"," + pos + brief +
                      R"(,"files":[{"path":"a.cpp","from":1,"to":3,"text":"   1| l1\n   2|+L2\n   3| l3\n      ... (4 lines)\n   8| l8\n      - (1 removed)\n   9| l10\n"}]})");
        const std::string compactedChanges =
            R"(,"changes":[{"path":"a.cpp","block":"b1","name":"f","hunks":[{"at":2,"before":["l2"],"after":["L2"]}]},)"
            R"({"path":"a.cpp","block":"b2","name":"g","hunks":[{"at":9,"before":["l9"],"after":[]}]}],"items":)";
        CHECK_STR(renderDelta(d, DeltaForm::compacted, budget, bc, {}).json,
                  head + "compacted\"," + pos + brief + compactedChanges + R"([{"key":"k1","layer":"issues","kind":"error","state":"done","title":"t"}]})");
        CHECK_STR(renderDelta(d, DeltaForm::compacted, budget, bc, {CompactBy::blockItem}).json,   // the item folds under its block
                  head + "compacted\"," + pos + brief +
                      R"(,"changes":[{"path":"a.cpp","block":"b1","name":"f","hunks":[{"at":2,"before":["l2"],"after":["L2"]}],"items":["k1"]},)"
                      R"({"path":"a.cpp","block":"b2","name":"g","hunks":[{"at":9,"before":["l9"],"after":[]}]}],"items":[]})");
        CHECK_STR(renderDelta(d, DeltaForm::full, budget, bc, {}).json,
                  head + "full\"," + pos + brief +
                      R"(,"files":[{"path":"a.cpp","from":1,"to":3,"steps":[{"revision":2,"by":"w1","lines":"2","delta":0,"hunks":[{"at":2,"before":["l2"],"after":["L2"]}]},)"
                      R"({"revision":3,"by":"w2","lines":"9","delta":-1,"hunks":[{"at":9,"before":["l9"],"after":[]}]}]}],"events":[)"
                      R"({"kind":"item.put@1","writer":{"namespace":"proj","writer":"srv"},"lamport":7,"subject":"k1","time":"","payload":{}},)"
                      R"({"kind":"item.state@1","writer":{"namespace":"proj","writer":"srv"},"lamport":8,"subject":"k1","time":"","payload":{}}]})");

        // Over budget: each step is a whole form towards brief, said so, never a diff cut short.
        const auto tiny = renderDelta(d, DeltaForm::full, DeltaBudget{.maxChars = 1}, bc, {});
        CHECK(tiny.degraded && tiny.form == DeltaForm::brief && tiny.requested == DeltaForm::full);
        CHECK_STR(tiny.json, head + "brief\",\"requested\":\"full\",\"degraded\":true," + pos + brief + "}");
        const std::size_t compactedSize = renderDelta(d, DeltaForm::compacted, budget, bc, {}).json.size();
        const auto mid = renderDelta(d, DeltaForm::full, DeltaBudget{.maxChars = compactedSize}, bc, {});
        CHECK(mid.degraded && mid.json.size() <= compactedSize && (mid.form == DeltaForm::elided || mid.form == DeltaForm::compacted));
        CHECK(!renderDelta(d, DeltaForm::compacted, budget, bc, {}).degraded);
    }

    {   // Quotas as data: a structured refusal names the limit; replay and expiry.
        DataSessionPolicy p;
        p.defaults.maxViews = 2;
        p.defaults.maxPendingBytes = 60;
        Rig r(p);
        Session* A = r.open(who("alice"));
        const std::string v1 = *A->openView({});
        CHECK(A->openView({}).has_value());
        const auto third = A->openView({});
        CHECK(!third.has_value());
        CHECK_EQ(third.error().code, std::string{"quota"});
        CHECK_EQ(third.error().limit, std::string{"views"});
        CHECK_EQ(third.error().current, std::uint64_t{2});
        CHECK_EQ(third.error().max, std::uint64_t{2});
        CHECK(A->closeView(v1).has_value());                                 // closing frees the quota
        CHECK(A->openView({}).has_value());
        const auto big = A->stage("s1/v2", editLines(2, 2, std::string(100, 'x') + "\n"), {{"a.cpp"}, false});
        CHECK_EQ(big.error().code, std::string{"quota"});
        CHECK_EQ(big.error().limit, std::string{"pendingBytes"});
        CHECK_EQ(A->pending("s1/v2")->hunks, std::size_t{0});                // the refused stage left nothing behind
        CHECK(A->stage("s1/v2", editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());

        DataSessionPolicy sp;
        sp.defaults.maxSubscriptions = 2;
        Rig s(sp);
        Session* B = s.open(who("bob"));
        CHECK(B->openView({}).has_value());
        CHECK(B->subscribe({}).has_value());
        const auto over = B->openView({});
        CHECK_EQ(over.error().limit, std::string{"subscriptions"});
        CHECK_EQ(B->subscribe({}).error().code, std::string{"quota"});
        // Per-session cursors: acknowledging moves only this session's.
        Session* C = s.open(who("carol"));
        const std::string c1 = *C->subscribe({});
        (void)s.store->putLayer("issues", "src", std::vector<LayerItem>{item("k1")});
        CHECK_EQ(C->inbox(c1)->events.size(), std::size_t{1});
        CHECK(C->acknowledge(c1, C->inbox(c1)->upTo).has_value());
        CHECK_EQ(C->inbox(c1)->events.size(), std::size_t{0});
        CHECK_EQ(B->inbox(c1).error().code, std::string{"unknown-subscription"});
    }

    {   // Identity and capabilities come from the caller; expiry by an injected number.
        DataSessionPolicy p;
        p.defaults.idleTtl = 100;
        p.require["edit"] = "edit";
        Rig r(p);
        const Identity alice = who("alice");
        const std::string sid = *r.ws->open(alice, 0);
        CHECK_EQ(r.ws->attach(sid, who("mallory"), 0).error().code, std::string{"not-your-session"});
        CHECK_EQ(r.ws->attach("s9", alice, 0).error().code, std::string{"unknown-session"});
        Session* A = *r.ws->attach(sid, alice, 0);
        const std::string v = *A->openView({});
        CHECK_EQ(A->stage(v, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).error().code, std::string{"capability"});
        A = *r.ws->attach(sid, who("alice", {"edit"}), 50);                  // the caller now vouches for the capability
        CHECK(A->stage(v, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        CHECK_EQ(r.ws->sweep(140), std::size_t{0});                          // idle 90 s: within the limit
        CHECK_EQ(r.ws->sweep(151), std::size_t{1});                          // idle 101 s
        CHECK_EQ(r.ws->attach(sid, alice, 151).error().code, std::string{"expired"});
        CHECK(r.views->session(sid)->closed);
        CHECK(r.views->view(v)->closed);
        CHECK(findSubscription(*r.store, "view:" + v) == nullptr);
        const std::string second = *r.ws->open(alice, 200);
        CHECK_EQ(second, std::string{"s2"});
        CHECK_EQ(r.ws->attach(second, alice, 301).error().code, std::string{"expired"});   // attach itself notices
    }

    {   // R-PIN-9 and "resume equals live": views and positions replay from the log to the same records; a resumed session has the same views and positions.
        Rig r;
        const Identity alice = who("alice"), bob = who("bob");
        Session *A = r.open(alice), *B = r.open(bob);
        const std::string va = *A->openView({.label = "worker"}), vb = *B->openView({.state = PinState::unpinned, .compactBy = CompactBy::blockItem});
        (void)A->hold(va, makeSourceAnchor("a.cpp", anchorRevision(**A->view(va)), kT1, 6, 5, 6, 14, &r.index));
        CHECK(A->stage(va, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        CHECK(A->advance(va)->moved);
        (void)B->refresh(vb, {.acknowledge = true});
        const std::string vf = A->fork(va)->id;
        const std::string live = A->read(va, "a.cpp", {.overlay = false})->text;

        StoreOptions o;
        o.writer = {"proj", "replica"};
        registerViewReducers(o.registry);
        Store replica(std::move(o));
        replica.replay(r.log);
        LogViewStore rv(replica);
        CHECK_EQ(rv.sessions().size(), r.views->sessions().size());
        for (const SessionRecord* s : r.views->sessions()) {
            CHECK_STR(sessionJson(*rv.session(s->id)), sessionJson(*s));
            CHECK_EQ(rv.viewsOf(s->id).size(), r.views->viewsOf(s->id).size());
            for (const ViewRecord* v : r.views->viewsOf(s->id)) CHECK_STR(viewJson(*rv.view(v->id)), viewJson(*v));
        }
        CHECK(!parseViewRecord(viewJson(**A->view(va)))->held.empty());      // the record round-trips through its JSON
        CHECK(*parseViewRecord(viewJson(**A->view(va))) == **A->view(va));

        DataSessionPolicy p2;
        Workspace again(r.canvas, replica, rv, p2, r.compactor, &r.index);   // a restarted process over the replayed log
        Session* A2 = *again.attach("s1", alice, 10);
        CHECK_EQ(A2->views().size(), std::size_t{2});
        for (const std::string& id : {va, vf}) {
            CHECK(**A2->view(id) == **A->view(id));                          // same views, same positions
            CHECK_STR(A2->read(id, "a.cpp")->text, A->read(id, "a.cpp", {.overlay = false})->text);
        }
        CHECK_STR(A2->read(va, "a.cpp")->text, live);
        CHECK_EQ(A2->pending(va)->hunks, std::size_t{0});                    // pending edits are the session's memory, not the log's
        Session* B2 = *again.attach("s2", bob, 10);
        CHECK(B2->refresh(vb, {}).has_value());                              // the tip view's cursor came with the log
        CHECK_EQ(again.attach("s2", alice, 10).error().code, std::string{"not-your-session"});
        const std::string next = *A->openView({});
        CHECK_EQ(next, std::string{"s1/v3"});             // ids continue where they were
        CHECK_EQ(A2->unpin(va).has_value(), true);
    }

    {   // Unpin: refused while edits are pending; the first refresh is the delta since the old pin. A held view closes cleanly.
        Rig r;
        Session* A = r.open(who("alice"));
        const std::string v = *A->openView({});
        CHECK(A->stage(v, editLines(2, 2, "    return 10;\n"), {{"a.cpp"}, false}).has_value());
        CHECK_EQ(A->unpin(v).error().code, std::string{"pending-work"});
        CHECK(A->discard(v).has_value());
        CHECK(A->unpin(v).has_value());
        CHECK_EQ(r.edit("t/bob", 6, 6, "    return 20;\n"), 2);
        const auto first = A->refresh(v, {.form = DeltaForm::brief});
        CHECK_EQ(first->files, std::size_t{1});
        CHECK_EQ(A->stage(v, editLines(2, 2, "x\n"), {{"a.cpp"}, false}).error().code, std::string{"unpinned"});
        Rig n;
        Session* N = n.open(who("nina"), 0);
        SubscriptionQuery flagged;
        const std::string f = *N->openView({.state = PinState::unpinned, .notify = Notify::flag});
        const std::string quiet = *N->openView({.state = PinState::unpinned});
        CHECK(!*N->isStale(f));
        CHECK_EQ(N->isStale(quiet).error().code, std::string{"notify-off"});
        CHECK_EQ(n.edit("t/bob", 2, 2, "    return 10;\n"), 2);
        CHECK(*N->isStale(f));
        CHECK(N->closeView(f).has_value());
        CHECK_EQ(N->closeView(f).error().code, std::string{"unknown-view"});
        (void)flagged;
    }

    return finish();
}
