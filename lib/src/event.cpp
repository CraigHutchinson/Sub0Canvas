#include "sub0canvas/event.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <format>

namespace sub0canvas {

std::strong_ordering eventOrder(const Event& a, const Event& b)
{
    if (const auto c = a.lamport <=> b.lamport; c != 0) return c;
    if (const auto c = a.writer.ns <=> b.writer.ns; c != 0) return c;
    return a.writer.writer <=> b.writer.writer;
}

KindRef splitKind(std::string_view kind)
{
    const std::size_t at = kind.find('@');
    if (at == std::string_view::npos) return {kind, {}};
    return {kind.substr(0, at), kind.substr(at + 1)};
}

std::string eventJson(const Event& e)
{
    json::Writer w;
    w.beginObject().kv("kind", e.kind);
    w.key("writer").beginObject().kv("namespace", e.writer.ns).kv("writer", e.writer.writer).endObject();
    w.kv("lamport", static_cast<std::int64_t>(e.lamport)).kv("subject", e.subject).kv("time", e.time);
    w.kvRaw("payload", e.payload.empty() ? std::string_view{"{}"} : std::string_view{e.payload});
    if (!e.extra.empty()) w.raw(e.extra);   // the writer places the comma; the text is members, kept as they were read
    w.endObject();
    return w.take();
}

namespace {

/// A raw value's text without the whitespace simdjson leaves after it.
std::string rawText(std::string_view raw)
{
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\n' || raw.back() == '\t' || raw.back() == '\r')) raw.remove_suffix(1);
    return std::string{raw};
}

}  // namespace

std::expected<Event, std::string> parseEvent(std::string_view text)
{
    Event e;
    bool haveKind = false, haveWriter = false, haveLamport = false;
    json::Writer extra;   // collects unknown members as an object, whose braces are stripped below
    extra.beginObject();
    bool anyExtra = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string key{json::keyOf(field)};
                auto value = field.value();
                if (key == "kind") { e.kind = json::toString(value); haveKind = true; }
                else if (key == "lamport") { e.lamport = value.get_uint64(); haveLamport = true; }
                else if (key == "subject") e.subject = json::toString(value);
                else if (key == "time") e.time = json::toString(value);
                else if (key == "payload") e.payload = rawText(value.raw_json());
                else if (key == "writer") {
                    simdjson::ondemand::object w = value.get_object();
                    for (auto wf : w) {
                        const std::string wk{json::keyOf(wf)};
                        auto wv = wf.value();
                        if (wk == "namespace") e.writer.ns = json::toString(wv);
                        else if (wk == "writer") e.writer.writer = json::toString(wv);
                    }
                    haveWriter = true;
                }
                else { extra.key(key).raw(rawText(value.raw_json())); anyExtra = true; }
            }
        });
    } catch (const simdjson::simdjson_error& err) {
        return std::unexpected(std::format("event is not valid JSON of the expected shape: {}", err.what()));
    }
    if (!haveKind || e.kind.empty()) return std::unexpected("event has no kind");
    if (!haveWriter) return std::unexpected("event has no writer");
    if (!haveLamport) return std::unexpected("event has no lamport");
    if (e.payload.empty()) e.payload = "{}";
    if (anyExtra) {
        extra.endObject();
        const std::string_view whole = extra.view();
        e.extra = std::string{whole.substr(1, whole.size() - 2)};
    }
    return e;
}

void MemoryLog::append(const Event& e) { events_.push_back(e); }

std::vector<Event> MemoryLog::since(std::uint64_t from) const
{
    if (from >= events_.size()) return {};
    return {events_.begin() + static_cast<std::ptrdiff_t>(from), events_.end()};
}

}  // namespace sub0canvas
