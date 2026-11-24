// Subscriptions and inboxes (slice B2): requirements R-BB-4, R-BB-5 and, through the new event kinds, R-BB-1, R-BB-8 and R-BB-9 (docs/REQUIREMENTS.md).
// The expected values are written by hand from the requirements: which events and items each subscriber sees at each step, the open set of the 5-turn layer, and
// the bytes of an inbox message, never read back from the code under test.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/inbox.hpp"
#include "sub0canvas/store.hpp"
#include "bench/rng.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

StoreOptions options(MemoryLog* sink = nullptr, const char* writer = "w-1")
{
    StoreOptions o;
    o.writer = {"proj", writer};
    o.sink = sink;
    (void)registerSubscriptionReducers(o.registry);
    return o;
}

ItemInput mk(const std::string& key, const std::string& title, const std::string& path = {}, const std::string& block = {})
{
    ItemInput in;
    in.item.key = key;
    in.item.title = title;
    in.item.kind = "error";
    in.item.detail = "detail of " + key;
    if (!path.empty() || !block.empty()) {
        Anchor a;
        a.path = path;
        a.blockId = block;
        in.item.anchors = {a};
    }
    return in;
}

SubscriptionQuery layerQuery(const std::string& layer)
{
    SubscriptionQuery q;
    q.layers = {layer};
    return q;
}

std::vector<std::string> kindsOf(const Inbox& in)
{
    std::vector<std::string> out;
    for (const Event& e : in.events) out.push_back(e.kind + " " + e.subject);
    return out;
}

std::vector<std::string> keysOf(const Inbox& in)
{
    std::vector<std::string> out;
    for (const LayerItem& i : in.items) out.push_back(i.key);
    return out;
}

using Strings = std::vector<std::string>;

std::string lines(const std::vector<Event>& events)
{
    std::string out;
    for (const Event& e : events) out += eventJson(e) + "\n";
    return out;
}

}  // namespace

