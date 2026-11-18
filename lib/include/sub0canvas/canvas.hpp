#pragma once
/// @file canvas.hpp
/// The canvas core (docs/CANVAS.md): files as revisioned documents edited by TRANSACTIONS. A transaction names the revision it was made against and
/// holds line operations; it applies entirely or not at all. A stale one is rebased through the edits made since (line numbers shift) or rejected with the
/// conflicting edit named. Every accepted transaction is logged, so any revision can be restored and "who changed what" is answerable. Pure data, no I/O.

#include "sub0canvas/outline.hpp"

#include <expected>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sub0canvas {

/// One operation of a transaction. Line numbers are 1-based and refer to the revision the transaction was made against.
struct CanvasOp
{
    enum class Kind { replaceLines, insertAfter, replaceSymbol };
    Kind kind = Kind::replaceLines;
    int from = 0, to = 0;      ///< replaceLines: inclusive range; insertAfter: `from` is the line to insert after (0 = at the top)
    std::string symbol;        ///< replaceSymbol: a function-like region by name; its whole text (signature to closing line) is replaced
    std::string text;          ///< the new lines (empty deletes for replaceLines)
};

struct Transaction
{
    std::string path;
    int baseRevision = 0;      ///< the revision the worker was shown
    std::string by;            ///< who made it (a worker or item id)
    std::vector<CanvasOp> ops;
};

struct TxResult
{
    bool ok = false;
    int revision = 0;          ///< the new revision when ok, else the current one
    std::string error;         ///< why it was rejected, naming the line, symbol or conflicting edit
};

/// One accepted transaction.
struct CanvasLogEntry
{
    std::string path, by;
    int revision = 0;          ///< the revision this transaction produced
    int baseRevision = 0;
    int from = 0, to = 0;      ///< the span it changed, in the numbering of the revision before it (an insertion has to == from - 1)
    int delta = 0;             ///< lines added (negative: removed)
};

class Canvas
{
public:
    /// Add or replace a file at revision 1 (a replaced file starts a new history).
    void addFile(const std::string& path, std::string text);
    [[nodiscard]] bool has(const std::string& path) const;
    [[nodiscard]] const std::string& text(const std::string& path) const;
    [[nodiscard]] int revision(const std::string& path) const;
    /// The text of an earlier revision (empty if unknown).
    [[nodiscard]] std::string textAt(const std::string& path, int revision) const;
    /// Apply a transaction: all operations or none. An edit made against an older revision is rebased when it does not overlap what changed since.
    [[nodiscard]] TxResult apply(const Transaction& tx);
    /// Replace the whole text as a new revision: the deliberate whole-file operation (a file not yet on the canvas is added at revision 1).
    [[nodiscard]] TxResult replaceText(const std::string& path, const std::string& text, const std::string& by);
    /// Restore an earlier revision as a new revision (history is never rewritten).
    [[nodiscard]] TxResult revertTo(const std::string& path, int revision, const std::string& by);
    [[nodiscard]] const std::vector<CanvasLogEntry>& log() const { return log_; }
    /// The paths on the canvas, sorted.
    [[nodiscard]] std::vector<std::string> paths() const;
    /// The current text of every file.
    [[nodiscard]] std::map<std::string, std::string> files() const;
    /// The log as JSON (`{"files":{path:revision},"log":[{path,by,revision,base,from,to,delta}]}`): what the portal and a later reader need to say who changed what.
    [[nodiscard]] std::string logJson() const;

private:
    struct File { std::vector<std::string> revisions; };   // revisions[i] is the text at revision i + 1
    std::map<std::string, File> files_;
    std::vector<CanvasLogEntry> log_;
    static const std::string empty_;
};

/// What a worker's reply asks of the canvas (docs/CANVAS.md, slice 0). The reply forms are those workers already use, read as transactions:
///   `=== EDIT: path LINES a-b ===` / `LINE a`   -> replaceLines        `=== EDIT: path AFTER n ===` -> insertAfter
///   `=== EDIT: path SYMBOL name ===`            -> replaceSymbol (the whole function, signature to closing line)
///   `=== EDIT: path ===` with SEARCH/REPLACE    -> kept verbatim in `searchEdits` (the older form; the caller applies it with `applyEdits`)
///   `=== FILE: path ===`                        -> `wholeFiles` (a deliberate whole-file replacement)
///   `=== READ: path ... ===`                    -> `reads`
/// Operations on one path form one transaction (all or nothing). A number column copied from the view (`  12| code`) and a markdown fence are removed.
struct CanvasReply
{
    std::vector<Transaction> transactions;             ///< one per path, in order of first mention; `baseRevision` and `by` are for the caller to fill
    std::map<std::string, std::string> wholeFiles;
    std::string searchEdits;
    std::vector<ReadRequest> reads;
    std::string error;                                 ///< a block that could not be read, named; the reply is then not applied at all
    [[nodiscard]] bool hasEdits() const { return !transactions.empty() || !wholeFiles.empty() || !searchEdits.empty(); }
};
[[nodiscard]] CanvasReply parseCanvasReply(const std::string& reply);

/// How a parsed reply is applied to a canvas (`applyReply`).
struct ApplyOptions
{
    std::set<std::string> allowed;   ///< the files the author may write; anything else is refused
    std::string by;                  ///< who made the reply: every revision it produces names them
    bool partial = false;            ///< false: all or nothing; true: every part that can apply does, and each part that cannot is reported
};

/// What applying a reply did. In partial mode, the parts that could not apply, each named; empty in all-or-nothing mode.
struct ReplyApplied
{
    std::vector<std::string> skipped;
};

/// Apply a parsed reply to the canvas, as one unit (docs/REQUIREMENTS.md R-EDIT-7, R-CAN-1): whole files first, then each file's operations as one
/// transaction against the revision the author was shown, then the search/replace edits (committed as whole-text revisions, since a snippet names its
/// place by its text). All or nothing: when any part cannot apply, the canvas is left exactly as it was and the error names the part (a path not
/// allowed, a file edited before it exists, a file both rewritten and edited, the canvas's own refusal). Partial: each operation and each snippet applies
/// on its own, against the revision shown (rebased over the parts before it), so one bad part never costs the good ones beside it; the unapplied parts are
/// listed. A reply that failed to parse (`reply.error`) is refused whole in either mode.
[[nodiscard]] std::expected<ReplyApplied, std::string> applyReply(Canvas& canvas, const CanvasReply& reply, const ApplyOptions& options);

/// Parse `reply` and apply it, all or nothing, to a canvas holding `files`: the files afterwards, or why it was refused (a reply with no edit is refused).
[[nodiscard]] std::expected<std::map<std::string, std::string>, std::string> applyReplyText(const std::string& reply, const std::map<std::string, std::string>& files,
                                                                                          const std::set<std::string>& allowed, const std::string& by);

}  // namespace sub0canvas
