#include "git_source.hpp"
#include "sub0canvas/resolver.hpp"
#include "sub0platform/testing/check.hpp"

#include <filesystem>
#include <fstream>

using namespace sub0canvas;
namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const fs::path repo = argv[1];
    fs::remove_all(repo);
    fs::create_directories(repo);
    const auto git = [&](const std::string& args) { return app::runGit(repo.string(), args).has_value(); };
    const auto write = [&](int value) {
        for (const auto* path : {"a.cpp", "b.cpp"}) {
            std::ofstream file(repo / path, std::ios::binary | std::ios::trunc);
            file << "int f() { return " << value << "; }\n";
        }
    };
    CHECK(git("init --quiet"));
    write(1);
    CHECK(git("add a.cpp b.cpp"));
    CHECK(git("-c user.name=Test -c user.email=test@example.test commit --quiet -m first"));
    app::GitSource source(repo.string());
    const auto first = source.resolve("HEAD");
    CHECK(first.has_value());
    CHECK(!source.resolve("later"));
    RevisionIndex index(source);
    const Scope scope{"a.cpp", "b.cpp"};
    HistoryDocument pinned(index, source, scope, "HEAD");
    CHECK(pinned.revision() == *first);
    CHECK(pinned.text("a.cpp").text && pinned.text("a.cpp").text->contains("return 1"));
    write(2);
    CHECK(git("add a.cpp b.cpp"));
    CHECK(git("-c user.name=Test -c user.email=test@example.test commit --quiet -m second"));
    CHECK(git("branch later"));
    const auto second = source.resolve("HEAD");
    CHECK(second.has_value() && second != first);
    CHECK(source.resolve("later") == second);
    CHECK(pinned.text("b.cpp").text && pinned.text("b.cpp").text->contains("return 1"));
    HistoryDocument tip(index, source, scope, "HEAD");
    CHECK(tip.text("b.cpp").text && tip.text("b.cpp").text->contains("return 2"));
    fs::remove_all(repo);
    return finish();
}