int main()
{
    {
        // R-BB-4: an agent sees only what it subscribed to; what it sees stays until it acknowledges, then it is gone; a later change shows exactly once.
        //   log: 1-3 put issues a b c (squad)   4 put review r1   5 subscribe agent-a (issues)   6 subscribe agent-b (review)
        Store s(options());
        (void)s.putLayer("issues", "squad/gate", {mk("a", "a"), mk("b", "b"), mk("c", "c")});
        (void)s.putLayer("review", "person/craig", {mk("r1", "note one")});
        CHECK(subscribe(s, "agent-a", "agent/a", layerQuery("issues")).has_value());
        CHECK(subscribe(s, "agent-b", "agent/b", layerQuery("review")).has_value());
        CHECK_EQ(s.head(), std::uint64_t{6});

        InboxReader reader(s);
        auto a = reader.inbox("agent-a");
        CHECK(a.has_value());
        CHECK_EQ(kindsOf(*a), (Strings{"item.put@1 a", "item.put@1 b", "item.put@1 c"}));
        CHECK_EQ(keysOf(*a), (Strings{"a", "b", "c"}));
        CHECK_EQ(a->items[1].number, 2);
        CHECK_EQ(a->upTo, std::uint64_t{6});
        auto b = reader.inbox("agent-b");
        CHECK_EQ(kindsOf(*b), (Strings{"item.put@1 r1"}));
        CHECK_EQ(keysOf(*b), (Strings{"r1"}));

        // Reading is not acknowledging: the same inbox again.
        CHECK_EQ(kindsOf(*reader.inbox("agent-a")), kindsOf(*a));

        // Acknowledge: gone. The acknowledgement and subscription events are not in the inbox.
        CHECK(*acknowledge(s, "agent-a", a->upTo));
        CHECK(reader.inbox("agent-a")->events.empty());
        CHECK(reader.inbox("agent-a")->items.empty());
        CHECK_EQ(findSubscription(s, "agent-a")->cursor, std::uint64_t{6});

        // A change to b only (a review note arriving too, which agent-a did not subscribe to): exactly the one event, once.
        (void)s.putLayer("review", "person/craig", {mk("r1", "note one"), mk("r2", "note two")});
        CHECK(s.setState("b", ItemState::held, "person/craig", "wait").value());
        auto a2 = reader.inbox("agent-a");
        CHECK_EQ(kindsOf(*a2), (Strings{"item.state@1 b"}));
        CHECK_EQ(keysOf(*a2), (Strings{"b"}));
        CHECK(a2->items[0].layer == "issues");
        CHECK(*acknowledge(s, "agent-a", a2->upTo));
        CHECK(reader.inbox("agent-a")->events.empty());

        // Two subscribers are independent: agent-b never acknowledged, so it still has its first event and the new note.
        CHECK_EQ(kindsOf(*reader.inbox("agent-b")), (Strings{"item.put@1 r1", "item.put@1 r2"}));
        CHECK_EQ(findSubscription(s, "agent-b")->cursor, std::uint64_t{0});
        CHECK(*acknowledge(s, "agent-b", reader.inbox("agent-b")->upTo));
        CHECK(reader.inbox("agent-b")->events.empty());
        CHECK_EQ(subscriptionsOf(s, "agent/a").size(), std::size_t{1});
        CHECK_EQ(subscriptionsOf(s, "nobody").size(), std::size_t{0});
    }
    {
        // Acknowledge is idempotent and monotone; its errors; subscribe again keeps the cursor; unsubscribe ends it.
        Store s(options());
        (void)s.putLayer("issues", "squad/gate", {mk("a", "a")});
        CHECK(subscribe(s, "w", "worker", layerQuery("issues")).has_value());
        const Inbox first = *inbox(s, "w");
        CHECK(*acknowledge(s, "w", first.upTo));
        const std::size_t after = s.events().size();
        CHECK(!*acknowledge(s, "w", first.upTo));           // again: nothing
        CHECK(!*acknowledge(s, "w", 1));                    // an older position: nothing, the cursor never moves back
        CHECK_EQ(s.events().size(), after);
        CHECK_EQ(findSubscription(s, "w")->cursor, first.upTo);
        CHECK(!acknowledge(s, "w", s.head() + 1).has_value());    // beyond the head: it would hide events not yet made
        CHECK(!acknowledge(s, "ghost", 1).has_value());
        CHECK(!inbox(s, "ghost").has_value());
        CHECK(!subscribe(s, "", "worker", {}).has_value());
        CHECK(!subscribe(s, "x", "", {}).has_value());
        // The same id again replaces the query and keeps the cursor.
        SubscriptionQuery review = layerQuery("review");
        CHECK(subscribe(s, "w", "worker", review).has_value());
        CHECK_EQ(findSubscription(s, "w")->cursor, first.upTo);
        CHECK(findSubscription(s, "w")->query.layers.contains("review"));
        CHECK(!findSubscription(s, "w")->query.layers.contains("issues"));
        // A new subscription may start at the head: it sees only what comes next.
        CHECK(subscribe(s, "tail", "worker", layerQuery("issues"), s.head()).has_value());
        CHECK(inbox(s, "tail")->events.empty());
        (void)s.setState("a", ItemState::done, "squad/gate");
        CHECK_EQ(kindsOf(*inbox(s, "tail")), (Strings{"item.state@1 a"}));
        CHECK(*unsubscribe(s, "tail"));
        CHECK(!*unsubscribe(s, "tail"));
        CHECK(findSubscription(s, "tail") == nullptr);
        // A store whose registry lacks the reducers keeps the event but says the subscription has no effect.
        Store plain;
        auto r = subscribe(plain, "x", "o", {});
        CHECK(!r.has_value());
        CHECK_EQ(plain.events().size(), std::size_t{1});
    }
    {
        // The filters, each against hand-written truth. Items: p1 (issues, src/canvas/a.cpp, block B1, tagged), p2 (issues, src/canvas_ext.cpp, B2),
        // p3 (issues, lib/x.cpp, B3, private tag), r1 (review, src/canvas/a.cpp, B1).
        Store s(options());
        (void)s.putLayer("issues", "squad/gate", {mk("p1", "alpha", "src/canvas/a.cpp", "B1"), mk("p2", "beta", "src/canvas_ext.cpp", "B2"), mk("p3", "gamma", "lib/x.cpp", "B3")});
        (void)s.putLayer("review", "person/craig", {mk("r1", "alpha review", "src/canvas/a.cpp", "B1")});
        CHECK(s.tag("p1", "area/canvas", "me").value());
        CHECK(s.tag("p3", "private/me", "me").value());
        CHECK(s.setState("p2", ItemState::done, "squad/gate").value());
        // log: 1 p1  2 p2  3 p3  4 r1  5 tag p1  6 tag p3  7 state p2
        auto read = [&](const SubscriptionQuery& q) {
            (void)subscribe(s, "q", "tester", q);
            Inbox in = *inbox(s, "q");
            (void)unsubscribe(s, "q");
            return in;
        };
        SubscriptionQuery q;
        q.paths = {"src/canvas"};   // a prefix: p1, r1 and (by string prefix) p2
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.put@1 p1", "item.put@1 p2", "item.put@1 r1", "item.tag@1 p1", "item.state@1 p2"}));
        q = {};
        q.paths = {"src/canvas/"};
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.put@1 p1", "item.put@1 r1", "item.tag@1 p1"}));
        q = {};
        q.blocks = {"B1"};
        CHECK_EQ(keysOf(read(q)), (Strings{"p1", "r1"}));   // layer order: issues, then review
        q.layers = {"review"};
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.put@1 r1"}));   // blocks and layers together
        q = {};
        q.events = {"item.state"};   // the bare name takes every version
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.state@1 p2"}));
        q.events = {"item.state@2"};
        CHECK(read(q).events.empty());
        q = {};
        q.layers = {"issues"};
        q.states = {"done"};
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.put@1 p2", "item.state@1 p2"}));   // the item's state now selects its older events too
        q = {};
        q.tags = {"area/canvas"};
        CHECK_EQ(kindsOf(read(q)), (Strings{"item.put@1 p1", "item.tag@1 p1"}));
        q = {};
        q.text = "alpha";
        CHECK_EQ(keysOf(read(q)), (Strings{"p1", "r1"}));
        // A private item is delivered only to a subscription that names its tag (R-BB-10).
        q = {};
        q.layers = {"issues"};
        CHECK_EQ(keysOf(read(q)), (Strings{"p1", "p2"}));
        q.tags = {"private/me"};
        CHECK_EQ(keysOf(read(q)), (Strings{"p3"}));
        // The empty query selects every event but the subscription kinds and the two about the private item: seven less two.
        CHECK_EQ(read({}).events.size(), std::size_t{5});
    }
    {
        // The subscription kinds are in an inbox only when asked for by name.
        Store s(options());
        (void)subscribe(s, "one", "o1", {});
        (void)subscribe(s, "two", "o2", layerQuery("issues"));
        SubscriptionQuery q;
        q.events = {"sub.subscribe"};
        CHECK(subscribe(s, "meta", "o3", q).has_value());
        CHECK_EQ(kindsOf(*inbox(s, "meta")), (Strings{"sub.subscribe@1 one", "sub.subscribe@1 two", "sub.subscribe@1 meta"}));
        CHECK(inbox(s, "one")->events.empty());
    }
    {
        // The item-level test is the one `Store::items` applies, query by query (the two must not drift: R-BB-10).
        Store s(options());
        (void)s.putLayer("issues", "squad/gate", {mk("p1", "alpha", "a.cpp", "B1"), mk("p2", "beta"), mk("p3", "gamma")});
        (void)s.putLayer("review", "person/craig", {mk("r1", "alpha review")});
        (void)s.tag("p1", "area/x", "me");
        (void)s.tag("p3", "private/me", "me");
        (void)s.setState("p2", ItemState::held, "me");
        std::vector<Query> queries(7);
        queries[1].layers = {"review"};
        queries[2].states = {"held", "open"};
        queries[3].tags = {"area/x"};
        queries[4].tags = {"private/me"};
        queries[5].text = "alpha";
        queries[6].sources = {"squad/gate"};
        queries[6].kinds = {"error"};
        for (const Query& q : queries) {
            Strings viaStore, viaFilter;
            for (const StoredItem* it : s.items(q)) viaStore.push_back(it->item.key);
            for (const char* key : {"p1", "p2", "p3", "r1"})
                if (itemMatches(*s.item(key), q)) viaFilter.push_back(key);
            std::sort(viaStore.begin(), viaStore.end());
            CHECK_EQ(viaStore, viaFilter);
        }
    }
    {
        // Replay equals live (R-BB-1) and the subscription kinds are messages (R-BB-8): a store that only reads the log has the same subscriptions, cursors and inboxes.
        MemoryLog log;
        Store live(options(&log));
        (void)live.putLayer("issues", "squad/gate", {mk("a", "a"), mk("b", "b")});
        (void)subscribe(live, "w", "worker", layerQuery("issues"));
        (void)acknowledge(live, "w", live.head());
        (void)live.setState("a", ItemState::done, "squad/gate");
        (void)subscribe(live, "v", "viewer", {});
        (void)subscribe(live, "gone", "viewer", {});
        (void)unsubscribe(live, "gone");
        const std::string liveInbox = lines(inbox(live, "w")->events);
        CHECK_EQ(liveInbox.size() > 0, true);

        Store replayed(options());
        replayed.replay(log);
        CHECK_EQ(replayed.snapshotJson(), live.snapshotJson());
        CHECK_EQ(lines(inbox(replayed, "w")->events), liveInbox);
        CHECK_EQ(findSubscription(replayed, "w")->cursor, findSubscription(live, "w")->cursor);
        CHECK(findSubscription(replayed, "gone") == nullptr);
        CHECK_EQ(findSubscription(replayed, "v")->owner, std::string("viewer"));

        // Any arrival order, and every event through its JSON line, gives the same.
        std::vector<Event> shuffled;
        for (const Event& e : log.events()) shuffled.push_back(*parseEvent(eventJson(e)));
        sub0canvas::rng::SplitMix64 rng(7);
        sub0canvas::rng::shuffle(shuffled, rng);   // deterministic: std::shuffle's mapping differs between standard libraries
        Store viaLines(options());
        viaLines.merge(shuffled);
        CHECK_EQ(lines(inbox(viaLines, "w")->events), liveInbox);
        CHECK_EQ(findSubscription(viaLines, "w")->cursor, findSubscription(live, "w")->cursor);

        // The acknowledge payload, byte for byte: the position is a lamport value, here the head (3) when "w" acknowledged.
        const Event* ack = nullptr;
        for (const Event& e : log.events()) if (e.kind == "sub.ack@1") ack = &e;
        CHECK(ack != nullptr);
        if (ack) { CHECK_EQ(ack->payload, std::string(R"({"upTo":3})")); CHECK_EQ(ack->subject, std::string("w")); }
    }
    {
        // Unsubscribed kinds survive replay (R-BB-9): a store with no subscription reducers keeps the three kinds, byte for byte, as unknown; a reader that
        // later knows them folds them; an event of a kind nobody knows is delivered to a subscription that names it, with its payload untouched.
        MemoryLog log;
        Store writer(options(&log));
        (void)writer.putLayer("issues", "squad/gate", {mk("a", "a")});
        (void)subscribe(writer, "w", "worker", layerQuery("issues"));
        (void)writer.emit("x.custom@1", "thing-1", R"({"keep":[1,2,{"deep":true}],"n":1.50})");
        (void)acknowledge(writer, "w", 2);

        Store old;   // the core's registry alone, as a build before B2
        old.replay(log);
        CHECK_EQ(lines(old.events()), lines(log.events()));
        CHECK_EQ(old.state().unknownKinds.at("sub.subscribe@1"), std::size_t{1});
        CHECK_EQ(old.state().unknownKinds.at("sub.ack@1"), std::size_t{1});
        CHECK_EQ(old.state().unknownKinds.at("x.custom@1"), std::size_t{1});
        CHECK(findSubscription(old, "w") == nullptr);

        // A store built later with the reducers reads what the old one held.
        Store newer(options());
        newer.merge(old.events());
        CHECK_EQ(findSubscription(newer, "w")->cursor, std::uint64_t{2});
        // The subscription on issues does not see the custom event (it is about no item); one that names the kind does, verbatim.
        CHECK(inbox(newer, "w")->events.empty());   // cursor 2: only the custom event and the ack are later
        SubscriptionQuery named;
        named.events = {"x.custom"};
        CHECK(subscribe(newer, "named", "o", named).has_value());
        auto got = inbox(newer, "named");
        CHECK_EQ(got->events.size(), std::size_t{1});
        CHECK_EQ(got->events[0].payload, std::string(R"({"keep":[1,2,{"deep":true}],"n":1.50})"));
        CHECK(got->items.empty());
        // And an item filter never selects it.
        SubscriptionQuery tagged;
        tagged.tags = {"anything"};
        CHECK(subscribe(newer, "tagged", "o", tagged).has_value());
        CHECK(inbox(newer, "tagged")->events.empty());
    }
    {
        // R-BB-5: the verdict a consumer builds through the inbox equals the direct path (Store::items + layerSummary/layerInline), byte for byte, on the 5-turn
        // scenario of the store's test. Hand-written truth of the open set after each turn, in layer order (places: A 1, B 2, C 3, D 4):
        //   1: A B C   2: A C   3: A B D   4: A B D   5: B D
        const Strings openAfter[5] = {{"A", "B", "C"}, {"A", "C"}, {"A", "B", "D"}, {"A", "B", "D"}, {"B", "D"}};
        const std::vector<std::vector<std::string>> offered = {{"A", "B", "C"}, {"A", "C"}, {"A", "B", "D"}, {"A", "B", "D"}, {"B", "D"}};
        const std::map<std::string, std::string> files;
        BlockIndex blocks;
        Store s(options());
        InboxReader persistent(s);   // one reader across the turns: its index extends as the log grows
        SubscriptionQuery verdict;   // the worker's verdict: the open issues the gate made
        verdict.layers = {"issues"};
        verdict.sources = {"squad/gate"};
        verdict.states = {"open"};
        Query direct;
        direct.layers = verdict.layers;
        direct.sources = verdict.sources;
        direct.states = verdict.states;
        for (int turn = 0; turn < 5; ++turn) {
            std::vector<ItemInput> items;
            for (const std::string& k : offered[turn]) items.push_back(mk(k, k == "B" && turn >= 3 ? "b renamed" : "title " + k));
            (void)s.putLayer("issues", "squad/gate", items);
            const std::string id = "verdict-" + std::to_string(turn);
            CHECK(subscribe(s, id, "agent/worker", verdict).has_value());

            std::vector<LayerItem> viaStore;
            for (const StoredItem* it : s.items(direct)) {
                LayerItem li = it->item;
                li.number = s.numberOf(li.key);
                viaStore.push_back(std::move(li));
            }
            InboxReader fresh(s);   // and one made for the turn
            for (InboxReader* reader : {&persistent, &fresh}) {
                auto in = reader->inbox(id);
                CHECK(in.has_value());
                CHECK_EQ(keysOf(*in), openAfter[turn]);
                CHECK_EQ(layerSummary(in->items, blocks), layerSummary(viaStore, blocks));
                CHECK_EQ(layerInline(in->items, blocks, files), layerInline(viaStore, blocks, files));
                CHECK_EQ(in->items.size(), viaStore.size());
                for (std::size_t i = 0; i < viaStore.size(); ++i) {
                    CHECK_EQ(in->items[i].key, viaStore[i].key);
                    CHECK_EQ(in->items[i].number, viaStore[i].number);
                    CHECK_EQ(in->items[i].title, viaStore[i].title);
                }
            }
        }
        // Turn 5, by hand: B (place 2, renamed) and D (place 4).
        auto last = persistent.inbox("verdict-4");
        CHECK_EQ(last->items[0].title, std::string("b renamed"));
        CHECK_EQ(last->items[0].number, 2);
        CHECK_EQ(last->items[1].number, 4);
        CHECK(layerSummary(last->items, blocks).starts_with("2 issues"));
    }
    {
        // The index follows the log and survives an older event arriving late: another writer's event with a lamport below the head sorts into the middle.
        Store s(options());
        (void)s.putLayer("issues", "squad/gate", {mk("a", "a"), mk("b", "b")});    // lamport 1, 2
        (void)subscribe(s, "w", "worker", layerQuery("issues"));                      // 3
        InboxReader reader(s);
        CHECK_EQ(reader.inbox("w")->events.size(), std::size_t{2});
        CHECK_EQ(reader.indexedEvents(), std::size_t{3});
        (void)s.setState("a", ItemState::held, "me");                                 // 4
        CHECK_EQ(reader.inbox("w")->events.size(), std::size_t{3});
        CHECK_EQ(reader.indexedEvents(), std::size_t{4});
        Event late;
        late.kind = "item.put@1";
        late.writer = {"proj", "w-0"};   // sorts before w-1 at an equal lamport
        late.lamport = 2;
        late.subject = "c";
        late.payload = R"({"layer":"issues","source":"s","kind":"error","title":"c","detail":""})";
        s.merge({late});
        auto after = reader.inbox("w");
        CHECK_EQ(kindsOf(*after), (Strings{"item.put@1 a", "item.put@1 c", "item.put@1 b", "item.state@1 a"}));
        CHECK_EQ(reader.indexedEvents(), std::size_t{5});
        CHECK_EQ(lines(after->events), lines(inbox(s, "w")->events));   // a fresh reader agrees with the one that rebuilt
        CHECK_EQ(reader.lastCandidates(), std::size_t{4});
    }
    {
        // The cost is the events returned: 200 events over 20 layers, each subscription on one layer examines only its layer's events.
        Store s(options());
        for (int l = 0; l < 20; ++l) {
            std::vector<ItemInput> items;
            for (int i = 0; i < 5; ++i) items.push_back(mk("k" + std::to_string(l) + "-" + std::to_string(i), "t"));
            (void)s.putLayer("L" + std::to_string(l), "src", items);
        }
        for (int l = 0; l < 20; ++l)
            for (int i = 0; i < 5; ++i) (void)s.tag("k" + std::to_string(l) + "-" + std::to_string(i), "t", "me");
        // 200 events so far: per layer 5 puts and 5 tags.
        CHECK(subscribe(s, "one", "o", layerQuery("L7")).has_value());
        InboxReader reader(s);
        auto in = reader.inbox("one");
        CHECK_EQ(in->events.size(), std::size_t{10});
        CHECK_EQ(reader.lastCandidates(), std::size_t{10});
        CHECK_EQ(in->items.size(), std::size_t{5});
        // After an acknowledgement the cursor skips the log by lamport value, not by scan.
        CHECK(*acknowledge(s, "one", in->upTo));
        (void)s.tag("k7-0", "again", "me");
        in = reader.inbox("one");
        CHECK_EQ(in->events.size(), std::size_t{1});
        CHECK_EQ(reader.lastCandidates(), std::size_t{1});
    }
    return finish();
}
