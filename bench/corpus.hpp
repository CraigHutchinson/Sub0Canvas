#pragma once
/// @file corpus.hpp
/// The deterministic synthetic corpus of the scale harness (docs/sessions/s0-scale-harness.md step 1, docs/SCALE.md sections 2 and 7): a seeded project of
/// C++ and Python-like files whose functions have realistic sizes and token distributions, so `foldRegions` and `BlockIndex` find real blocks, plus, for
/// profile B, the non-code mix (sidecars beside assets, binaries, serialised data, text, Markdown) and a commit-like edit stream with the identity truth of
/// every edit. Pure in-memory generation: no I/O, no clock, no environment, no platform-dependent library distributions (an integer-only generator), so the
/// same seed gives byte-identical output on every machine and compiler.
///
/// Streaming. The corpus is a sequence of ENTRIES; entry `i` is a pure function of (seed, i) (`entry`), so a 10^9-line project is never held: a benchmark walks
/// `CorpusStream` and drops each entry, or samples entries at random indices. `generateCorpus` materialises a corpus only up to `kMaterialiseLimitLines`
/// (10^6 lines is tens of megabytes) and refuses a larger one; ask for those through the stream.
///
/// Distributions (a bucketed approximation of published function-size studies, not a fit: right-skewed, median about ten lines, a heavy tail; the harness
/// needs shape, not fidelity):
///   * function body lines: 1-3 18%, 4-10 40%, 11-25 26%, 26-60 12%, 61-150 4% (mean about 17);
///   * functions per code file: 2-12 half of the files, 13-30 a third, 31-70 the rest (mean about 19; a file about 400 lines, with a tail into the thousands);
///   * identifiers: a vocabulary of 2,048 syllable words drawn with a skew toward common words (the product of two uniform picks), a per-file pool of
///     about 40 and a per-function pool of 5-12 drawn from it, so functions of one file share tokens and unrelated functions stay below the Jaccard 0.5
///     rename threshold of `BlockIndex`.
/// Directories: a path is a pure function of its entry index (mixed-radix directory digits named from the vocabulary), 24 entries to a directory, depth 1-5.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sub0canvas::bench {

/// The files of a corpus, by path. Text files hold their text; binaries hold their bytes (which may contain NULs and newlines).
using FileMap = std::map<std::string, std::string>;

/// What a generated file is. Only `code` files are parsed for blocks; the rest exist to measure classification and the cost of not parsing.
enum class FileKind { code, sidecar, image, serialised, text, markdown };

enum class Language { cpp, python };

/// The relative weights of the entry kinds. An entry is one file, or an asset with its sidecar (`image`, `serialised`: two files, `asset` and `asset.meta`).
/// Weights need not sum to 1.
struct KindMix
{
    double code = 1.0, image = 0.0, serialised = 0.0, text = 0.0, markdown = 0.0;
    double pythonShare = 0.30;     ///< of the code entries, the share written as Python-like text
    double headerShare = 0.30;     ///< of the C++ entries, the share that are headers (`.hpp`, inline functions) rather than sources (`.cpp`)
};

/// Profile A of docs/SCALE.md section 2: lines-dominated, source almost only.
[[nodiscard]] KindMix profileA();
/// Profile B: work-dominated, about a third of the files sidecars beside assets, about 14% images, about a quarter code, the rest serialised data, text and
/// Markdown (the measured mix of docs/SCALE.md section 2; its C# and C/C++ files are all generated as the two languages here).
[[nodiscard]] KindMix profileB();

struct CorpusSpec
{
    std::uint64_t seed = 1;
    std::size_t targetLines = 10'000;   ///< entries are produced until this many lines (code and text; binaries count none) are reached; the last overshoots
    KindMix mix = profileA();
};

/// One function of a code file as the generator built it: the truth for block identity. The model renders to text, so an edit changes the model and
/// re-renders (the edit stream below).
struct Function
{
    std::uint64_t uid = 0;                  ///< stable across the edits of a stream until the function is split, merged or moved (see EditKind)
    std::string name;                       ///< as `foldRegions` names it (may be qualified, `Klass::method`)
    std::string signature;                  ///< the signature line, without the opening brace or the trailing colon
    std::vector<std::string> body;          ///< the body's lines, already indented, without the brace lines
};

/// A code file as a model: text before the first function, the functions, text after the last. `render` is what the file's text is.
struct CodeFile
{
    Language language = Language::cpp;
    std::vector<std::string> preamble, epilogue;
    std::vector<Function> functions;
    /// The file's text. Every function follows a blank line; C++ functions use the brace-on-its-own-line layout, Python's an indented body.
    [[nodiscard]] std::string render() const;
};

/// One generated file with what is known about it.
struct GeneratedFile
{
    std::string path;
    std::string text;                       ///< binaries: their bytes
    FileKind kind = FileKind::code;
    std::size_t lines = 0;                  ///< newline-terminated lines of a text file; 0 for a binary
    std::optional<CodeFile> code;           ///< set for `code` files: the model `text` was rendered from, and the block truth (one block per function)
};

