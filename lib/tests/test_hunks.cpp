// Hunk ranges (docs/LAYERED_DOCUMENT.md, "Hunk ranges"): the hunks of a -U0 diff mapped into block-relative spans, and the spans written on the turn's events.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/hunks.hpp"

#include <simdjson.h>

#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

const std::vector<BlockSpan> kBefore = {{"a", 3, 12}, {"b", 15, 30}};
const std::vector<BlockSpan> kAfter = {{"a", 3, 12}, {"b", 15, 30}};

}  // namespace

int main()
{
    {   // parseHunks: counts default to 1, a path with a space survives, a deleted file's hunks are listed under its path.
        const auto h = parseHunks("diff --git a/src/a b.cpp b/src/a b.cpp\nindex 1..2 100644\n--- a/src/a b.cpp\n+++ b/src/a b.cpp\n@@ -5 +5,2 @@ int f()\n-x\n+y\n+z\n"
                                  "@@ -20,3 +21,0 @@\n-p\n-q\n-r\ndiff --git a/gone.h b/gone.h\ndeleted file mode 100644\n--- a/gone.h\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-a\n-b\n");
        CHECK_EQ(h.size(), std::size_t{2});
        const auto& v = h.at("src/a b.cpp");
        CHECK_EQ(v.size(), std::size_t{2});
        CHECK(v[0].oldStart == 5 && v[0].oldLen == 1 && v[0].newStart == 5 && v[0].newLen == 2);
        CHECK(v[1].oldStart == 20 && v[1].oldLen == 3 && v[1].newStart == 21 && v[1].newLen == 0);
        CHECK_EQ(h.at("gone.h").size(), std::size_t{1});
    }
    {   // (a) an edit inside one block: only the changed lines, relative to the block.
        const auto m = mapHunks({{7, 2, 7, 3}}, kBefore, kAfter);
        CHECK_EQ(m.total, std::size_t{1});
        CHECK_EQ(m.outside, std::size_t{0});
        CHECK_EQ(m.byBlock.size(), std::size_t{1});
        CHECK(m.byBlock.at("a") == (std::vector<HunkSpan>{{5, 2, 5, 3}}));   // lines 7-8 of the file are lines 5-6 of the block
    }
    {   // (b) a rewrite of the whole block: the span covers it, on both sides.
        const auto m = mapHunks({{15, 16, 15, 16}}, kBefore, kAfter);
        CHECK(m.byBlock.at("b") == (std::vector<HunkSpan>{{1, 16, 1, 16}}));
    }
    {   // a hunk between blocks touches none; one across two blocks is placed once and clipped for each.
        const auto between = mapHunks({{13, 1, 13, 1}, {13, 0, 14, 1}}, kBefore, kAfter);
        CHECK_EQ(between.total, std::size_t{2});
        CHECK_EQ(between.outside, std::size_t{2});
        CHECK(between.byBlock.empty());
        const auto across = mapHunks({{10, 6, 10, 6}}, kBefore, kAfter);
        CHECK_EQ(across.total - across.outside, std::size_t{1});
        CHECK(across.byBlock.at("a") == (std::vector<HunkSpan>{{8, 3, 8, 3}}));
        CHECK(across.byBlock.at("b") == (std::vector<HunkSpan>{{1, 1, 1, 1}}));
    }
    {   // a deletion inside a block is a zero-length new side; a block that is new at the new revision takes nothing.
        const auto del = mapHunks({{5, 2, 4, 0}}, kBefore, kAfter);
        CHECK(del.byBlock.at("a") == (std::vector<HunkSpan>{{3, 2, 2, 0}}));
        const auto added = mapHunks({{40, 0, 41, 5}}, kBefore, {{"a", 3, 12}, {"b", 15, 30}, {"c", 41, 45}});
        CHECK_EQ(added.outside, std::size_t{1});
    }
    {   // the writer: spans ride on that turn's events only, and the turn's totals are written.
        BlockIndex index;
        index.setTurn(0);
        index.update("f.cpp", "int a()\n{\n    return 1;\n}\n");
        index.setTurn(1);
        index.update("f.cpp", "int a()\n{\n    return 2;\n}\n");
        const auto id = index.blocks("f.cpp").at(0)->id;
        SnapshotSpec spec;
        spec.turn = 1;
        spec.files = {{"f.cpp", "int a()\n{\n    return 2;\n}\n"}};
        spec.blockHunks[id] = {{3, 1, 3, 1}};
        spec.hunkCounts = {{2, 1}};
        simdjson::ondemand::parser parser;
        const auto json = simdjson::padded_string(layeredJson(spec, index));
        auto doc = parser.iterate(json);
        std::size_t withHunks = 0;
        for (auto e : doc["events"]) {
            const int64_t turn = e["turn"].get_int64().value();
            const std::string_view detail = e["detail"].get_string();
            auto h = e["hunks"];
            if (h.error() != simdjson::SUCCESS) continue;
            ++withHunks;
            CHECK(turn == 1);
            CHECK(detail == "edited");
            std::vector<int64_t> flat;
            for (auto span : h) for (auto n : span) flat.push_back(n.get_int64().value());
            CHECK((flat == std::vector<int64_t>{3, 1, 3, 1}));
        }
        CHECK_EQ(withHunks, std::size_t{1});
        CHECK(doc["diffHunks"]["total"].get_int64().value() == int64_t{2});
        CHECK(doc["diffHunks"]["outside"].get_int64().value() == int64_t{1});
    }
    return finish();
}
