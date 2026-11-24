// The file log (slice B1b): requirement R-BB-1 over a real file (replay of a recorded log equals the live store, byte for byte), R-BB-7 (the I/O is outside the
// library: the store sees only the `Log` seam), R-BB-9 (what the log wrote is kept verbatim), and the format of BACKBONE.md section 7: a header line with producer
// versions, one event per line, a torn tail discarded on read. The expected bytes are written by hand from the format, not read back from the code.
#include "sub0platform/testing/check.hpp"
#include "file_log.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace sub0canvas;
using sub0canvas::app::FileLog;
using sub0canvas::app::FileLogHeader;

namespace {

namespace fs = std::filesystem;

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& p, const std::string& text, std::ios::openmode mode = std::ios::trunc)
{
    std::ofstream out(p, std::ios::binary | mode);
    out << text;
}

Event ev(std::uint64_t lamport, const std::string& kind, const std::string& subject, const std::string& payload)
{
    Event e;
    e.kind = kind;
    e.writer = {"proj", "w-1"};
    e.lamport = lamport;
    e.subject = subject;
    e.time = "2026-11-24T10:00:00Z";
    e.payload = payload;
    return e;
}

LayerItem item(const std::string& key, const std::string& title)
{
    LayerItem i;
    i.key = key;
    i.kind = "test";
    i.title = title;
    i.detail = "detail of " + key;
    return i;
}

}  // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "sub0canvas_test_file_log";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path file = dir / "events.ndjson";

    const Event first = ev(1, "item.tag@1", "k", R"({"tag":"x"})");
    const Event second = ev(2, "item.tag@1", "k", R"({"tag":"y"})");
    const std::string headerLine = R"({"schema":"canvas-events-1","document":"calc","producers":{"sub0canvas":"0.3.0","mock":"1"}})";
    const std::string line1 = R"({"kind":"item.tag@1","writer":{"namespace":"proj","writer":"w-1"},"lamport":1,"subject":"k","time":"2026-11-24T10:00:00Z","payload":{"tag":"x"}})";

    {
        // A new file: the header with producer versions, then one line per event, each ended by a newline.
        auto log = FileLog::open(file, FileLogHeader{"calc", {{"sub0canvas", "0.3.0"}, {"mock", "1"}}, {}});
        CHECK(log.has_value());
        if (!log) return finish();
        CHECK_EQ(slurp(file), headerLine + "\n");
        CHECK_EQ(log->size(), std::uint64_t{0});
        log->append(first);
        log->append(second);
        CHECK_EQ(log->size(), std::uint64_t{2});
        CHECK(log->error().empty());
        const std::string text = slurp(file);
        CHECK(text.starts_with(headerLine + "\n" + line1 + "\n"));   // appended and flushed: a second reader sees it at once
        CHECK_EQ(std::count(text.begin(), text.end(), '\n'), 3);
    }
    {
        // Reopened: the file's own header wins over the one passed, positions are dense, `since` resumes without a rescan.
        auto log = FileLog::open(file, FileLogHeader{"other", {}, {}});
        CHECK(log.has_value());
        if (!log) return finish();
        CHECK_EQ(log->header().document, std::string("calc"));
        CHECK_EQ(log->header().producers.size(), std::size_t{2});
        CHECK_EQ(log->header().producers[1].second, std::string("1"));
        CHECK_EQ(log->size(), std::uint64_t{2});
        CHECK_EQ(log->tornBytes(), std::uint64_t{0});
        const std::vector<Event> tail = log->since(1);
        CHECK_EQ(tail.size(), std::size_t{1});
        if (!tail.empty()) CHECK(tail[0] == second);
        CHECK_EQ(log->since(0).size(), std::size_t{2});
        CHECK(log->since(5).empty());
    }
    {
        // A torn tail (a crash mid-append): a partial line without its newline is discarded; the file is cut back on open so the next append is clean;
        // a read-only open leaves the file as it is.
        const std::string torn = R"({"kind":"item.pu)";
        spit(file, torn, std::ios::app);
        const std::uint64_t lengthWithTear = fs::file_size(file);
        auto readOnly = FileLog::read(file);
        CHECK(readOnly.has_value());
        if (readOnly) {
            CHECK_EQ(readOnly->size(), std::uint64_t{2});
            CHECK_EQ(readOnly->tornBytes(), static_cast<std::uint64_t>(torn.size()));
        }
        CHECK_EQ(fs::file_size(file), lengthWithTear);
        auto log = FileLog::open(file);
        CHECK(log.has_value());
        if (!log) return finish();
        CHECK_EQ(log->size(), std::uint64_t{2});
        CHECK_EQ(log->tornBytes(), static_cast<std::uint64_t>(torn.size()));
        CHECK_EQ(fs::file_size(file), lengthWithTear - torn.size());
        log->append(ev(3, "item.tag@1", "k", R"({"tag":"z"})"));
        auto again = FileLog::read(file);
        CHECK(again.has_value());
        if (again) {
            CHECK_EQ(again->size(), std::uint64_t{3});
            CHECK_EQ(again->tornBytes(), std::uint64_t{0});
        }
    }
    {
        // A whole line that is not an event is an error wherever it sits (a corrupt file is reported, not silently shortened); so is a wrong header.
        spit(dir / "corrupt.ndjson", headerLine + "\n" + line1 + "\n" + "not json\n" + line1 + "\n");
        const auto bad = FileLog::open(dir / "corrupt.ndjson");
        CHECK(!bad.has_value());
        if (!bad) CHECK(bad.error().contains("line 3"));
        spit(dir / "wrong.ndjson", R"({"schema":"journal-1"})" "\n" + line1 + "\n");
        CHECK(!FileLog::open(dir / "wrong.ndjson").has_value());
        spit(dir / "tornheader.ndjson", R"({"schema":"canvas-ev)");
        CHECK(!FileLog::open(dir / "tornheader.ndjson").has_value());
        CHECK(!FileLog::read(dir / "missing.ndjson").has_value());
    }
    {
        // An unknown header member is kept and a header member order does not matter to the reader.
        spit(dir / "future.ndjson", R"({"document":"d","future":{"a":1},"schema":"canvas-events-1"})" "\n");
        const auto log = FileLog::read(dir / "future.ndjson");
        CHECK(log.has_value());
        if (log) {
            CHECK_EQ(log->header().extra, std::string(R"("future":{"a":1})"));
            CHECK_EQ(log->header().document, std::string("d"));
        }
    }
    {
        // R-BB-1 over a real file: a store writing through a FileLog, and a store that only reads the file afterwards, hold the same snapshot; so does a store
        // that does not keep its events, and one restored from a checkpoint and caught up from the file's tail (B1b).
        fs::remove(dir / "store.ndjson");
        std::string liveSnapshot, midCheckpointJson;
        Checkpoint mid;
        {
            auto log = FileLog::open(dir / "store.ndjson", FileLogHeader{"turns", {{"test", "1"}}, {}});
            CHECK(log.has_value());
            if (!log) return finish();
            StoreOptions o;
            o.writer = {"proj", "w-1"};
            o.sink = &*log;
            o.clock = [] { return std::string("2026-11-24T10:00:00Z"); };
            Store live(o);
            (void)live.putLayer("issues", "gate", std::vector<LayerItem>{item("a", "A"), item("b", "B"), item("c", "C")});
            (void)live.putLayer("issues", "gate", std::vector<LayerItem>{item("a", "A"), item("c", "C")});          // b vanishes
            mid = live.checkpoint();
            (void)live.putLayer("issues", "gate", std::vector<LayerItem>{item("a", "A"), item("b", "B"), item("d", "D")});   // b reopens, c vanishes, d is new
            (void)live.tag("a", "owner/x", "me");
            (void)live.link("d", "causes", "a", "me");
            liveSnapshot = live.snapshotJson();
            CHECK_EQ(log->size(), live.eventCount());
            CHECK(log->error().empty());
        }
        const auto reread = FileLog::read(dir / "store.ndjson");
        CHECK(reread.has_value());
        if (!reread) return finish();
        Store replayed;
        replayed.replay(*reread);
        CHECK_EQ(replayed.snapshotJson(), liveSnapshot);
        CHECK(liveSnapshot.contains("\"state\":\"open\""));

        StoreOptions lean;
        lean.retainEvents = false;
        Store leanStore(lean);
        leanStore.replay(*reread);
        CHECK_EQ(leanStore.snapshotJson(), liveSnapshot);
        CHECK(leanStore.events().empty());
        CHECK_EQ(leanStore.eventCount(), reread->size());

        Store restored;
        CHECK(restored.restore(mid).has_value());
        restored.replay(*reread, mid.events);   // only the tail is read from the file
        CHECK_EQ(restored.snapshotJson(), liveSnapshot);
        CHECK_EQ(restored.events().size(), static_cast<std::size_t>(reread->size() - mid.events));
    }
    fs::remove_all(dir, ec);
    return finish();
}