/// Entry `index` of the corpus: one or two files. A pure function of (spec.seed, spec.mix, index): never depends on any other entry.
[[nodiscard]] std::vector<GeneratedFile> entry(const CorpusSpec& spec, std::size_t index);

/// The entries in order until `spec.targetLines` are reached. Holds one entry at a time, so any size streams.
class CorpusStream
{
public:
    explicit CorpusStream(CorpusSpec spec) : spec_(std::move(spec)) {}
    /// The next entry's files, or nullopt once the target has been reached (the entry that reached it is returned; the next call ends the stream).
    [[nodiscard]] std::optional<std::vector<GeneratedFile>> next();
    [[nodiscard]] std::size_t linesSoFar() const { return lines_; }
    [[nodiscard]] std::size_t entriesSoFar() const { return index_; }

private:
    CorpusSpec spec_;
    std::size_t index_ = 0, lines_ = 0;
};

/// Counts of a corpus, summed over its files.
struct CorpusStats
{
    std::size_t files = 0, entries = 0, lines = 0, codeFiles = 0, codeLines = 0, blocks = 0, bytes = 0;
    std::size_t sidecars = 0, images = 0, serialised = 0, texts = 0, markdowns = 0;
};

struct Corpus
{
    FileMap files;
    std::map<std::string, CodeFile> models;   ///< by path, for the code files: the block truth
    CorpusStats stats;
};

/// The largest corpus `generateCorpus` holds in memory.
inline constexpr std::size_t kMaterialiseLimitLines = 1'000'000;

/// The whole corpus in memory (the gated sizes: 10^3 to 10^5 lines in the smoke tier; up to the limit elsewhere). A larger request is an error that points at the stream.
[[nodiscard]] std::expected<Corpus, std::string> generateCorpus(const CorpusSpec& spec);

/// The mean lines per entry of `spec`'s mix, measured over the first `sample` entries: how many entries a size will take, for a benchmark that samples a 10^9-line
/// project by index (`entry(spec, i)` for i below targetLines / this).
[[nodiscard]] double meanLinesPerEntry(const CorpusSpec& spec, std::size_t sample = 512);

// ---- the edit stream: a commit-like sequence of changes with the identity truth ----------------------------------------------------------------------------

/// What an edit did to one block (the truth `BlockIndex` is checked against; docs/LAYERED_DOCUMENT.md, `Carry`).
///   edited:    a body change, same signature: kept (id unchanged).
///   resigned:  a new signature, same name and body: resigned.
///   renamed:   a new name, same body (a few lines touched): renamed.
///   added:     a function with a body unlike any before: added (a new id).
///   removed:   a function deleted: retired.
///   revived:   a function deleted earlier and written again byte-identically, in the same or another file: revived (its old id).
///   moved:     a function taken out of one file and put byte-identically into another in the same commit: the id follows, provided the consumer updates the
///              file that loses it (`path`) before the file that gains it (`otherPath`); the reverse order is a different outcome (a new id, then a retirement).
///   split:     one function's tail moved to a new function and replaced by a call: the first part keeps the id; the new one is added.
///   merged:    a function's body folded into another and the function removed: the removed one is merged into the survivor.
enum class EditKind { edited, resigned, renamed, added, removed, revived, moved, split, merged };

/// How often each edit kind is drawn (relative weights) and how many files and edits a commit has.
struct EditMix
{
    double edited = 55, resigned = 6, renamed = 5, added = 12, removed = 6, revived = 3, moved = 4, split = 4, merged = 3;
    double newFile = 4, deleteFile = 2;      ///< a whole file added (a fresh code entry) or deleted (every one of its functions removed)
    std::size_t editsPerCommitMax = 4;       ///< a commit holds 1..this many edits, each to a file chosen at random
};

/// One edit's truth. `name` is the function's name before the edit, `newName` after (empty when the edit has none); `uid` the generator's identity for it.
struct EditTruth
{
    EditKind kind = EditKind::edited;
    std::string path, name, newName;
    std::string otherPath;                   ///< moved: the file it went to; revived: the file it was retired from; merged: the survivor's file (same file)
    std::uint64_t uid = 0;                   ///< the function whose identity the edit carries
    bool fileAdded = false, fileDeleted = false;
};

/// One file's new state in a commit: its text, or deleted.
struct FileChange
{
    std::string path;
    std::optional<std::string> text;         ///< nullopt: the file is deleted
};

struct Commit
{
    std::size_t number = 0;                  ///< 1-based
    std::vector<FileChange> changes;         ///< files touched, in path order, each once, with the commit's final text
    std::vector<EditTruth> truth;            ///< one per edit
};

/// A reproducible commit stream over a materialised corpus: `next` applies a few edits to the model and returns the changed files and what each edit means for
/// block identity. Only code files are edited. Deterministic in (corpus, seed, mix).
class EditStream
{
public:
    EditStream(const Corpus& base, std::uint64_t seed, EditMix mix = {});
    [[nodiscard]] Commit next();
    /// The current text of every code file the stream tracks (what the corpus looks like after the commits so far).
    [[nodiscard]] FileMap texts() const;
    [[nodiscard]] std::size_t liveFunctions() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace sub0canvas::bench
