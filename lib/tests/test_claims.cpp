// Claims and notes (slice B3): requirement R-BB-6 (docs/REQUIREMENTS.md) and BACKBONE.md section 3.5.
// The expected values are written by hand from the rules: expiry is `at + ttl` and exclusive of that second, the bytes of one take event, who wins two
// concurrent takes (the earlier `(lamport, writer)`), what a merge, a split and a retirement do to a block claim, where a note ends up. No time is read: every
// instant below is a number the test passes in.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/claims.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

const WriterId kA{"p", "a"}, kB{"p", "b"};

Store makeStore(const WriterId& who, const ClaimPolicy& policy = ClaimPolicy::standard(), Log* sink = nullptr)
{
    StoreOptions o;
    o.writer = who;
    o.sink = sink;
    CHECK(registerClaimReducers(o.registry, policy));
    return Store(std::move(o));
}

/// One layer put with all the keys: a later put that omitted a key would set it done (R-BB-2).
void putItems(Store& s, const std::vector<std::string>& keys)
{
    std::vector<ItemInput> items;
    for (const std::string& key : keys) {
        ItemInput in;
        in.item.key = key;
        in.item.title = "title of " + key;
        items.push_back(in);
    }
    (void)s.putLayer("issues", "s", items);
}

void putItem(Store& s, const std::string& key) { putItems(s, {key}); }

ClaimRequest item(const std::string& key, const std::string& mode = "write")
{
    ClaimRequest r;
    r.kind = TargetKind::item;
    r.target = key;
    r.mode = mode;
    return r;
}

ClaimRequest block(const std::string& id, const std::string& path = {})
{
    ClaimRequest r;
    r.kind = TargetKind::block;
    r.target = id;
    r.path = path;
    return r;
}

ClaimRequest file(const std::string& path)
{
    ClaimRequest r;
    r.kind = TargetKind::file;
    r.target = path;
    return r;
}

/// What a `BlockIndex` would log for a block that merged into another.
BlockEvent mergedEvent(const std::string& id, const std::string& into) { return BlockEvent{Carry::merged, id, "src/a.cpp", "f", into, 1}; }

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

