#pragma once
// The store core (BACKBONE.md sections 3.1, 3.2, 4; slice B1): items with a source, state, tags, links and open data, held as the fold of an event log.
// Every change is an event; a store is rebuilt by replaying events merged by `(lamport, writer)`, and replay equals the live store byte for byte in its
// snapshot (R-BB-1). Pure: time and persistence come in through `StoreOptions` (R-BB-7). New event kinds are added by registering a reducer in a
// `ReducerRegistry`, in a file of their own (B2: subscriptions, B3: claims and notes); this header and store.cpp are not edited for them.
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/event.hpp"

#include <any>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace sub0canvas {

/// The lifecycle of an item (R-BB-3). `orphaned` is derived, never stored: see `isOrphaned`.
enum class ItemState { open, held, done, verified, dropped };

[[nodiscard]] std::string_view stateName(ItemState s);
[[nodiscard]] std::optional<ItemState> parseState(std::string_view name);

/// A typed relation stated by one item about another. Kinds are open strings with conventions (A5): `duplicates`, `causes`, `fixes`, `implements`,
/// `discusses`, `blocks`, `partOf`, `repliesTo`, `supersedes`, `tests`, `documents`. The core treats none specially.
struct Link
{
    std::string kind;
    std::string key;   ///< the other item's key, possibly in another layer or source
    bool operator==(const Link&) const = default;
};

/// Who made a change and when, read from the event's envelope: authorship is derived from the log, never written by a producer (A6).
struct Stamp
{
    WriterId writer;
    std::uint64_t lamport = 0;
    std::string time;   ///< display only
};

/// The latest lifecycle transition of an item, named with its actor (R-BB-3).
struct Transition
{
    ItemState from = ItemState::open, to = ItemState::open;
    std::string actor, reason;
    Stamp at;
};

/// What a producer hands `putLayer`: the layer item, plus the open fields the store keeps beside it. `LayerItem::number` is ignored (derived: an item's
/// place in its layer); `LayerItem::layer` is overridden by the layer named in the call.
struct ItemInput
{
    LayerItem item;
    std::string data;           ///< a JSON object, kind-specific (coverage, durations, a project field): A4, R-EXP-2. Empty: none
    std::string detailFormat;   ///< `text` (the default when empty) or `markdown` (A4)
};

/// An item as the store holds it: the `LayerItem` (unchanged), and the fields the backbone adds.
struct StoredItem
{
    LayerItem item;                  ///< `item.key` is the identity; `item.number` is not kept (derived at snapshot)
    std::string source;              ///< the adapter that produced it
    std::string data;                ///< raw JSON object or empty
    std::string detailFormat;
    ItemState state = ItemState::open;
    std::set<std::string> tags;
    std::vector<Link> links;         ///< stated by this item, in the order stated
    Stamp created, updated;          ///< the first and the latest `item.put` (A6)
    std::optional<Transition> lastTransition;
    mutable std::uint64_t touchedBy = 0;   ///< scratch for `putLayer`'s vanished scan: never read by reducers, never in a snapshot
};

/// An item that links to a key, from the store's reverse index ("what implements this plan item?").
struct Backlink
{
    std::string fromKey, kind;
};

struct StringHash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

/// What reducers fold events into. The core's reducers keep items, layers and the reverse link index; a later slice's reducers keep their own state in
/// `ext<T>()` (one default-constructed `T` per type, created on first use and rebuilt with the rest on every replay).
class StoreState
{
public:
    StoreState() = default;
    StoreState(const StoreState&) = delete;   // layers hold pointers into `items`
    StoreState& operator=(const StoreState&) = delete;
    StoreState(StoreState&&) = default;       // a move keeps the nodes, so the pointers stay valid
    StoreState& operator=(StoreState&&) = default;

    [[nodiscard]] StoredItem* find(std::string_view key);
    [[nodiscard]] const StoredItem* find(std::string_view key) const;
    /// Makes a new item in `layer` (last in the layer's order). The key must not exist.
    StoredItem& create(std::string key, std::string layer);
    /// Moves an item to another layer (last in its order); a no-op for the layer it is in.
    void moveToLayer(StoredItem& item, const std::string& layer);
    /// Adds a link (and its reverse entry); false when this exact link is already stated.
    bool addLink(StoredItem& item, Link link);
    /// Removes a link (and its reverse entry); false when it was not stated.
    bool removeLink(StoredItem& item, const Link& link);
    /// State of type `T` for a reducer that is not the core's.
    template <class T>
    [[nodiscard]] T& ext()
    {
        std::any& slot = ext_[std::type_index(typeid(T))];
        if (!slot.has_value()) slot.emplace<T>();
        return *std::any_cast<T>(&slot);
    }

