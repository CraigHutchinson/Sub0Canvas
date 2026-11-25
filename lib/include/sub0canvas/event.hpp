#pragma once
// The event envelope and the log seam of the backbone (docs/BACKBONE.md section 3.3 and 4; docs/DECISIONS.md sections 3 and 5; amendments A7,
// A15, A18). This header depends on nothing else in the library so that it can be lifted into Sub0Fabric as the second user of its record module (A18): an event
// is `{kind@ver, writer, lamport, subject, time, payload}` and a log is an append-only sequence of them. Pure: no I/O, no clock, no threads (R-BB-7).
#include <compare>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

/// Who wrote an event: a composite of a namespace (project, source home) and a writer id local to it, because two homes both mint `w-000001` (review D6).
/// Ordered by namespace, then writer: the tie-break of the merge rule.
struct WriterId
{
    std::string ns;       ///< project or source home
    std::string writer;   ///< the writer's id within the namespace
    auto operator<=>(const WriterId&) const = default;
};

/// One change to the backbone. The order of events is `(lamport, writer)`, never arrival order, so several writers' segments merge to one deterministic
/// sequence (A15, review D4). A log's own position (its cursor) is NOT part of the envelope: it belongs to the segment that holds the event, so this envelope
/// has no `seq` or `type` field and cannot collide with Sub0Squad's `journal-1` (decision recorded in docs/BACKBONE.md section 14.2, A22).
struct Event
{
    std::string kind;          ///< `name@version`, for example `item.put@1`; a reducer is registered for the whole string, so a new version is a new kind
    WriterId writer;
    std::uint64_t lamport = 0; ///< strictly increasing per writer; a store stamps max(seen) + 1
    std::string subject;       ///< an item key, a block id, a path, a claim id
    std::string time;          ///< ISO 8601, for display only: it never orders anything
    std::string payload = "{}";///< the kind's data as raw JSON, kept verbatim (A7): a reader that does not know the kind never rewrites it
    std::string extra;         ///< envelope members this version does not know, as raw JSON members (`"k":v,...`), kept verbatim and written back (A7)
    bool operator==(const Event&) const = default;
};

/// The merge order of two events: lamport clock, then writer. Equal only for the same event (a writer never reuses a lamport value).
[[nodiscard]] std::strong_ordering eventOrder(const Event& a, const Event& b);

/// A kind split at its `@`: `item.put@1` is name `item.put`, version `1`. A kind without `@` has an empty version.
struct KindRef
{
    std::string_view name, version;
};
[[nodiscard]] KindRef splitKind(std::string_view kind);

/// One event as a compact JSON object: `kind, writer{namespace,writer}, lamport, subject, time, payload`, then any `extra` members. Payload and extra are
/// written as they are held, so `parseEvent(eventJson(e))` equals `e` and a line this function wrote parses and writes back byte for byte.
[[nodiscard]] std::string eventJson(const Event& e);

/// Read one event from a JSON object. The fields `kind`, `writer` (an object with `namespace` and `writer`) and `lamport` are required; `subject`, `time` and
/// `payload` default to empty (payload `{}`). A member this version does not know is kept in `extra`; the payload is kept as its own text. The error names the
/// first problem.
[[nodiscard]] std::expected<Event, std::string> parseEvent(std::string_view json);

/// Where events are read from. A source's position is a dense count of the events it holds, so a reader resumes with `since(position)` and never rescans.
/// The core defines the seam only: a file, a git ref or a Fabric segment supplies the implementation (review D2).
class EventSource
{
public:
    virtual ~EventSource() = default;
    /// The events at positions `from` and later, in the source's own order (not necessarily merge order). `from` past the end gives none.
    [[nodiscard]] virtual std::vector<Event> since(std::uint64_t from) const = 0;
    /// The number of events held: the position the next appended event gets.
    [[nodiscard]] virtual std::uint64_t size() const = 0;
};

/// An append-only event source. One writer owns a log (A15: single writer per segment); durability is the implementation's policy.
class Log : public EventSource
{
public:
    /// Appends `e` at position `size()`. Order of arrival is the log's order; the merge rule applies when a store reads it.
    virtual void append(const Event& e) = 0;
};

/// A log held in memory: tests, a squad's per-attempt use, and the reference implementation of the seam.
class MemoryLog final : public Log
{
public:
    void append(const Event& e) override;
    [[nodiscard]] std::vector<Event> since(std::uint64_t from) const override;
    [[nodiscard]] std::uint64_t size() const override { return events_.size(); }
    /// Everything appended, in order.
    [[nodiscard]] const std::vector<Event>& events() const { return events_; }

private:
    std::vector<Event> events_;
};

}  // namespace sub0canvas
