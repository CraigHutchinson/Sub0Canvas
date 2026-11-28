#include "sub0canvas/block_table.hpp"
#include "sub0platform/testing/check.hpp"

using namespace sub0canvas;
using namespace std::string_view_literals;

int main()
{
    Replayed state;
    state.revision = "indexed";
    state.ordinal = 2;
    state.historyCut = true;
    state.unknown.push_back({"missing", "b.cpp"});
    state.index.update("a.cpp", "int a() { return 1; }\nint b() { return 2; }\n");
    state.index.update("c.cpp", "int c() { return 3; }\n");
    const auto bytes = writeBlockTable("requested", state);
    CHECK(bytes);
    CHECK(bytes->starts_with("S0BLOCK1\x09\0\0\0"sv));
    const auto table = readBlockTable(*bytes);
    CHECK(table);
    CHECK(table->requestedRevision == "requested" && table->indexedRevision == "indexed");
    CHECK(table->ordinal == 2 && table->historyCut);
    CHECK(table->unknown.size() == 1 && table->unknown[0].first == "missing" && table->unknown[0].second == "b.cpp");
    const auto actual = state.index.all();
    CHECK(actual.size() == 3 && table->blocks.size() == actual.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto& row = table->blocks[i];
        const auto& block = *actual[i];
        CHECK(row.id == block.id && row.path == block.path && row.name == block.name && row.signature == block.signature);
        CHECK(row.first == block.first && row.last == block.last);
    }
    for (std::size_t i = 0; i < bytes->size(); ++i) CHECK(!readBlockTable(std::string_view(*bytes).substr(0, i)));
    CHECK(!readBlockTable(*bytes + "tail"));
    auto badVersion = *bytes;
    badVersion[7] = '2';
    CHECK(!readBlockTable(badVersion));
    return 0;
}
