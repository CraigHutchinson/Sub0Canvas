#include "file_log.hpp"

#include "json_util.hpp"   // the library's one JSON writer and reader (CMake gives the app lib/src on its include path)

#include <format>
#include <iterator>
#include <system_error>

namespace sub0canvas::app {

namespace {

constexpr std::string_view kSchema = "canvas-events-1";

std::string rawText(std::string_view raw)
{
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\n' || raw.back() == '\t' || raw.back() == '\r')) raw.remove_suffix(1);
    return std::string{raw};
}

std::expected<FileLogHeader, std::string> parseHeader(std::string_view line)
{
    FileLogHeader h;
    bool schemaOk = false;
    json::Writer extra;
    extra.beginObject();
    bool anyExtra = false;
    try {
        json::withObject(line, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string key{json::keyOf(f)};
                auto v = f.value();
                if (key == "schema") schemaOk = json::toString(v) == kSchema;
                else if (key == "document") h.document = json::toString(v);
                else if (key == "producers") {
                    for (auto p : v.get_object()) {
                        const std::string tool{json::keyOf(p)};
                        h.producers.emplace_back(tool, json::toString(p.value()));
                    }
                }
                else { extra.key(key).raw(rawText(v.raw_json())); anyExtra = true; }
            }
        });
    } catch (const simdjson::simdjson_error& err) {
        return std::unexpected(std::format("the first line is not a JSON object: {}", err.what()));
    }
    if (!schemaOk) return std::unexpected(std::format("the first line is not a {} header", kSchema));
    if (anyExtra) {
        extra.endObject();
        const std::string_view whole = extra.view();
        h.extra = std::string{whole.substr(1, whole.size() - 2)};
    }
    return h;
}

}  // namespace

std::string fileLogHeaderLine(const FileLogHeader& header)
{
    json::Writer w;
    w.beginObject().kv("schema", kSchema).kv("document", header.document);
    w.key("producers").beginObject();
    for (const auto& [tool, version] : header.producers) w.kv(tool, version);
    w.endObject();
    if (!header.extra.empty()) w.raw(header.extra);
    w.endObject();
    return w.take();
}

namespace {

/// Reads the file into `log`'s pieces: header, line offsets, the length of the whole lines, and how much torn tail follows them.
struct Scanned
{
    FileLogHeader header;
    std::vector<std::uint64_t> offsets;
    std::uint64_t whole = 0, torn = 0;
};

std::expected<Scanned, std::string> scan(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", path.string()));
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Scanned out;
    std::size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) break;   // no newline: a torn write, whatever it holds
        const std::string_view line{text.data() + pos, nl - pos};
        if (first) {
            auto h = parseHeader(line);
            if (!h) return std::unexpected(h.error());
            out.header = std::move(*h);
            first = false;
        } else {
            if (auto e = parseEvent(line); !e) return std::unexpected(std::format("line {}: {}", out.offsets.size() + 2, e.error()));
            out.offsets.push_back(pos);
        }
        pos = nl + 1;
    }
    out.whole = pos;
    out.torn = text.size() - pos;
    if (first && out.torn != 0) return std::unexpected("the header line is torn: not a log");   // a file that never got a whole header line
    return out;
}

}  // namespace

std::expected<FileLog, std::string> FileLog::read(const std::filesystem::path& path)
{
    auto s = scan(path);
    if (!s) return std::unexpected(s.error());
    FileLog log;
    log.path_ = path;
    log.header_ = std::move(s->header);
    log.offsets_ = std::move(s->offsets);
    log.end_ = s->whole;
    log.torn_ = s->torn;
    return log;
}

std::expected<FileLog, std::string> FileLog::open(const std::filesystem::path& path, FileLogHeader header)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0;
    FileLog log;
    if (exists) {
        auto s = scan(path);
        if (!s) return std::unexpected(s.error());
        if (s->torn != 0) {
            std::filesystem::resize_file(path, s->whole, ec);   // cut back to the last whole line so the next append starts clean
            if (ec) return std::unexpected(std::format("cannot cut the torn tail of {}: {}", path.string(), ec.message()));
        }
        log.header_ = std::move(s->header);
        log.offsets_ = std::move(s->offsets);
        log.end_ = s->whole;
        log.torn_ = s->torn;
    } else {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        const std::string line = fileLogHeaderLine(header) + "\n";
        std::ofstream create(path, std::ios::binary | std::ios::trunc);
        create << line;
        create.flush();
        if (!create) return std::unexpected(std::format("cannot write {}", path.string()));
        log.header_ = std::move(header);
        log.end_ = line.size();
    }
    log.path_ = path;
    log.out_.open(path, std::ios::binary | std::ios::app);
    if (!log.out_) return std::unexpected(std::format("cannot open {} for appending", path.string()));
    return log;
}

void FileLog::append(const Event& e)
{
    if (!error_.empty()) return;
    if (!out_.is_open()) { error_ = "the log was opened for reading only"; return; }
    const std::string line = eventJson(e) + "\n";
    out_.write(line.data(), static_cast<std::streamsize>(line.size()));
    out_.flush();
    if (!out_) { error_ = std::format("cannot append to {}", path_.string()); return; }
    offsets_.push_back(end_);
    end_ += line.size();
}

std::vector<Event> FileLog::since(std::uint64_t from) const
{
    std::vector<Event> events;
    if (from >= offsets_.size()) return events;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return events;
    in.seekg(static_cast<std::streamoff>(offsets_[from]));
    std::string line;
    for (std::uint64_t i = from; i < offsets_.size() && std::getline(in, line); ++i)
        if (auto e = parseEvent(line)) events.push_back(std::move(*e));   // every line was parsed when the file was scanned or appended
    return events;
}

}  // namespace sub0canvas::app