int main()
{
    const ClaimPolicy policy = ClaimPolicy::standard();

    {
        // The take event: its bytes, its id, its expiry (R-BB-6: expiry is at + ttl), the item held, the holder from the log.
        Store a = makeStore(kA);
        putItem(a, "k1");   // lamport 1
        auto c = takeClaim(a, policy, kA, item("k1"), 1000);
        CHECK(c.has_value());
        const Event& e = a.events().back();
        CHECK_EQ(e.kind, std::string("claim.take@1"));
        CHECK_EQ(e.subject, std::string("p/a:2"));
        CHECK_EQ(e.payload, std::string(R"({"kind":"item","target":"k1","mode":"write","at":1000,"ttl":300})"));
        if (c) {
            CHECK_EQ(c->id, std::string("p/a:2"));
            CHECK_EQ(c->expires, Instant{1300});
            CHECK(c->holder == kA);
            CHECK_EQ(std::string(claimStatusName(c->status)), std::string("live"));
        }
        CHECK(a.item("k1")->state == ItemState::held);
        CHECK_EQ(a.item("k1")->lastTransition->actor, std::string("a"));
    }

    {
        // R-BB-6: two writers; the second's conflicting claim is refused naming the holder and the expiry; the expiry frees it; so does a release.
        Store a = makeStore(kA), b = makeStore(kB);
        putItem(a, "k1");
        (void)takeClaim(a, policy, kA, item("k1"), 1000);   // until 1300
        b.merge(a.events());

        auto refused = takeClaim(b, policy, kB, item("k1"), 1100);
        CHECK(!refused.has_value());
        if (!refused) {
            CHECK(refused.error().why == ClaimRefusal::Why::conflict);
            CHECK(refused.error().holder == kA);
            CHECK_EQ(refused.error().expires, Instant{1300});
            CHECK_EQ(refused.error().claimId, std::string("p/a:2"));
            CHECK(contains(refused.error().message, "p/a"));
            CHECK(contains(refused.error().message, "1300"));
        }
        CHECK_EQ(b.events().size(), a.events().size());   // a refusal before writing costs no event
        CHECK(!checkClaim(b, policy, kB, item("k1"), 1299).has_value());
        CHECK(checkClaim(b, policy, kB, item("k1"), 1300).has_value());   // expiry frees it: at + ttl, exclusive
        CHECK(checkClaim(b, policy, kB, item("k1", "observe"), 1100).has_value());   // observe is shared
        CHECK(checkClaim(b, policy, kA, item("k1"), 1100).has_value());   // the holder does not conflict with itself

        auto later = takeClaim(b, policy, kB, item("k1"), 1300);
        CHECK(later.has_value());
        CHECK_EQ(claimsOn(b, TargetKind::item, "k1", 1300).size(), std::size_t{1});   // A's is past its time
        CHECK(claimsOn(b, TargetKind::item, "k1", 1300)[0].holder == kB);

        // Release frees it, and only the holder may release.
        Store c = makeStore(kA), d = makeStore(kB);
        putItem(c, "k1");
        auto taken = takeClaim(c, policy, kA, item("k1"), 1000);
        d.merge(c.events());
        CHECK(!releaseClaim(d, kB, taken->id, 1100).has_value());
        CHECK(releaseClaim(c, kA, taken->id, 1100, "done early").has_value());
        d.merge(c.events());
        CHECK(checkClaim(d, policy, kB, item("k1"), 1100).has_value());
        CHECK_EQ(findClaim(d, taken->id)->reason, std::string("done early"));
        CHECK(c.item("k1")->state == ItemState::open);   // releasing returns a held item to open
        CHECK(!releaseClaim(c, kA, taken->id, 1200).has_value());   // already over
    }

    {
        // Renew extends (never shortens) and clamps to the mode's maxTtl; a late renewal is refused; expireDue writes the fact down once.
        Store a = makeStore(kA);
        putItem(a, "k1");
        auto c = takeClaim(a, policy, kA, item("k1"), 1000);
        auto r = renewClaim(a, kA, c->id, std::nullopt, 1200, policy);
        CHECK(r.has_value());
        if (r) CHECK_EQ(r->expires, Instant{1500});
        auto shorter = renewClaim(a, kA, c->id, Instant{10}, 1210, policy);
        CHECK(shorter.has_value());
        if (shorter) CHECK_EQ(shorter->expires, Instant{1500});   // 1210 + 10 < 1500: kept
        auto huge = renewClaim(a, kA, c->id, Instant{99999}, 1220, policy);
        if (huge) CHECK_EQ(huge->expires, Instant{1220 + 3600});
        CHECK(!renewClaim(a, kB, c->id, std::nullopt, 1230, policy).has_value());   // not the holder's
        CHECK(!renewClaim(a, kA, c->id, std::nullopt, 9000, policy).has_value());   // over: take again

        Store e = makeStore(kA);
        putItems(e, {"k1", "k2"});
        (void)takeClaim(e, policy, kA, item("k1"), 1000);   // until 1300
        (void)takeClaim(e, policy, kA, item("k2"), 1200);   // until 1500
        CHECK_EQ(expireDue(e, 1299), std::size_t{0});
        CHECK_EQ(expireDue(e, 1300), std::size_t{1});
        CHECK_EQ(expireDue(e, 1300), std::size_t{0});
        CHECK(e.item("k1")->state == ItemState::open);    // the expired claim let go of the item
        CHECK(e.item("k2")->state == ItemState::held);
        CHECK_EQ(findClaim(e, "p/a:3")->reason, std::string("expired"));
    }

    {
        // Concurrent takes settle by merge order, the same on every replica: both writers take at lamport 1, so the writer breaks the tie and `a` wins.
        Store a = makeStore(kA), b = makeStore(kB);
        auto ca = takeClaim(a, policy, kA, item("k1"), 1000);
        auto cb = takeClaim(b, policy, kB, item("k1"), 1001);
        CHECK(ca.has_value() && cb.has_value());
        Store r1 = makeStore({"p", "r1"}), r2 = makeStore({"p", "r2"});
        r1.merge(a.events());
        r1.merge(b.events());
        r2.merge(b.events());
        r2.merge(a.events());
        for (const Store* s : {&r1, &r2}) {
            CHECK_EQ(std::string(claimStatusName(findClaim(*s, "p/a:1")->status)), std::string("live"));
            CHECK_EQ(std::string(claimStatusName(findClaim(*s, "p/b:1")->status)), std::string("refused"));
            CHECK_EQ(findClaim(*s, "p/b:1")->reason, std::string("p/a:1"));   // names the claim that blocked it
        }
        CHECK_EQ(claimsJson(r1, 1100), claimsJson(r2, 1100));
        CHECK(holderOf(r1, policy, TargetKind::item, "k1", "write", 1100)->holder == kA);
    }

    {
        // Replay equals live (R-BB-1 for claims): a replica built from the log has the same claims, notes and snapshot.
        MemoryLog log;
        Store a = makeStore(kA, policy, &log);
        putItems(a, {"k1", "k2"});

        auto c1 = takeClaim(a, policy, kA, item("k1"), 1000);
        auto c2 = takeClaim(a, policy, kA, block("b1", "src/a.cpp"), 1010);
        (void)takeClaim(a, policy, kA, item("k2", "observe"), 1020);
        (void)renewClaim(a, kA, c1->id, Instant{600}, 1100, policy);
        (void)emitBlockCarries(a, {mergedEvent("b1", "b2")});
        (void)addNote(a, TargetKind::block, "b1", "needs a second look");
        (void)releaseClaim(a, kA, c2->id, 1200);
        (void)expireDue(a, 5000);
        Store replica = makeStore({"p", "r"});
        replica.replay(log);
        for (const Instant t : {Instant{1000}, Instant{1050}, Instant{1300}, Instant{5000}}) CHECK_EQ(claimsJson(a, t), claimsJson(replica, t));
        CHECK_EQ(a.snapshotJson(), replica.snapshotJson());
        CHECK_EQ(notesOn(a, TargetKind::block, "b2").size(), notesOn(replica, TargetKind::block, "b2").size());
        CHECK_EQ(claimCount(a), claimCount(replica));
        // The same log merged twice, or in two halves, changes nothing.
        Store halves = makeStore({"p", "h"});
        halves.replay(log, 5);
        halves.replay(log, 0);
        CHECK_EQ(claimsJson(a, 1050), claimsJson(halves, 1050));
    }

    {
        // A block claim follows a merge: b1 merged into b2 (what the index logs: Carry::merged, detail = survivor). The claim now covers b2, the old id still finds it.
        Store a = makeStore(kA), b = makeStore(kB);
        auto c = takeClaim(a, policy, kA, block("b1", "src/a.cpp"), 1000);
        CHECK_EQ(c->covers.size(), std::size_t{1});
        CHECK_EQ(emitBlockCarries(a, {mergedEvent("b1", "b2")}), std::size_t{1});
        b.merge(a.events());
        CHECK_EQ(findClaim(b, c->id)->covers[0], std::string("block:b2"));
        auto onSurvivor = takeClaim(b, policy, kB, block("b2"), 1100);
        CHECK(!onSurvivor.has_value());
        if (!onSurvivor) CHECK(onSurvivor.error().holder == kA);
        auto onOldId = checkClaim(b, policy, kB, block("b1"), 1100);   // a stale writer still names b1
        CHECK(!onOldId.has_value());
        if (!onOldId) CHECK_EQ(onOldId.error().expires, Instant{1300});
        CHECK_EQ(claimsOn(b, TargetKind::block, "b1", 1100).size(), std::size_t{1});
        CHECK_EQ(resolveBlock(b, "b1")[0], std::string("b2"));
        CHECK_EQ(resolveBlock(b, "b9")[0], std::string("b9"));   // unknown ids stand for themselves
        CHECK(releaseClaim(a, kA, c->id, 1200).has_value());
        b.merge(a.events());
        CHECK(checkClaim(b, policy, kB, block("b2"), 1200).has_value());

        // Two writers each held one of the two blocks before they merged: both claims now sit on the survivor, and both stay (the fold ends neither).
        Store x = makeStore(kA), y = makeStore(kB);
        (void)takeClaim(x, policy, kA, block("b1"), 1000);
        (void)takeClaim(y, policy, kB, block("b2"), 1000);
        x.merge(y.events());
        (void)emitBlockCarries(x, {mergedEvent("b1", "b2")});
        CHECK_EQ(claimsOn(x, TargetKind::block, "b2", 1100).size(), std::size_t{2});
    }

    {
        // A retired block: its claims end `retired`, nothing can claim it, and a revived id can be claimed again.
        Store a = makeStore(kA);
        auto c = takeClaim(a, policy, kA, block("b3"), 1000);
        (void)emitBlockCarries(a, {BlockEvent{Carry::kept, "b4", "src/a.cpp", "g", "", 1}, BlockEvent{Carry::retired, "b3", "src/a.cpp", "f", "", 1}});
        CHECK_EQ(std::string(claimStatusName(findClaim(a, c->id)->status)), std::string("retired"));
        CHECK_EQ(findClaim(a, c->id)->reason, std::string("retired"));
        CHECK(claimsOn(a, TargetKind::block, "b3", 1100).empty());
        auto again = takeClaim(a, policy, kA, block("b3"), 1100);
        CHECK(!again.has_value());
        if (!again) CHECK(again.error().why == ClaimRefusal::Why::retired);
        CHECK(resolveBlock(a, "b3").empty());
        CHECK_EQ(emitBlockCarries(a, {BlockEvent{Carry::revived, "b3", "src/b.cpp", "f", "src/a.cpp", 2}}), std::size_t{1});
        CHECK(takeClaim(a, policy, kA, block("b3"), 1200).has_value());
    }

    {
        // A real index: deleting a function retires its block and the claim on it ends (hand-checked: `beta` shares no body with `alpha`).
        BlockIndex idx;
        idx.update("src/a.cpp", "int alpha(int x) {\n    return x + 1;\n}\n\nint beta(int y) {\n    return y * 2;\n}\n");
        const std::string betaId = idx.blocks("src/a.cpp")[1]->id;
        Store a = makeStore(kA);
        auto c = takeClaim(a, policy, kA, block(betaId, "src/a.cpp"), 1000);
        idx.setTurn(2);
        idx.update("src/a.cpp", "int alpha(int x) {\n    return x + 1;\n}\n");
        CHECK(!idx.events().empty() && idx.events().back().carry == Carry::retired);
        (void)emitBlockCarries(a, idx.events());
        CHECK_EQ(std::string(claimStatusName(findClaim(a, c->id)->status)), std::string("retired"));
    }

    {
        // A split: b5 became b8 and b9 (hand-written truth). A claim on b5 covers both parts; a claim on b9 is refused naming the holder; the release frees both.
        // When the index keeps the id on one part and adds the other, no carry is logged and the claim stays on the kept id; the new block is free.
        Store a = makeStore(kA), b = makeStore(kB);
        auto c = takeClaim(a, policy, kA, block("b5"), 1000);
        (void)emitBlockCarry(a, "b5", {"b8", "b9"});
        b.merge(a.events());
        CHECK_EQ(findClaim(b, c->id)->covers.size(), std::size_t{2});
        auto refused = checkClaim(b, policy, kB, block("b9"), 1100);
        CHECK(!refused.has_value());
        if (!refused) CHECK(refused.error().holder == kA);
        CHECK(!checkClaim(b, policy, kB, block("b8"), 1100).has_value());
        CHECK_EQ(claimsOn(b, TargetKind::block, "b5", 1100).size(), std::size_t{1});   // the parent id finds it once
        CHECK(releaseClaim(a, kA, c->id, 1150).has_value());
        b.merge(a.events());
        CHECK(checkClaim(b, policy, kB, block("b8"), 1150).has_value());
        CHECK(checkClaim(b, policy, kB, block("b9"), 1150).has_value());

        Store k = makeStore(kA);
        (void)takeClaim(k, policy, kA, block("b5"), 1000);
        (void)emitBlockCarries(k, {BlockEvent{Carry::added, "b9", "src/a.cpp", "g", "", 2}});   // b5 keeps its id; b9 is new
        CHECK(!checkClaim(k, policy, kB, block("b5"), 1100).has_value());
        CHECK(checkClaim(k, policy, kB, block("b9"), 1100).has_value());
    }

    {
        // A file claim covers the blocks of the file (and the reverse), unless the policy says otherwise.
        Store a = makeStore(kA);
        (void)takeClaim(a, policy, kA, file("src/a.cpp"), 1000);
        auto refused = checkClaim(a, policy, kB, block("b7", "src/a.cpp"), 1100);
        CHECK(!refused.has_value());
        if (!refused) CHECK(refused.error().holder == kA);
        CHECK(checkClaim(a, policy, kB, block("b7", "src/other.cpp"), 1100).has_value());
        CHECK(checkClaim(a, policy, kB, block("b7"), 1100).has_value());   // a block claim that names no file is not covered
        ClaimPolicy flat = policy;
        flat.fileCoversBlocks = false;
        CHECK(checkClaim(a, flat, kB, block("b7", "src/a.cpp"), 1100).has_value());

        Store r = makeStore(kA);
        (void)takeClaim(r, policy, kA, block("b7", "src/a.cpp"), 1000);
        CHECK(!checkClaim(r, policy, kB, file("src/a.cpp"), 1100).has_value());
        CHECK(checkClaim(r, policy, kB, file("src/b.cpp"), 1100).has_value());
        CHECK(holderOf(r, policy, TargetKind::file, "src/a.cpp", "write", 1100).has_value());
    }

    {
        // The policy is data: a client's JSON makes `write` shared with a 50 s default; two writers then both hold it and the default ttl applies.
        auto shared = parseClaimPolicy(R"({"modes":{"write":{"exclusive":false,"holdsItem":false,"ttl":50,"maxTtl":100}},"fileCoversBlocks":false})");
        CHECK(shared.has_value());
        if (shared) {
            Store a = makeStore(kA, *shared), b = makeStore(kB, *shared);
            auto ca = takeClaim(a, *shared, kA, item("k1"), 1000);
            CHECK_EQ(ca->expires, Instant{1050});
            b.merge(a.events());
            CHECK(takeClaim(b, *shared, kB, item("k1"), 1010).has_value());
            a.merge(b.events());
            CHECK_EQ(claimsOn(a, TargetKind::item, "k1", 1020).size(), std::size_t{2});
            CHECK(!shared->fileCoversBlocks);
            CHECK(!takeClaim(a, *shared, kA, item("k1", "observe"), 1000).has_value());   // `observe` is not a mode of this policy
            auto clamped = takeClaim(a, *shared, kA, [] { ClaimRequest r = item("k2"); r.ttl = 500; return r; }(), 1000);
            CHECK_EQ(clamped->expires, Instant{1100});
        }
        CHECK(!parseClaimPolicy(R"({"modes":{"write":{"ttl":0}}})").has_value());
        CHECK(!parseClaimPolicy(R"({"modes":{"write":{"ttl":500,"maxTtl":100}}})").has_value());
        CHECK(!parseClaimPolicy("[1]").has_value());
        auto standard = parseClaimPolicy("{}");
        CHECK(standard.has_value() && standard->mode("write") != nullptr && standard->mode("write")->exclusive && standard->fileCoversBlocks);
        auto unknown = checkClaim(makeStore(kA), policy, kA, item("k1", "tinker"), 1000);
        CHECK(!unknown.has_value());
        if (!unknown) CHECK(unknown.error().why == ClaimRefusal::Why::unknownMode);
    }

    {
        // Notes: the author, order and time are the log's; a block's notes follow a merge and a split; an item's do not move.
        Store a = makeStore(kA), b = makeStore(kB);
        (void)addNote(a, TargetKind::block, "b1", "first");
        (void)addNote(a, TargetKind::item, "k1", "on the item");
        b.merge(a.events());
        (void)addNote(b, TargetKind::block, "b1", "second");
        a.merge(b.events());
        const auto notes = notesOn(a, TargetKind::block, "b1");
        CHECK_EQ(notes.size(), std::size_t{2});
        if (notes.size() == 2) {
            CHECK_EQ(notes[0].text, std::string("first"));
            CHECK(notes[0].at.writer == kA);
            CHECK_EQ(notes[1].text, std::string("second"));
            CHECK(notes[1].at.writer == kB);
            CHECK_EQ(notes[1].at.lamport, std::uint64_t{3});
        }
        (void)emitBlockCarries(a, {mergedEvent("b1", "b2")});
        CHECK_EQ(notesOn(a, TargetKind::block, "b2").size(), std::size_t{2});
        CHECK_EQ(notesOn(a, TargetKind::block, "b1").size(), std::size_t{2});   // the old id resolves to the survivor
        (void)addNote(a, TargetKind::block, "b1", "after the merge");   // written on the old id, filed under the survivor
        CHECK_EQ(notesOn(a, TargetKind::block, "b2").size(), std::size_t{3});
        CHECK_EQ(notesOn(a, TargetKind::item, "k1").size(), std::size_t{1});
        (void)emitBlockCarry(a, "b2", {"b6", "b7"});
        CHECK_EQ(notesOn(a, TargetKind::block, "b6").size(), std::size_t{3});
        CHECK_EQ(notesOn(a, TargetKind::block, "b7").size(), std::size_t{3});
        CHECK_EQ(notesOn(a, TargetKind::block, "b2").size(), std::size_t{3});   // both parts, each note once
        (void)addNote(a, TargetKind::item, "k1", "");   // an empty note is not a note
        CHECK_EQ(a.state().skipped, std::size_t{1});
    }

    {
        // Unknown kinds survive (A7): a newer claim kind and an unrelated one are kept verbatim, counted, and never change the claims; a take whose payload
        // is unusable is skipped, not applied; replay gives the same state.
        MemoryLog log;
        Store a = makeStore(kA, policy, &log);
        (void)takeClaim(a, policy, kA, item("k1"), 1000);
        Event newer;
        newer.kind = "claim.take@2";
        newer.writer = kB;
        newer.lamport = 10;
        newer.subject = "p/b:10";
        newer.payload = R"({"target":"k1","lease":{"until":99}})";
        newer.extra = R"("trace":"t-1")";
        CHECK(a.append(newer));
        Event odd = newer;
        odd.kind = "lease.frob@1";
        odd.lamport = 11;
        CHECK(a.append(odd));
        Event bad = newer;
        bad.kind = "claim.take@1";
        bad.lamport = 12;
        bad.payload = R"({"kind":"item","target":"k1","mode":"write","at":"yesterday","ttl":300})";
        CHECK(a.append(bad));
        CHECK_EQ(a.state().unknownKinds.at("claim.take@2"), std::size_t{1});
        CHECK_EQ(a.state().unknownKinds.at("lease.frob@1"), std::size_t{1});
        CHECK_EQ(a.state().skipped, std::size_t{1});
        CHECK_EQ(claimCount(a), std::size_t{1});
        CHECK(eventJson(a.events()[1]).find(R"("trace":"t-1")") != std::string::npos);
        CHECK(*parseEvent(eventJson(a.events()[1])) == newer);
        Store replica = makeStore({"p", "r"});
        replica.replay(log);
        CHECK_EQ(replica.events().size(), std::size_t{4});
        CHECK_EQ(claimsJson(replica, 1100), claimsJson(a, 1100));
        CHECK_EQ(replica.state().unknownKinds.size(), std::size_t{2});
        // A store without the claim reducers keeps the claim events as unknown and has no claims (a reader that predates B3).
        Store old;
        old.replay(log);
        CHECK_EQ(old.events().size(), std::size_t{4});
        CHECK_EQ(claimCount(old), std::size_t{0});
        CHECK_EQ(old.state().unknownKinds.at("claim.take@1"), std::size_t{2});
    }

    return finish();
}
