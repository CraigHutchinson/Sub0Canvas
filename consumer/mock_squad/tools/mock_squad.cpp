// mock_squad: run the scenarios of fixtures/scenarios/squad through the reference loop and show each attempt (docs/MOCK_SQUAD.md).
//   mock_squad <scenarios dir> <fixtures/mock> [--update] [name...]
// Without --update it only prints each outcome sequence. With --update it regenerates fixtures/mock/<name>/ (the persisted attempt): review the diff by hand in
// the same commit, because a regenerated file proves only the writer's bytes (docs/TESTING.md: never compare against the code's own output).
#include "mock_squad/loop.hpp"
#include "mock_squad/record.hpp"
#include "mock_squad/scenario.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::println(stderr, "usage: mock_squad <scenarios dir> <fixtures/mock> [--update] [name...]");
        return 2;
    }
    bool update = false;
    std::set<std::string> only;
    for (int i = 3; i < argc; ++i) {
        if (std::string{argv[i]} == "--update") update = true;
        else only.insert(argv[i]);
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.path().extension() == ".squad") files.push_back(e.path());
    std::ranges::sort(files);
    int bad = 0;
    for (const auto& f : files) {
        if (!only.empty() && !only.count(f.stem().string())) continue;
        std::ifstream in(f, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        auto sc = mock_squad::parseScenario(text.str());
        if (!sc) {
            std::println(stderr, "{}: {}", f.filename().string(), sc.error());
            ++bad;
            continue;
        }
        auto attempt = mock_squad::runScenario(*sc);
        if (!attempt) {
            std::println(stderr, "{}", attempt.error());
            ++bad;
            continue;
        }
        std::string seq;
        for (const auto& o : attempt->outcomes) seq += (seq.empty() ? "" : " | ") + o.kind + (o.text.empty() ? "" : ":" + o.text.substr(0, 50));
        std::println("{}: {}", sc->name, seq);
        for (const auto& v : attempt->violations) std::println("  VIOLATION {}", v);
        if (update) mock_squad::writeTree(fs::path(argv[2]) / sc->name, attempt->record);
    }
    return bad == 0 ? 0 : 1;
}
