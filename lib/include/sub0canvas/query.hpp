#pragma once
/// @file query.hpp
/// The pure query layer of the live-views server (slice V1a-q; docs/QUERY.md, docs/AUTHORING.md section 3.1, requirements R-AUTH-1, R-AUTH-13, R-AUTH-14,
/// R-AUTH-16, R-AUTH-17): a JSON request in, a JSON response out, over a document held as files at revisions, a `BlockIndex` and a `Store`. Schema `query-1`.
///
///   * every answer is a BOUNDED page (page sizes are data, `QueryPolicy`) with a cursor for the rest, and carries the position it was computed at (`at`);
///   * a request names its scope (a folder, a file, a layer and its query) and the work done is proportional to the answer, not to the document: asking for
///     one folder never visits another (`QueryStats::visited` is the count, a metric);
///   * the data is written through the SAME projection functions `layeredJson` writes the bundle with (lib/src/projection.hpp, review D11), so the bundle is
///     "every query answered at once";
///   * no transport and no I/O: the stdio, HTTP and SSE transports (review D7) call `QueryServer::handle` with the request text.
///
/// Pure: no I/O, no threads, no clock. A `QueryServer` holds a reference to its document, and the document borrows the index and the store: the owner of the
/// process serialises writes and reads (one process owns the document, AUTHORING.md section 3), `handle` itself only reads.

#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/store.hpp"

#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sub0canvas {

/// The schema every response names, and a request may name (a request naming another is refused).
inline constexpr std::string_view kQuerySchema = "query-1";

/// What a request that carries a member this version does not know gets (docs/QUERY.md section 6): `ignore` answers as if it were absent; `echo` (the default)
/// answers and lists the names in `unknownFields`, so a newer client can see what an older server dropped; `reject` is an error naming the first.
enum class UnknownFields { ignore, echo, reject };

/// The bounds of every answer, as data a client or a configuration may supply (`parseQueryPolicy`): behaviour that is not hard-coded (AGENTS.md, the three axes).
struct QueryPolicy
{
    /// One operation's page size: the default when a request names no `limit`, and the largest `limit` honoured (a larger one is clamped, and the response says so).
    struct Page
    {
        std::size_t defaultItems = 100, maxItems = 1000;
    };
    Page tree{500, 2000};       ///< entries of a folder: a 1,588-file folder is one page at its limit, three at the default
    Page blocks{200, 2000};
    Page layers{100, 1000};
    Page layer{50, 500};
    Page events{200, 2000};
    Page revisions{50, 500};
    /// A page stops adding entries once the response has reached this many bytes (the first entry is always added, so the response may exceed it by one entry).
    std::size_t maxResponseBytes = std::size_t{1} << 20;
    /// The most text bytes one `file` response carries (a longer text is read in chunks by `offset`).
    std::size_t maxTextBytes = std::size_t{256} << 10;
    std::size_t maxRequestBytes = std::size_t{64} << 10;   ///< a longer request is refused unparsed
    std::size_t maxHave = 1000;                            ///< content ids a `file` request may list as already held (R-AUTH-14)
    /// The most layer items one `layer` request examines (a filter that matches nothing must not walk a million items to say so): the page ends there with a
    /// cursor, possibly with fewer entries than `limit`, and `scanned` says how far it got.
    std::size_t maxScan = 100'000;
    UnknownFields unknownFields = UnknownFields::echo;
    /// Tag prefixes that hide a layer item unless the request names that tag exactly (A11, R-BB-10): the store's rule, as data here.
    std::vector<std::string> hiddenTagPrefixes{"private/"};
};

/// A policy from JSON: `{"tree":{"default":500,"max":2000}, "blocks":..., "layers":..., "layer":..., "events":..., "revisions":..., "maxResponseBytes":n,
/// "maxTextBytes":n, "maxRequestBytes":n, "maxHave":n, "unknownFields":"ignore|echo|reject", "hiddenTagPrefixes":["private/"]}`. Every member is optional and
/// starts from the defaults; an unknown member, a zero page size or a default above its maximum is an error naming it.
[[nodiscard]] std::expected<QueryPolicy, std::string> parseQueryPolicy(std::string_view json);
/// The policy as JSON, the form `parseQueryPolicy` reads (and the `info` operation reports).
[[nodiscard]] std::string queryPolicyJson(const QueryPolicy& policy);

