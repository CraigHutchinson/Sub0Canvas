#pragma once
// The file implementation of the log seam (docs/BACKBONE.md section 7, A7, A18; slice B1b). The library's `Log` is pure (R-BB-7: no file reads); this is the I/O
// that implements it, so it lives with the application and the tests that drive it, never under lib/.
//
// Format, schema `canvas-events-1` (events.ndjson): the first line is a header `{"schema":"canvas-events-1","document":"<name>","producers":{"<tool>":"<version>"}}`,
// then one event per line as `eventJson` writes it (the envelope of A22), each line ended by '\n'. A line without its '\n' is a torn write (a crash
// mid-append): it is discarded on open and the file is cut back to the last whole line, so the next append starts on a clean line. A line that is whole but
// is not an event is an error wherever it sits: a corrupt file is reported, not silently shortened. Unknown header members are kept in `header().extra`.
#include "sub0canvas/event.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace sub0canvas::app {

/// What the header of a log names. `producers` records which tool (and version) wrote events here, so a later reader can tell which reducers made them (A7).
struct FileLogHeader
{
    std::string document;                                          ///< what the events are about (a project, an attempt)
    std::vector<std::pair<std::string, std::string>> producers;    ///< tool name to version, in the order given
    std::string extra;                                             ///< header members this version does not know, as raw JSON members, kept and written back
};

/// An append-only event log in a file. Opening an existing file reads it (discarding a torn tail); opening a missing or empty one writes the header. `append`
/// writes one line and flushes it to the operating system (not to disk: durability beyond that is the caller's policy). A failed write is not thrown from
/// `append` (the seam returns void): it sets `error()`, and nothing more is written after it.
class FileLog final : public Log
{
public:
    /// Opens `path` for appending. For a new file `header` is written; for an existing one it is read and `header` is ignored (`header()` is the file's).
    /// The error names the first problem: an unreadable file, a first line that is not a canvas-events-1 header, a whole line that is not an event.
    [[nodiscard]] static std::expected<FileLog, std::string> open(const std::filesystem::path& path, FileLogHeader header = {});

    /// Reads a log without opening it for writing (a reader that must not touch the file): the header and every whole event.
    [[nodiscard]] static std::expected<FileLog, std::string> read(const std::filesystem::path& path);

    FileLog(FileLog&&) noexcept = default;
    FileLog& operator=(FileLog&&) noexcept = default;

    void append(const Event& e) override;
    [[nodiscard]] std::vector<Event> since(std::uint64_t from) const override;
    [[nodiscard]] std::uint64_t size() const override { return offsets_.size(); }

    [[nodiscard]] const FileLogHeader& header() const { return header_; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    /// Bytes of torn tail cut off when the file was opened (0: it ended on a whole line).
    [[nodiscard]] std::uint64_t tornBytes() const { return torn_; }
    /// The first write error, empty when none.
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    FileLog() = default;
    std::filesystem::path path_;
    FileLogHeader header_;
    std::vector<std::uint64_t> offsets_;   ///< the byte offset of each event's line, so `since(n)` seeks instead of scanning
    std::uint64_t end_ = 0;                ///< the file's length: where the next line goes
    std::uint64_t torn_ = 0;
    std::ofstream out_;                    ///< open for appending (empty in a read-only log)
    std::string error_;
};

/// The header line for `header`, without its newline (exposed so a test can check the bytes).
[[nodiscard]] std::string fileLogHeaderLine(const FileLogHeader& header);

}  // namespace sub0canvas::app