    /// The same state read without creating it: null when no reducer has used `T` yet.
    template <class T>
    [[nodiscard]] const T* extIf() const
    {
        const auto it = ext_.find(std::type_index(typeid(T)));
        return it == ext_.end() ? nullptr : std::any_cast<T>(&it->second);
    }

    std::unordered_map<std::string, StoredItem, StringHash, std::equal_to<>> items;
    std::map<std::string, std::vector<StoredItem*>, std::less<>> layers;                       ///< layer name to its items in first-put order
    std::unordered_map<std::string, std::vector<Backlink>, StringHash, std::equal_to<>> backlinks;   ///< target key to who links to it
    std::size_t skipped = 0;                          ///< events of a known kind whose payload a reducer could not use (kept in the log, not applied)
    std::map<std::string, std::size_t, std::less<>> unknownKinds;   ///< events with no reducer, by kind: kept in the log, skipped by the fold (A7)

private:
    std::map<std::type_index, std::any> ext_;
};

/// Folds one event into the state. Must be a pure function of the state and the event (it runs again on every replay, in merge order).
using Reducer = std::function<void(StoreState&, const Event&)>;

/// Event kinds to reducers, keyed by the whole `name@version` string. Registration is explicit and local to a store (no static self-registration, whose
/// order is undefined), so B2 and B3 add their kinds in files of their own: `registerSubscriptionReducers(registry)` and so on, called where the store is made.
class ReducerRegistry
{
public:
    /// The core's kinds: `item.put@1`, `item.state@1`, `item.tag@1`, `item.untag@1`, `item.link@1`, `item.unlink@1`.
    [[nodiscard]] static ReducerRegistry core();
    /// Registers `reducer` for `kind`; false (and nothing changes) when the kind already has one.
    bool add(std::string kind, Reducer reducer);
    [[nodiscard]] const Reducer* find(std::string_view kind) const;
    [[nodiscard]] std::size_t size() const { return reducers_.size(); }

private:
    std::unordered_map<std::string, Reducer, StringHash, std::equal_to<>> reducers_;
};

/// Which items an export or a query selects. Every set is optional and an empty set matches all; within a set any member matches, across sets all must (A11).
struct Query
{
    using NameSet = std::set<std::string, std::less<>>;
    NameSet layers, kinds, tags, states, sources;
    std::string text;   ///< a substring of the title or the detail
    /// Tag prefixes that hide an item unless the query names that tag exactly (data, not a constant: A11's `private/<actor>` convention). A snapshot that does
    /// not ask for a private item never carries it (R-BB-10).
    std::vector<std::string> hiddenTagPrefixes{"private/"};
};

/// What a `putLayer` did: the events it cost are `added + changed + reopened + vanished`; an unchanged item costs none.
struct PutResult
{
    std::size_t added = 0;      ///< new keys, open
    std::size_t changed = 0;    ///< known keys whose content differed
    std::size_t reopened = 0;   ///< done or verified items that came back, set open
    std::size_t vanished = 0;   ///< items of this layer and source not offered, set done
    std::size_t unchanged = 0;
    std::size_t rejected = 0;   ///< items offered with an empty key or a `data` that is not a JSON object: not applied
};

/// The store's construction: who writes, where new events go, and the display clock.
struct StoreOptions
{
    WriterId writer{"local", "local"};                  ///< stamped on events the store makes
    ReducerRegistry registry = ReducerRegistry::core();
    Log* sink = nullptr;                                ///< non-owning; receives the events the store makes (not those it merges); nullptr: kept in memory only
    std::function<std::string()> clock;                 ///< display time for events the store makes; empty: "". The core reads no clock itself (R-BB-7)
};

/// The backbone's store. Events are held sorted by `(lamport, writer)`; the state is their fold. An event that sorts after every held event (the usual
/// case: the store's own writes) is folded in place in O(1); one that sorts earlier (another writer's older event arriving late) rebuilds the fold, O(n).
class Store
{
public:
    explicit Store(StoreOptions options = {});

