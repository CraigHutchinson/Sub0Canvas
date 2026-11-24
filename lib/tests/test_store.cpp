// The store core (slice B1): requirements R-BB-1, R-BB-2, R-BB-3, R-BB-7, R-BB-8, R-BB-9, R-BB-10, R-COL-2, R-EXP-2 (docs/REQUIREMENTS.md).
// The expected values are written by hand from the requirements, not read back from the code: the 5-turn layer's states and event counts, the
// merge order of two writers, the bytes of an envelope and of a one-item snapshot.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

ItemInput mk(const std::string& key, const std::string& title, const std::string& kind = "error")
{
    ItemInput in;
    in.item.key = key;
    in.item.title = title;
    in.item.kind = kind;
    in.item.detail = "detail of " + key;
    return in;
}

std::vector<std::string> keysOf(const Store& s, const Query& q = {})
{
    std::vector<std::string> out;
    for (const StoredItem* it : s.items(q)) out.push_back(it->item.key);
    return out;
}

Event putEvent(const std::string& ns, const std::string& writer, std::uint64_t lamport, const std::string& key, const std::string& title)
{
    Event e;
    e.kind = "item.put@1";
    e.writer = {ns, writer};
    e.lamport = lamport;
    e.subject = key;
    e.payload = R"({"layer":"issues","source":"s","kind":"error","title":")" + title + R"(","detail":""})";
    return e;
}

std::string lines(const std::vector<Event>& events)
{
    std::string out;
    for (const Event& e : events) out += eventJson(e) + "\n";
    return out;
}

/// A note kind registered from outside the core, as B2 and B3 will: its own file, its own state, no edit to store.cpp.
struct NoteCount { int notes = 0; std::string last; };

}  // namespace

