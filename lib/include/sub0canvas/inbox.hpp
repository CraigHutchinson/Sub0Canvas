#pragma once
// Subscriptions delivered as inboxes (BACKBONE.md section 3.4; slice B2; R-BB-4, R-BB-5). A subscription is a standing query with a cursor; its inbox is what
// changed since the cursor, and the cursor moves only when the reader acknowledges. Everything the store keeps about subscriptions is an event: `sub.subscribe@1`,
// `sub.unsubscribe@1` and `sub.ack@1`, folded by reducers that `registerSubscriptionReducers` adds to a `ReducerRegistry` (store.hpp is not edited for them), so a
// replay gives the same subscriptions and cursors as the live store. Delivery is pull: nothing here calls a subscriber, owns a thread or reads a clock (R-BB-7).
//
// Positions. An event's position is its lamport value: the store holds events in `(lamport, writer)` order, a store stamps its own writes max(seen) + 1, and the
// lamport survives every replay, which an index into the merged vector would not. A cursor is therefore "everything up to this lamport value has been seen".
// For one writer that is exact. When several writers' segments merge, an event another writer made earlier but delivered late can sort below a cursor that has
// already moved past it, and the inbox will not show it; closing that gap (per-writer cursors) belongs to the slice that merges writers (V1, A15).
#include "sub0canvas/store.hpp"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

/// What a subscription selects. The base `Query` filters the item an event is about (layers, kinds, tags, states, sources, text, hidden tag prefixes: the same
/// meaning as `Store::items`, A11); the three added sets filter the event itself. An empty set matches all; within a set any member matches, across sets all must.
struct SubscriptionQuery : Query
{
    /// Block ids: "anything anchored here" (the anchors the item's latest `item.put` carried). Merges of block ids are not followed here: the caller passes the
    /// ids it wants, the revision index knows the merges.
    NameSet blocks;
    /// Path prefixes: `src/canvas` selects an owner's area (a prefix of an anchor's path, as a string, so `src/canvas` also takes `src/canvas_ext.cpp`).
    NameSet paths;
    /// Event kinds, as the whole `name@version` or the bare name (`item.state` takes every version). Needed alone, it also selects events of kinds no reducer
    /// knows, which no item filter could reach (A7).
    NameSet events;
};

/// A standing query with a cursor, as the store holds it. `id` names the subscription (a worker's id, a view's id, a person's); `owner` is who reads it.
struct Subscription
{
    std::string id, owner;
    SubscriptionQuery query;
    std::uint64_t cursor = 0;   ///< the lamport value acknowledged: events after it are unseen
};

/// What matched since a subscription's cursor.
struct Inbox
{
    std::vector<LayerItem> items;   ///< the items the matching events are about that still match now, as they are now, ordered by layer then first-put order; `number` is their place in the layer
    std::vector<Event> events;      ///< the matching events, in merge order, verbatim
    std::uint64_t upTo = 0;         ///< what acknowledging this inbox moves the cursor to: the store's head when it was read
};

/// The subscriptions as the reducers fold them; `StoreState::ext<SubscriptionState>()`.
struct SubscriptionState
{
    std::map<std::string, Subscription, std::less<>> subscriptions;
};

inline constexpr std::string_view kSubscribeKind = "sub.subscribe@1";
inline constexpr std::string_view kUnsubscribeKind = "sub.unsubscribe@1";
inline constexpr std::string_view kAckKind = "sub.ack@1";

/// Adds the reducers of the three subscription kinds to `registry`. Call it where the store is made, once per registry. False when one of the kinds already had
/// a reducer (nothing registered for it then).
bool registerSubscriptionReducers(ReducerRegistry& registry);

/// Makes `id` a subscription of `owner` for `query` (an event). Subscribing again under the same id replaces owner and query and keeps the cursor. A new
/// subscription starts at `cursor`: 0 sees everything the log holds, `store.head()` only what comes next. Errors: an empty id or owner; a store whose registry
/// has no subscription reducers (the event would be kept but never applied).
[[nodiscard]] std::expected<void, std::string> subscribe(Store& store, const std::string& id, const std::string& owner, const SubscriptionQuery& query,
                                                         std::uint64_t cursor = 0);
/// Ends a subscription (an event); true when it existed, false (and no event) when not.
[[nodiscard]] std::expected<bool, std::string> unsubscribe(Store& store, const std::string& id);
/// Moves the cursor of `id` to `position` (an event). True when it moved; false, with no event, when the cursor is already at or past `position`, so
/// acknowledging twice, or an old position again, changes nothing. Errors: an unknown subscription; a position beyond `store.head()` (it would hide events
/// not yet made).
[[nodiscard]] std::expected<bool, std::string> acknowledge(Store& store, const std::string& id, std::uint64_t position);

/// The subscription `id`, or null.
[[nodiscard]] const Subscription* findSubscription(const Store& store, std::string_view id);
/// Every subscription of `owner`, by id.
[[nodiscard]] std::vector<const Subscription*> subscriptionsOf(const Store& store, std::string_view owner);

/// True when an item passes the item-level filters of `q`: what `Store::items(q)` applies (layers included, against the item's layer now). Exposed so a consumer
/// that holds an item can ask the same question the store answers, and so a test can hold the two together.
[[nodiscard]] bool itemMatches(const StoredItem& item, const Query& q);

/// Reads inboxes from a store in time proportional to the events returned, not to the log. It keeps an index of the store's events by layer, path, block and
/// event kind, built from `store.events()` alone (it folds the item layers and anchors itself, so it does not depend on when it is asked), extended as the log
/// grows, and rebuilt when an older event arrives out of order. The store must outlive the reader; a reader is cheap to make, but its first read pays one pass
/// over the log.
///
/// What an event is bucketed under is what its item was when the event was made: the layer and anchors of the latest earlier `item.put` of the subject (an
/// `item.put` itself, the one it carries). `layers`, `blocks` and `paths` filter on that; the other item filters use the item as it is now, as `Store::items`
/// does. An item that moved layers keeps its older events under the older layer; the move itself shows under the new one.
class InboxReader
{
public:
    explicit InboxReader(const Store& store);
    ~InboxReader();
    InboxReader(const InboxReader&) = delete;
    InboxReader& operator=(const InboxReader&) = delete;

    /// What changed under subscription `id` since its cursor. The events of the subscription kinds are never in an inbox unless `query.events` names them, so
    /// acknowledging does not make the next inbox non-empty. Cost: the events of the narrowest bucket since the cursor (`blocks`, else `paths`, else `events`,
    /// else `layers`; a query with none of these looks at every event since the cursor), each tested against the rest of the query: exactly the events
    /// returned when the bucket is the whole query. Error: an unknown subscription.
    [[nodiscard]] std::expected<Inbox, std::string> inbox(std::string_view id);

    /// Events examined by the last `inbox` (the candidates, before the final test): the measure of its cost.
    [[nodiscard]] std::size_t lastCandidates() const;
    /// How many of the store's events the index holds.
    [[nodiscard]] std::size_t indexedEvents() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// One inbox through a reader made for the call: O(events in the log). For a consumer that asks once; one that asks repeatedly keeps an `InboxReader`.
[[nodiscard]] std::expected<Inbox, std::string> inbox(const Store& store, std::string_view id);

}  // namespace sub0canvas