/// One revision of one file as the document holds it.
struct FileRevision
{
    int number = 0;               ///< the canvas's revision number (A1: `revisionId` is the opaque form)
    std::string id;               ///< the opaque revision id ("r3", a commit id)
    std::string textId;           ///< the text's content id (`contentId`, the git blob id)
    std::size_t bytes = 0, lines = 0;
};

/// A folder of the document's tree: its subfolders and files by name (sorted), and how many files lie below it, kept as files are added so that a listing
/// never counts what it does not list.
struct Folder
{
    std::map<std::string, std::unique_ptr<Folder>, std::less<>> folders;
    std::map<std::string, std::vector<FileRevision>, std::less<>> files;   ///< by name; the revisions in order, the last the current
    std::size_t filesBelow = 0;                                            ///< files in this folder and every folder under it
};

/// What the queries answer from: the files at their revisions (texts held once by content id), the folder tree over their paths, and the block index and the
/// store, which it borrows (null: that part answers "not available").
class QueryDocument
{
public:
    QueryDocument() = default;
    /// Borrows `index` and `store`; both must outlive the document. Either may be null.
    QueryDocument(const BlockIndex* index, const Store* store) : index_(index), store_(store) {}

    /// Records `text` as revision `revision` of `path` (`revisionId`: its opaque id; empty gives "r<number>"). False, and nothing recorded, for a path that is
    /// empty or has an empty segment (`a//b`, a leading or trailing `/`), or a revision that is not above the file's last. Identical texts are held once.
    bool addRevision(const std::string& path, int revision, std::string revisionId, const std::string& text);
    /// Names a layer for the `layers` operation (label, description, producer): the bundle's `layerMeta`.
    void setLayerMeta(const std::string& layer, LayerMeta meta) { meta_[layer] = std::move(meta); }

    [[nodiscard]] const BlockIndex* index() const { return index_; }
    [[nodiscard]] const Store* store() const { return store_; }
    /// The folder at `path` ("" is the root), or null. `steps` is increased by the folders passed through: a lookup costs the path's depth.
    [[nodiscard]] const Folder* folder(std::string_view path, std::size_t& steps) const;
    /// The revisions of the file at `path`, or null when there is no such file.
    [[nodiscard]] const std::vector<FileRevision>* revisions(std::string_view path, std::size_t& steps) const;
    /// The text with that content id, or null.
    [[nodiscard]] const std::string* text(std::string_view textId) const;
    [[nodiscard]] const std::map<std::string, LayerMeta>& layerMeta() const { return meta_; }
    /// Files recorded, and revisions recorded: the document's own position; a response's `at.generation` is the number of `addRevision` calls that took effect.
    [[nodiscard]] std::size_t fileCount() const { return root_.filesBelow; }
    [[nodiscard]] std::size_t generation() const { return generation_; }

private:
    Folder root_;
    std::unordered_map<std::string, std::string> texts_;
    std::map<std::string, LayerMeta> meta_;
    const BlockIndex* index_ = nullptr;
    const Store* store_ = nullptr;
    std::size_t generation_ = 0;
};

/// What one request cost, for the metrics and the tests of R-AUTH-1: `visited` counts the nodes the handler touched to answer (folder entries and path steps for
/// `tree`, the blocks of the file for `blocks`, the layer items examined for `layer`, the events returned for `events`, ...); `entries` the entries returned.
struct QueryStats
{
    std::size_t visited = 0, entries = 0, responseBytes = 0;
};

/// Answers requests over a document. JSON text in, JSON text out; every failure is a structured error response, never an exception or an empty string.
/// Operations (docs/QUERY.md): `info`, `tree`, `file`, `revisions`, `blocks`, `layers`, `layer`, `events`.
class QueryServer
{
public:
    explicit QueryServer(const QueryDocument& document, QueryPolicy policy = {}) : document_(document), policy_(std::move(policy)) {}

    /// The response to `request` (one JSON object). `stats`, when given, receives what the answer cost.
    [[nodiscard]] std::string handle(std::string_view request, QueryStats* stats = nullptr) const;
    [[nodiscard]] const QueryPolicy& policy() const { return policy_; }

private:
    const QueryDocument& document_;
    QueryPolicy policy_;
};

}  // namespace sub0canvas