    /// Merges the events of `source` from position `from` (replay, or catching up). Events already held are ignored (by writer and lamport), so overlapping
    /// reads are harmless. Not forwarded to the sink.
    void replay(const EventSource& source, std::uint64_t from = 0);
    /// Merges foreign or replayed events: sorted, de-duplicated, folded once. Not forwarded to the sink.
    void merge(std::vector<Event> events);
    /// Appends one event as given (its writer and lamport are the caller's) and forwards it to the sink. Returns false when an event with that writer and
    /// lamport is already held.
    bool append(Event e);
    /// Makes an event of this store's writer with the next lamport value, appends it and returns it.
    const Event& emit(std::string kind, std::string subject, std::string payload);

    // Conveniences that build events (every one is an `emit`).
    /// Reconciles a re-derived layer by key (R-BB-2): a new key is open; a known key whose content differs is updated; a done or verified key that comes back
    /// is reopened (a dropped or held one is left as it is); an open or held key of this layer and source that is not offered is set done by the source.
    /// Nothing is deleted. Keys must be unique within `items`. Cost: events for the items that changed, plus one pass over the layer.
    PutResult putLayer(const std::string& layer, const std::string& source, const std::vector<ItemInput>& items);
    PutResult putLayer(const std::string& layer, const std::string& source, const std::vector<LayerItem>& items);
    /// Sets an item's state; `actor` names who (required). True when an event was made, false when the item already had that state.
    [[nodiscard]] std::expected<bool, std::string> setState(const std::string& key, ItemState state, const std::string& actor, const std::string& reason = {});
    /// Adds a tag (lower-case `[a-z0-9-/]`, 3.1). True when an event was made, false when the item already had it.
    [[nodiscard]] std::expected<bool, std::string> tag(const std::string& key, const std::string& tag, const std::string& actor);
    [[nodiscard]] std::expected<bool, std::string> untag(const std::string& key, const std::string& tag, const std::string& actor);
    /// States that `key` relates to `target` by `kind` (an open string, A5). True when an event was made, false when already stated.
    [[nodiscard]] std::expected<bool, std::string> link(const std::string& key, const std::string& kind, const std::string& target, const std::string& actor);
    [[nodiscard]] std::expected<bool, std::string> unlink(const std::string& key, const std::string& kind, const std::string& target, const std::string& actor);

    // Reads.
    [[nodiscard]] const StoredItem* item(std::string_view key) const { return state_.find(key); }
    /// The items a query selects, by layer name then first-put order.
    [[nodiscard]] std::vector<const StoredItem*> items(const Query& q = {}) const;
    /// Who links to `key`, from the reverse index.
    [[nodiscard]] std::vector<Backlink> linksTo(std::string_view key) const;
    /// The 1-based place of an item in its layer (what `READ ISSUE n` names); 0 when unknown.
    [[nodiscard]] int numberOf(std::string_view key) const;
    /// The held events in merge order, verbatim (unknown kinds and fields included, A7).
    [[nodiscard]] const std::vector<Event>& events() const { return events_; }
    /// The lamport value of the last event in merge order (0 when empty).
    [[nodiscard]] std::uint64_t head() const { return events_.empty() ? 0 : events_.back().lamport; }
    [[nodiscard]] const StoreState& state() const { return state_; }

    /// The store as JSON (`store-snapshot-1`), scoped by `q` (A11): layers by name, items in layer order with their state, tags, links, data, anchors,
    /// authorship and last transition. Deterministic: the same events give the same bytes.
    [[nodiscard]] std::string snapshotJson(const Query& q = {}) const;

private:
    void fold(const Event& e);
    void rebuild();
    bool matches(const StoredItem& it, const Query& q) const;
    [[nodiscard]] std::string now() const { return options_.clock ? options_.clock() : std::string{}; }

    StoreOptions options_;
    std::vector<Event> events_;   ///< sorted by eventOrder
    StoreState state_;
    std::uint64_t epoch_ = 0;     ///< putLayer calls so far: the mark for `touchedBy`
};

/// True when an item has anchors and none of them resolves: derived, never stored (R-BB-3). An item with no anchors is waiting, not orphaned (3.2).
/// `resolves` is the index's answer for one anchor; the store holds no index.
[[nodiscard]] bool isOrphaned(const StoredItem& item, const std::function<bool(const Anchor&)>& resolves);

}  // namespace sub0canvas