int main()
{
    {
        // The envelope (D2): the bytes, the round trip, the members a version does not know, and no `seq` or `type` (the journal-1 collision, A22).
        Event e;
        e.kind = "item.tag@1";
        e.writer = {"proj", "w-1"};
        e.lamport = 7;
        e.subject = "k";
        e.time = "2026-11-24T10:00:00Z";
        e.payload = R"({"tag":"x"})";
        const std::string expected = R"({"kind":"item.tag@1","writer":{"namespace":"proj","writer":"w-1"},"lamport":7,"subject":"k","time":"2026-11-24T10:00:00Z","payload":{"tag":"x"}})";
        CHECK_EQ(eventJson(e), expected);
        const auto back = parseEvent(expected);
        CHECK(back.has_value());
        if (back) CHECK(*back == e);
        CHECK(expected.find("\"seq\"") == std::string::npos && expected.find("\"type\"") == std::string::npos);
        const KindRef k = splitKind(e.kind);
        CHECK_EQ(std::string(k.name), std::string("item.tag"));
        CHECK_EQ(std::string(k.version), std::string("1"));
        CHECK_EQ(std::string(splitKind("bare").version), std::string());
        // Payload text with its own spacing, and an unknown envelope member, come back exactly (A7).
        const std::string odd = R"({"kind":"x.y@2","writer":{"namespace":"n","writer":"w"},"lamport":3,"subject":"s","time":"","payload":{ "a" : [1, 2] , "z":null },"future":{"q":1},"flag":true})";
        const auto parsedOdd = parseEvent(odd);
        CHECK(parsedOdd.has_value());
        if (parsedOdd) {
            CHECK_EQ(parsedOdd->payload, std::string(R"({ "a" : [1, 2] , "z":null })"));
            CHECK_EQ(parsedOdd->extra, std::string(R"("future":{"q":1},"flag":true)"));
            CHECK_EQ(eventJson(*parsedOdd), odd);
        }
        CHECK(!parseEvent(R"({"writer":{"namespace":"n","writer":"w"},"lamport":1})").has_value());   // no kind
        CHECK(!parseEvent(R"({"kind":"a@1","lamport":1})").has_value());                                // no writer
        CHECK(!parseEvent(R"({"kind":"a@1","writer":{"namespace":"n","writer":"w"}})").has_value());    // no lamport
        CHECK(!parseEvent("not json").has_value());
    }
    {
        // eventOrder: lamport first, then writer (namespace, then id); equal only for the same event.
        const Event a = putEvent("p", "w1", 2, "k", "a"), b = putEvent("p", "w2", 2, "k", "b"), c = putEvent("q", "w0", 2, "k", "c"), d = putEvent("p", "w9", 3, "k", "d");
        CHECK(eventOrder(a, b) < 0 && eventOrder(b, c) < 0 && eventOrder(c, d) < 0 && eventOrder(a, a) == 0);
    }
    {
        // R-BB-2, R-BB-3: a re-derived issues layer over 5 turns. Hand-written truth, by turn:
        //   1: A B C offered        -> three opens                                  (3 events: 3 puts)
        //   2: A C                  -> B vanished: done by the source               (1 event)
        //   3: A B D                -> B comes back: open; C vanished: done; D new  (3 events: put D, B open, C done)
        //   4: A B(title changed) D -> one put                                      (1 event)
        //   5: B D                  -> A vanished: done                             (1 event)
        // Nine events; four items, never a duplicate, nothing deleted; A done, B open, C done, D open.
        Store s;
        auto turn = [&](std::vector<ItemInput> items) { return s.putLayer("issues", "squad/gate", items); };
        PutResult r = turn({mk("A", "a"), mk("B", "b"), mk("C", "c")});
        CHECK_EQ(r.added, std::size_t{3});
        CHECK_EQ(s.events().size(), std::size_t{3});
        r = turn({mk("A", "a"), mk("C", "c")});
        CHECK_EQ(r.vanished, std::size_t{1});
        CHECK(s.item("B")->state == ItemState::done);
        CHECK_EQ(s.item("B")->lastTransition->actor, std::string("squad/gate"));
        CHECK_EQ(s.item("B")->lastTransition->reason, std::string("vanished"));
        CHECK_EQ(s.events().size(), std::size_t{4});
        r = turn({mk("A", "a"), mk("B", "b"), mk("D", "d")});
        CHECK_EQ(r.added, std::size_t{1});
        CHECK_EQ(r.reopened, std::size_t{1});
        CHECK_EQ(r.vanished, std::size_t{1});
        CHECK_EQ(s.events().size(), std::size_t{7});
        r = turn({mk("A", "a"), mk("B", "b renamed"), mk("D", "d")});
        CHECK_EQ(r.changed, std::size_t{1});
        CHECK_EQ(s.events().size(), std::size_t{8});
        r = turn({mk("B", "b renamed"), mk("D", "d")});
        CHECK_EQ(r.vanished, std::size_t{1});
        CHECK_EQ(s.events().size(), std::size_t{9});
        CHECK_EQ(s.items().size(), std::size_t{4});
        CHECK(s.item("A")->state == ItemState::done);
        CHECK(s.item("B")->state == ItemState::open);
        CHECK(s.item("C")->state == ItemState::done);
        CHECK(s.item("D")->state == ItemState::open);
        CHECK_EQ(s.item("B")->item.title, std::string("b renamed"));
        CHECK_EQ(s.numberOf("A") * 1000 + s.numberOf("B") * 100 + s.numberOf("C") * 10 + s.numberOf("D"), 1234);
        // An unchanged resubmission costs no event at all.
        r = turn({mk("B", "b renamed"), mk("D", "d")});
        CHECK_EQ(r.unchanged, std::size_t{2});
        CHECK_EQ(r.added + r.changed + r.reopened + r.vanished, std::size_t{0});
        CHECK_EQ(s.events().size(), std::size_t{9});
        // A source touches only its own items in the layer.
        CHECK_EQ(s.putLayer("issues", "other/source", {mk("X", "x")}).added, std::size_t{1});
        CHECK_EQ(s.putLayer("issues", "squad/gate", std::vector<ItemInput>{}).vanished, std::size_t{2});   // B and D go; X stays
        CHECK(s.item("X")->state == ItemState::open);
    }
    {
        // Reappearance by state: verified reopens (a regression), dropped stays dropped (a person's decision), held stays held; a vanished held item is done.
        Store s;
        (void)s.putLayer("L", "src", {mk("v", "v"), mk("d", "d"), mk("h", "h")});
        CHECK(s.setState("v", ItemState::verified, "person/craig").value());
        CHECK(s.setState("d", ItemState::dropped, "person/craig", "not a bug").value());
        CHECK(s.setState("h", ItemState::held, "squad/worker/app#2").value());
        const PutResult r = s.putLayer("L", "src", {mk("v", "v"), mk("d", "d"), mk("h", "h")});
        CHECK_EQ(r.reopened, std::size_t{1});
        CHECK(s.item("v")->state == ItemState::open);
        CHECK(s.item("d")->state == ItemState::dropped);
        CHECK(s.item("h")->state == ItemState::held);
        CHECK_EQ(s.putLayer("L", "src", std::vector<ItemInput>{}).vanished, std::size_t{2});   // v (open) and h (held); d is dropped already
        CHECK(s.item("h")->state == ItemState::done && s.item("d")->state == ItemState::dropped);
    }
    {
        // R-BB-3: a transition names its actor; the same state makes no event; an unknown item is an error; orphaned is derived and never a state.
        Store s;
        ItemInput in = mk("k", "t");
        Anchor a;
        a.blockId = "b1";
        in.item.anchors = {a};
        (void)s.putLayer("L", "src", {in, mk("free", "unanchored")});
        CHECK(!s.setState("k", ItemState::done, "").has_value());
        CHECK(!s.setState("nope", ItemState::done, "x").has_value());
        const std::size_t before = s.events().size();
        CHECK(!s.setState("k", ItemState::open, "x").value());
        CHECK_EQ(s.events().size(), before);
        CHECK(s.setState("k", ItemState::held, "who", "claimed").value());
        CHECK(s.item("k")->lastTransition->from == ItemState::open && s.item("k")->lastTransition->to == ItemState::held);
        CHECK_EQ(stateName(ItemState::verified), std::string_view("verified"));
        CHECK(!parseState("orphaned").has_value());
        CHECK(isOrphaned(*s.item("k"), [](const Anchor&) { return false; }));
        CHECK(!isOrphaned(*s.item("k"), [](const Anchor& x) { return x.blockId == "b1"; }));
        CHECK(!isOrphaned(*s.item("free"), [](const Anchor&) { return false; }));   // no anchors: waiting, not orphaned
    }
    {
        // Tags, links (open string kinds, A5), the reverse index, and a thread as items with links (R-COL-2).
        Store s;
        (void)s.putLayer("review", "gh", {mk("c/1", "comment"), mk("c/2", "reply one"), mk("c/3", "reply two")});
        CHECK(s.link("c/2", "repliesTo", "c/1", "gh/user/x").value());
        CHECK(s.link("c/3", "repliesTo", "c/1", "gh/user/y").value());
        CHECK(!s.link("c/3", "repliesTo", "c/1", "gh/user/y").value());   // already stated
        CHECK(s.link("c/3", "x-custom-kind", "somewhere:else", "gh/user/y").value());   // an open string
        const std::vector<Backlink> thread = s.linksTo("c/1");
        CHECK_EQ(thread.size(), std::size_t{2});
        if (thread.size() == 2) CHECK(thread[0].fromKey == "c/2" && thread[1].fromKey == "c/3" && thread[1].kind == "repliesTo");
        CHECK(s.unlink("c/2", "repliesTo", "c/1", "gh/user/x").value());
        CHECK_EQ(s.linksTo("c/1").size(), std::size_t{1});
        CHECK(!s.link("c/2", "", "c/1", "a").has_value());
        CHECK(!s.link("missing", "k", "c/1", "a").has_value());
        CHECK(s.tag("c/1", "owner/parser", "person/craig").value());
        CHECK(!s.tag("c/1", "owner/parser", "person/craig").value());
        CHECK(!s.tag("c/1", "Upper", "x").has_value());
        CHECK(!s.tag("c/1", "has space", "x").has_value());
        CHECK(s.untag("c/1", "owner/parser", "person/craig").value());
        CHECK(s.item("c/1")->tags.empty());
        CHECK(!s.untag("c/1", "owner/parser", "person/craig").value());
    }
    {
        // R-EXP-2: numeric per-block data rides on an item verbatim, with no mechanism per metric; a change of it is a change of the item.
        Store s;
        ItemInput cov = mk("cov/a.cpp", "coverage");
        cov.data = R"({"lines": 120,"covered":97,"ratio":0.808})";
        cov.detailFormat = "markdown";
        CHECK_EQ(s.putLayer("coverage", "ci", {cov}).added, std::size_t{1});
        CHECK_EQ(s.item("cov/a.cpp")->data, cov.data);
        CHECK_EQ(s.item("cov/a.cpp")->detailFormat, std::string("markdown"));
        CHECK_EQ(s.putLayer("coverage", "ci", {cov}).unchanged, std::size_t{1});
        cov.data = R"({"lines":120,"covered":98})";
        CHECK_EQ(s.putLayer("coverage", "ci", {cov}).changed, std::size_t{1});
        ItemInput bad = mk("cov/b.cpp", "bad");
        bad.data = "[1,2";
        CHECK_EQ(s.putLayer("coverage", "ci", {cov, bad}).rejected, std::size_t{1});
        CHECK(s.item("cov/b.cpp") == nullptr);
        ItemInput badUpdate = cov;
        badUpdate.data = "{oops";
        CHECK_EQ(s.putLayer("coverage", "ci", {badUpdate}).rejected, std::size_t{1});   // offered but unusable: the stored item is kept, not vanished
        CHECK(s.item("cov/a.cpp")->state == ItemState::open);
        CHECK_EQ(s.item("cov/a.cpp")->data, cov.data);
        ItemInput empty = mk("", "no key");
        CHECK_EQ(s.putLayer("coverage", "ci", {cov, empty}).rejected, std::size_t{1});
    }
    {
        // A6: authorship is read from the log (first and latest put), never written by a producer. The clock is injected, so the test reads none.
        int tick = 0;
        StoreOptions o;
        o.writer = {"proj", "w-7"};
        o.clock = [&] { return "T" + std::to_string(++tick); };
        Store s(o);
        (void)s.putLayer("L", "src", {mk("k", "first")});            // lamport 1, T1
        (void)s.setState("k", ItemState::held, "who");               // lamport 2, T2: a transition is not a put
        (void)s.putLayer("L", "src", {mk("k", "second")});           // lamport 3, T3
        const StoredItem* it = s.item("k");
        CHECK_EQ(it->created.lamport, std::uint64_t{1});
        CHECK_EQ(it->created.time, std::string("T1"));
        CHECK_EQ(it->updated.lamport, std::uint64_t{3});
        CHECK_EQ(it->updated.writer.writer, std::string("w-7"));
        CHECK_EQ(it->lastTransition->at.lamport, std::uint64_t{2});
        CHECK_EQ(s.head(), std::uint64_t{3});
    }
    {
        // R-BB-1 and R-BB-8: replay equals the live store byte for byte, and the events as JSON lines (the messages) rebuild it in a store that never saw the calls.
        MemoryLog log;
        StoreOptions o;
        o.writer = {"proj", "w-1"};
        o.sink = &log;
        int tick = 0;
        o.clock = [&] { return "t" + std::to_string(++tick); };
        Store live(o);
        for (int turn = 0; turn < 5; ++turn) {
            std::vector<ItemInput> items;
            for (int i = 0; i < 6; ++i) if ((i + turn) % 4 != 0) items.push_back(mk("k" + std::to_string(i), "t" + std::to_string(i * turn)));
            (void)live.putLayer("issues", "squad/gate", items);
        }
        (void)live.tag("k1", "ready-for-implementation", "squad/planner");
        (void)live.link("k2", "causes", "k1", "squad/planner");
        (void)live.setState("k3", ItemState::verified, "person/craig", "checked");
        (void)live.setState("k1", ItemState::dropped, "person/craig");
        CHECK_EQ(log.size(), live.events().size());   // the sink saw exactly what the store made
        const std::string liveSnap = live.snapshotJson();

        Store replayed;
        replayed.replay(log);
        CHECK_EQ(replayed.snapshotJson(), liveSnap);
        CHECK(replayed.events() == live.events());

        Store viaMessages;   // each event as a line, parsed back: the serialisable message of R-BB-8
        std::vector<Event> parsed;
        for (const Event& e : log.events()) {
            const auto p = parseEvent(eventJson(e));
            CHECK(p.has_value());
            if (p) parsed.push_back(*p);
        }
        viaMessages.merge(parsed);
        CHECK_EQ(viaMessages.snapshotJson(), liveSnap);

        // Whatever order the events arrive in, the same store results.
        std::vector<Event> reversed(log.events().rbegin(), log.events().rend());
        Store fromReversed;
        fromReversed.merge(reversed);
        CHECK_EQ(fromReversed.snapshotJson(), liveSnap);

        // Catching up: a second replay from the position already read adds nothing, and overlapping reads are harmless.
        replayed.replay(log, log.size());
        replayed.replay(log);
        CHECK_EQ(replayed.snapshotJson(), liveSnap);
        CHECK_EQ(replayed.events().size(), live.events().size());
    }
    {
        // The bytes of a one-item snapshot, from the format as specified.
        StoreOptions o;
        o.writer = {"p", "w1"};
        Store s(o);
        ItemInput in;
        in.item.key = "k"; in.item.kind = "error"; in.item.title = "T"; in.item.detail = "D";
        (void)s.putLayer("issues", "s", {in});
        const std::string who = R"({"writer":{"namespace":"p","writer":"w1"},"lamport":1,"time":""})";
        CHECK_EQ(s.snapshotJson(), std::string(R"({"schema":"store-snapshot-1","head":1,"events":1,"layers":{"issues":[{"number":1,"key":"k","source":"s","kind":"error","title":"T","detail":"D",)"
                                               R"("state":"open","tags":[],"links":[],"anchors":[],"created":)") + who + R"(,"updated":)" + who + "}]}}");
    }
    {
        // R-BB-9: an unknown kind, an unknown envelope member and an unknown payload field survive a replay untouched, in the events and in what is written back.
        const std::string l1 = R"({"kind":"item.put@1","writer":{"namespace":"n","writer":"w"},"lamport":1,"subject":"k","time":"t1","payload":{"layer":"L","source":"s","kind":"e","title":"T","detail":"","fromTheFuture":{"a":[1,2,3]}},"envelopeFuture":"x"})";
        const std::string l2 = R"({"kind":"design.vote@3","writer":{"namespace":"n","writer":"w"},"lamport":2,"subject":"k","time":"t2","payload":{ "votes" : [ 1 , 2 ] ,"nested":{"deep":null}}})";
        const std::string l3 = R"({"kind":"item.put@9","writer":{"namespace":"n","writer":"w"},"lamport":3,"subject":"k","time":"t3","payload":{"layer":"CHANGED"}})";   // a newer version of a known kind
        std::vector<Event> in;
        for (const std::string* l : {&l1, &l2, &l3}) { auto p = parseEvent(*l); CHECK(p.has_value()); if (p) in.push_back(*p); }
        MemoryLog log;
        for (const Event& e : in) log.append(e);
        Store s;
        s.replay(log);
        CHECK_EQ(lines(s.events()), l1 + "\n" + l2 + "\n" + l3 + "\n");
        CHECK_EQ(s.state().unknownKinds.size(), std::size_t{2});
        CHECK_EQ(s.state().unknownKinds.at("design.vote@3"), std::size_t{1});
        CHECK_EQ(s.item("k")->item.layer, std::string("L"));   // the unknown version did not touch the item
        CHECK_EQ(s.items().size(), std::size_t{1});
        // Written back to a new log and replayed again: the same bytes.
        MemoryLog out;
        for (const Event& e : s.events()) out.append(e);
        Store again;
        again.replay(out);
        CHECK_EQ(lines(again.events()), lines(s.events()));
        CHECK_EQ(again.snapshotJson(), s.snapshotJson());
        // The store's own next event sorts after what it holds.
        CHECK_EQ(s.emit("note.x@1", "k", "{}").lamport, std::uint64_t{4});
    }
    {
        // Concurrent writers order by (lamport, writer), not arrival. By hand: A(p/w1) and B(p/w2) both at lamport 1 put key k; A sorts first, so B's title
        // is the later write and wins. C (q/w0) at lamport 2 sorts after both whatever arrives first.
        const Event a = putEvent("p", "w1", 1, "k", "from A"), b = putEvent("p", "w2", 1, "k", "from B"), c = putEvent("q", "w0", 2, "k", "from C");
        const std::vector<std::vector<Event>> arrivals = {{a, b, c}, {b, a, c}, {c, b, a}, {c, a, b}, {b, c, a}, {a, c, b}};
        for (const auto& order : arrivals) {
            Store s;
            s.merge(order);   // one batch
            CHECK_EQ(s.item("k")->item.title, std::string("from C"));
            CHECK(s.events()[0].writer.writer == "w1" && s.events()[1].writer.writer == "w2" && s.events()[2].writer.ns == "q");
            Store live;       // one at a time, late arrivals rebuilding the fold
            for (const Event& e : order) CHECK(live.append(e));
            CHECK_EQ(live.snapshotJson(), s.snapshotJson());
            CHECK(!live.append(a));   // a second delivery of the same event is ignored
            CHECK_EQ(live.events().size(), std::size_t{3});
        }
        Store ab;
        ab.merge({b, a});
        CHECK_EQ(ab.item("k")->item.title, std::string("from B"));   // A then B: B wins
        CHECK_EQ(ab.item("k")->created.writer.writer, std::string("w1"));
        // The same writer id in two namespaces is two writers (D6): namespace breaks the tie first.
        const Event pw = putEvent("a-proj", "w", 5, "k", "ns a"), qw = putEvent("b-proj", "w", 5, "k", "ns b");
        Store ns;
        ns.merge({qw, pw});
        CHECK_EQ(ns.events()[0].writer.ns, std::string("a-proj"));
        CHECK_EQ(ns.item("k")->item.title, std::string("ns b"));
        // A store that has merged another writer's lamport 9 stamps its next event 10.
        Store clock;
        clock.merge({putEvent("p", "w1", 9, "k", "x")});
        CHECK_EQ(clock.emit("item.tag@1", "k", R"({"tag":"t"})").lamport, std::uint64_t{10});
    }
    {
        // R-BB-10: a snapshot takes a query; a private item never leaves unless asked for by name; scoping keeps READ ISSUE numbers.
        Store s;
        (void)s.putLayer("issues", "gate", {mk("i1", "one"), mk("i2", "two"), mk("i3", "three")});
        (void)s.putLayer("notes", "person/craig", {mk("n1", "a private thought", "note"), mk("n2", "a public note", "note")});
        CHECK(s.tag("n1", "private/craig", "person/craig").value());
        CHECK(s.tag("i2", "ready", "x").value());
        CHECK_EQ(keysOf(s).size(), std::size_t{4});   // the default query hides n1
        const std::string all = s.snapshotJson();
        CHECK(all.find("a private thought") == std::string::npos);
        CHECK(all.find("private/craig") == std::string::npos);
        CHECK(all.find("a public note") != std::string::npos);
        Query ask;
        ask.tags = {"private/craig"};
        CHECK_EQ(keysOf(s, ask), std::vector<std::string>{"n1"});
        CHECK(s.snapshotJson(ask).find("a private thought") != std::string::npos);
        Query layer;
        layer.layers = {"issues"};
        layer.tags = {"ready"};
        CHECK_EQ(keysOf(s, layer), std::vector<std::string>{"i2"});
        const std::string scoped = s.snapshotJson(layer);
        CHECK(scoped.find("\"number\":2,\"key\":\"i2\"") != std::string::npos);   // i2 is still issue 2
        CHECK(scoped.find("\"notes\"") == std::string::npos);
        Query noHiding;
        noHiding.hiddenTagPrefixes.clear();
        CHECK_EQ(keysOf(s, noHiding).size(), std::size_t{5});   // policy as data: a client may change the prefixes
        Query text;
        text.text = "thought";
        text.hiddenTagPrefixes.clear();
        CHECK_EQ(keysOf(s, text), std::vector<std::string>{"n1"});
        Query states;
        states.states = {"open"};
        states.sources = {"gate"};
        CHECK_EQ(keysOf(s, states).size(), std::size_t{3});
    }
    {
        // The registry is the seam B2 and B3 use: a kind added from outside the core is folded, replayed, and unknown until registered.
        ReducerRegistry registry = ReducerRegistry::core();
        CHECK(registry.add("note.add@1", [](StoreState& st, const Event& e) {
            NoteCount& n = st.ext<NoteCount>();
            ++n.notes;
            n.last = e.subject;
        }));
        CHECK(!registry.add("note.add@1", [](StoreState&, const Event&) {}));   // a kind has one reducer
        StoreOptions o;
        o.registry = registry;
        MemoryLog log;
        o.sink = &log;
        Store s(o);
        (void)s.emit("note.add@1", "k1", "{}");
        (void)s.emit("note.add@1", "k2", "{}");
        (void)s.emit("note.add@2", "k3", "{}");   // another version: no reducer, kept
        CHECK_EQ(s.state().unknownKinds.size(), std::size_t{1});
        StoreOptions o2;
        o2.registry = registry;
        Store replayed(o2);
        replayed.replay(log);
        const NoteCount* n = replayed.state().extIf<NoteCount>();
        CHECK(n != nullptr);
        if (n) { CHECK_EQ(n->notes, 2); CHECK_EQ(n->last, std::string("k2")); }
        Store plain;   // the core alone does not know the kind
        plain.replay(log);
        CHECK_EQ(plain.state().unknownKinds.size(), std::size_t{2});
        // An out-of-order arrival rebuilds the extension state too: it is part of the fold.
        Store late(o2);
        Event x;
        x.kind = "note.add@1"; x.writer = {"z", "w"}; x.lamport = 5; x.subject = "later";
        Event y = x;
        y.lamport = 2; y.subject = "earlier";
        (void)late.append(x);
        (void)late.append(y);
        const NoteCount* ln = late.state().extIf<NoteCount>();
        CHECK(ln != nullptr);
        if (ln) { CHECK_EQ(ln->notes, 2); CHECK_EQ(ln->last, std::string("later")); }
    }
    {
        // R-BB-7: with no clock supplied the store makes no time; two stores given the same calls write the same bytes; a store with no sink writes nowhere.
        auto build = [] {
            Store s;
            (void)s.putLayer("L", "src", {mk("a", "x"), mk("b", "y")});
            (void)s.tag("a", "t", "me");
            return s;
        };
        // (a Store is not copyable; build twice through the lambda's guaranteed elision)
        Store one = build(), two = build();
        CHECK_EQ(one.snapshotJson(), two.snapshotJson());
        CHECK_EQ(one.events()[0].time, std::string());
        MemoryLog sink;
        StoreOptions o;
        o.sink = &sink;
        Store withSink(o);
        std::vector<Event> foreign = {putEvent("x", "y", 1, "k", "t")};
        withSink.merge(foreign);
        CHECK_EQ(sink.size(), std::uint64_t{0});   // merged events are not written back
        (void)withSink.emit("item.tag@1", "k", R"({"tag":"t"})");
        CHECK_EQ(sink.size(), std::uint64_t{1});
    }
    return finish();
}
