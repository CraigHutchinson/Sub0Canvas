#pragma once
/// @file doc_layers.hpp
/// The layered document (docs/LAYERED_DOCUMENT.md): one document seen at several abstractions, every layer bound to the same STABLE block identities rather
/// than to line numbers. A block is a semantic part of a file (a function or method, as `foldRegions` finds them); its id is assigned when the block first
/// appears and carried through edits by reconciliation (signature, then name, then content similarity), so a layer item made against one revision still
/// names the right code after a SYMBOL replace, a whole-file rewrite or a rename. Lines are resolved only when a layer is rendered. Text outside every
/// block falls back to a path and line, valid for the revision it was found in.
///
/// Slice 1 builds the block index and the ISSUES layer (compiler errors and failing tests, parsed from a gate's verdict); slice 2 adds the first issues
/// inline (`layerInline`). The layer item, its summary and
/// its detail are layer-neutral, so the review and prose layers reuse them. Pure data, no I/O.

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace sub0canvas {

/// One semantic block of a file, with its stable identity and where it stands now.
struct Block
{
    std::string id;                    ///< "b7": stable for the life of the index; never reused
    std::string path;
    std::string name;                  ///< as `foldRegions` names it (may be qualified: `Parser::term`)
    std::string signature;             ///< the signature line, whitespace collapsed
    int first = 0, last = 0;           ///< the lines it spans now (1-based, inclusive, signature to closing line)
    std::set<std::string> tokens;      ///< identifiers and numbers of its body: what content similarity compares
    std::set<std::string> literals;    ///< the contents of its string and character literals ("%", "asin"): what issue localisation matches
};

/// How a block's identity was carried through the last update of its file (for the log and the tests).
/// kept: same signature (logged only when its text changed); resigned: same name, new signature; renamed: similar body, new name; revived: a retired id
/// back; added: new; retired: gone, its items orphaned; merged: gone, its body now in another block that its items follow.
enum class Carry { kept, resigned, renamed, revived, added, retired, merged };

/// One line of the index's history: what happened to which id.
struct BlockEvent
{
    Carry carry = Carry::kept;
    std::string id, path, name, detail;   ///< `detail`: the old name of a renamed block, the old signature of a re-signed one, the survivor of a
                                          ///< merged one, the file a revived block was retired from, "edited" for a kept block whose text changed
    int turn = 0;                         ///< the turn it happened in (`setTurn`)
};

/// Every block of a document, with identities that survive edits. Feed it each file's new text after any change (`update` is cheap when the text is the
/// same); ask it where an id is now, or which block holds a line.
class BlockIndex
{
public:
    /// Reconcile the blocks of `path` with its new text. In order, a new block keeps the id of an unmatched old block of the same file that has
    ///   1. the same signature (a SYMBOL replace, a body edit, a move within the file),
    ///   2. the same name, when that name is unique on both sides (a signature change),
    ///   3. a body similar enough (token Jaccard >= 0.5; a rename),
    /// or else revives a RETIRED id of any file with the same signature or a near-identical body (a block deleted and written again, a move across files);
    /// or else gets a new id. An old block left unmatched is retired: when most of its body now lives in one block (token containment >= 0.6) it is
    /// recorded as MERGED into that block, whose id its layer items follow; otherwise its items are orphans.
    void update(const std::string& path, const std::string& text);
    /// The turn later events are stamped with.
    void setTurn(int turn) { turn_ = turn; }

    /// The live block of that id, or of the block it was merged into; nullptr when the id is retired without a successor (its items are orphans).
    [[nodiscard]] const Block* resolve(const std::string& id) const;
    /// The innermost live block of `path` that holds `line`, or nullptr (text outside every block).
    [[nodiscard]] const Block* at(const std::string& path, int line) const;
    /// The live blocks of `path`, in file order.
    [[nodiscard]] std::vector<const Block*> blocks(const std::string& path) const;
    /// Every live block of every file.
    [[nodiscard]] std::vector<const Block*> all() const;
    /// What happened to ids, in order.
    [[nodiscard]] const std::vector<BlockEvent>& events() const { return events_; }
    /// The index as JSON (`{"blocks":[{id,path,name,first,last}],"retired":[{id,name,mergedInto}],"events":[...]}`), kept beside the canvas for a later reader.
    /// `events` is the whole log.
    [[nodiscard]] std::string json() const;
    /// The same, but `events` holds only the log from position `eventsFrom` on, and `eventsFrom` says where it starts: a snapshot carries what happened since the
    /// previous one (docs/BACKBONE.md 3.3; the owner confirmed per-turn events, 2026-11-23). A position past the end gives an empty list.
    [[nodiscard]] std::string json(std::size_t eventsFrom) const;
    /// The whole log as the cumulative record, schema `canvas-events-1` (docs/BACKBONE.md section 7): a header line naming `document`, then one line per event,
    /// `{"seq":n,"kind":"block","source":"canvas/index","subject":id,"data":{turn,carry,path,name,detail}}`, `seq` being the position in `events()` (the cursor
    /// a snapshot's `eventsFrom` names). A provisional subset of the design's Event: the actor and time are not recorded here, and B1's store owns the final writer.
    [[nodiscard]] std::string eventLogNdjson(const std::string& document) const;

private:
    struct Retired { Block block; std::string mergedInto; };
    [[nodiscard]] std::string retiredJson() const;
    [[nodiscard]] std::string jsonFrom(std::size_t eventsFrom, bool say) const;
    std::map<std::string, std::string> texts_;            ///< the text each path was last indexed at
    std::map<std::string, std::size_t> hashes_;           ///< by live id: a hash of its text, to tell an edited block from an untouched one
    std::map<std::string, std::vector<Block>> live_;      ///< by path, in file order
    std::map<std::string, Retired> retired_;              ///< by id
    std::vector<BlockEvent> events_;
    int next_ = 1;
    int turn_ = 0;
};

/// Where a layer item belongs: a block id when it lies in a block (resolved to lines at render time), else a path and line of the revision it was found in.
struct Anchor
{
    enum class Confidence { exact, suspect, none };
    std::string blockId;
    std::string path;
    int line = 0;
    Confidence confidence = Confidence::none;   ///< exact: the producer named a line in the block; suspect: inferred (a failing test's subject)
    int column = 0;                             ///< 1-based column of the place on `line` (a compiler names it); 0: not known. Keeps several sites on one line apart
    int endLine = 0, endColumn = 0;             ///< the end of a range (A3): the line it ends on and the column just PAST its last character (0: a point, not a range)
};

/// One item of one layer: an issue, a review note, a prose description. Layer-neutral: every layer renders through `layerSummary` and `layerDetail`.
/// An item relates to ONE OR MANY places (1-N): the same diagnostic at twenty call sites is one item with twenty anchors, so it is reported, read and
/// fixed once (a rename applies at every anchor).
struct LayerItem
{
    std::string layer;            ///< "issues", "review", "prose"
    int number = 0;               ///< 1-based within the layer as it stands now: what `=== READ: ISSUE n ===` names
    std::string kind;             ///< issues: "error", "warning", "test"
    std::string title;            ///< one line
    std::string detail;           ///< the producer's full text (of the first occurrence when there are several)
    std::vector<Anchor> anchors;  ///< where it belongs, the first the primary; empty: nowhere known
    std::string key;              ///< the same item across turns while its number changes: issues use the test's name, or a hash of the message with numbers removed
};

/// The issues in a verdict: every compiler diagnostic (`path:line:col: error: ...`, Python's `path:line: msg`) anchored exactly to the block holding the
/// line, and every failing test (`FAIL n - name: message`) anchored to its SUSPECT block: the block named like the test, else the block whose string and
/// character literals best match the tokens of the expression under test, rarer tokens weighing more. Diagnostics with the same message (line numbers
/// aside) are ONE issue with an anchor per place. `sources` (the test files, by path) lets a runner that names the test's line (`(eval_test.py:8)`)
/// but not its expression be localised from that line.
[[nodiscard]] std::vector<LayerItem> parseIssues(const std::string& verdict, const BlockIndex& index, const std::map<std::string, std::string>& sources = {});

/// The events of one id and of every id merged into it, in order: the identity history of a section.
[[nodiscard]] std::vector<BlockEvent> blockHistory(const BlockIndex& index, const std::string& id);

/// What a layer says about itself (`layerMeta`): a viewer shows the label and the description, and the producer names who wrote it.
struct LayerMeta
{
    std::string label, description, producer;
};

/// Everything a snapshot is made of, by field name, so that a new field never breaks a caller (docs/MOCK_SQUAD.md section 6, question 3). The defaults write
/// what the first version of the format held, plus the additive fields that need no input.
struct SnapshotSpec
{
    int turn = 0;
    std::map<std::string, std::string> files;                       ///< the text of every file now
    std::map<std::string, int> revisions;                           ///< each path's revision number (the old field `revision`)
    std::map<std::string, std::string> revisionIds;                 ///< each path's revision as an OPAQUE string (A1); a path without one gets "r<number>"
    std::map<std::string, std::vector<LayerItem>> layers;
    std::map<std::string, LayerMeta> layerMeta;                     ///< a layer without an entry gets the library's own for "issues", none for another
    std::map<std::string, std::string> policies;                    ///< the policies that made this snapshot, by role: {"loop":"mock-loop@1"} (A13)
    std::map<std::string, std::string> textIds;                     ///< a path's content id when the caller already has it (saves hashing a text that has not changed)
    std::size_t eventsFrom = 0;                                     ///< the position in `index.events()` this snapshot's events start at: what earlier snapshots carried
    bool textsById = false;                                         ///< false: every file's `text` is written (what old readers need). true: only `textId`, and the texts a
                                                                    ///< reader has not been given are in `texts`
    std::set<std::string>* textsCarried = nullptr;                  ///< textsById: the content ids earlier snapshots of this document carried, updated with this one's
                                                                    ///< (null: this snapshot carries every text it refers to, so it stands alone)
};

/// The interchange form (schema `layered-doc-1`, docs/LAYERED_DOCUMENT.md, BACKBONE.md section 7): the files at their revisions, the index, its history and every
/// layer, one JSON object a viewer reads without the program (sub0canvas). Beyond the first version it writes, all additive: per file `revisionId` and `textId`
/// (and `text`, unless `textsById`); a top-level `texts` (`textsById`), `eventsFrom` with only the events from there on, `policies`, `layerMeta`; per item `id` (its
/// key); per anchor `column`, `endLine` and `endColumn` where known.
[[nodiscard]] std::string layeredJson(const SnapshotSpec& spec, const BlockIndex& index);

/// The first version's call: every event from the start (`eventsFrom` 0), every text inline, revision ids "r<number>". `revisions` gives each path's revision number.
[[nodiscard]] std::string layeredJson(int turn, const std::map<std::string, std::string>& files, const std::map<std::string, int>& revisions, const BlockIndex& index,
                                      const std::map<std::string, std::vector<LayerItem>>& layers);

/// A layer's summary, one line per item, each at most `lineChars`: "3 issues ...:\n 1. test modulo: ... - in term() (lines 40-52)". "" for no items.
[[nodiscard]] std::string layerSummary(const std::vector<LayerItem>& items, const BlockIndex& index, std::size_t lineChars = 170);
/// One item in full: the producer's text, then its anchor resolved now (the block's current numbered lines, or the line it was found at), or that its
/// block is gone (an orphan).
[[nodiscard]] std::string layerDetail(const LayerItem& item, const BlockIndex& index, const std::map<std::string, std::string>& files);
/// The first `count` items in full, as `layerDetail` renders them, for a verdict that shows the first few without a read (slice 2). A function already
/// shown for an earlier item is named, not repeated. Items are added while the text stays within `maxChars` (the first always); the rest are named as
/// readable with `=== READ: ISSUE n ===`. "" for no items or a `count` of 0.
[[nodiscard]] std::string layerInline(const std::vector<LayerItem>& items, const BlockIndex& index, const std::map<std::string, std::string>& files,
                                      std::size_t count = 3, std::size_t maxChars = 3000);
/// `path:line` for every item that resolves to a block, one per line: text a folded view reads as focus, so the anchored blocks stay open.
[[nodiscard]] std::string layerFocus(const std::vector<LayerItem>& items, const BlockIndex& index);

/// The item numbers a reply asks to read: `=== READ: ISSUE 3 ===` (also `ISSUES 3 5`), in order.
[[nodiscard]] std::vector<int> parseIssueReads(const std::string& reply);

}  // namespace sub0canvas
