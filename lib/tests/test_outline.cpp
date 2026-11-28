// Block discovery behind the provider seam (docs/BLOCK_DISCOVERY.md section 2.1; docs/REQUIREMENTS.md, R-BLK): every provider is total,
// deterministic and returns well-formed nesting; the language is detected from the path, filename and first line; folding is a policy over the outline, so the
// outline never depends on the view asked for; and the faults of BLOCK_DISCOVERY.md section 2 are pinned one by one (each case written first and seen failing).
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/blocks.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/outline.hpp"

#include <cstdint>
#include <ranges>
#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

/// Texts of several languages, small enough to be cut at every length.
const std::vector<std::pair<std::string, std::string>>& samples()
{
    static const std::vector<std::pair<std::string, std::string>> s = {
        {"a.cpp", "#include <x>\nnamespace n {\nint f(int a)\n{\n    if (a) { return 1; }\n    return 2;\n}\nstruct S { int g() { return 3; } };\nvoid h() { }\n}\n"},
        {"a.py", "import os\n\n\ndef f(a):\n    return a\n\n\nclass P:\n    def g(self):\n        x = 1\n        return x\n\n    async def h(self):\n        pass\n"},
        {"a.rs", "use std::fmt;\nfn first(x: &'a str) -> &'a str {\n    x\n}\nfn parse<T>(s: &str) -> T {\n    s.parse().unwrap()\n}\n"},
        {"a.go", "package p\n\nfunc (p *Parser) Term() float64 {\n\treturn 1\n}\n\nfunc Free(a int) int {\n\treturn a\n}\n"},
        {"a.js", "const add = (a, b) => {\n  return a + b;\n};\nfunction id(x) {\n  return x;\n}\n"},
        {"a.ts", "function id<T>(x: T): T {\n  return x;\n}\nclass C {\n  m(a: number) {\n    return a;\n  }\n}\n"},
        {"a.cpp", "/* { not a block */\nint f() {\n    return 1;\n}\nint g() {\n    return 2;\n}\n"},
    };
    return s;
}

/// The invariants every outline must hold (R-BLK-3): spans inside the file, a node inside its parent, siblings in order and disjoint, the name line inside the span.
void checkNesting(const std::vector<OutlineNode>& nodes, int lo, int hi, int lines, bool& ok)
{
    int previousEnd = lo - 1;
    for (const auto& n : nodes) {
        if (n.first < 1 || n.last < n.first || n.last > std::max(lines, 1) || n.first < lo || n.last > hi || n.first <= previousEnd) ok = false;
        if (n.nameLine < n.first || n.nameLine > n.last) ok = false;
        if (n.bodyFrom != 0 && (n.bodyFrom < n.first || n.bodyTo > n.last || n.bodyTo < n.bodyFrom)) ok = false;
        checkNesting(n.children, n.first, n.last, lines, ok);
        previousEnd = n.last;
    }
}

int lineCount(const std::string& text)
{
    int n = 0;
    for (std::size_t i = 0; i < text.size(); ++i) if (text[i] == '\n' || (i + 1 == text.size())) ++n;
    return n;
}

bool wellFormed(const Outline& o, const std::string& text)
{
    bool ok = true;
    checkNesting(o.nodes, 1, std::max(lineCount(text), 1), lineCount(text), ok);
    return ok;
}

/// The names of the nodes of one file, in order.
std::vector<std::string> namesOf(const std::string& path, const std::string& text)
{
    std::vector<std::string> names;
    for (const auto& n : defaultLanguages().outline(path, text).nodes) names.push_back(n.name);
    return names;
}

}  // namespace

int main()
{
    {   // R-BLK-8: the language comes from the filename, the extension, then the first line; a hidden file called `.py` is not Python.
        const Languages& l = defaultLanguages();
        CHECK_EQ(l.detect("calc/engine.py"), std::string("python"));
        CHECK_EQ(l.detect("tools/run", "#!/usr/bin/env python3"), std::string("python"));
        CHECK_EQ(l.detect("tools/run", "#!/usr/bin/python3.12"), std::string("python"));
        CHECK_EQ(l.detect("tools/run", "#!/bin/sh"), std::string(""));
        CHECK_EQ(l.detect("src/a.cpp"), std::string("cpp"));
        CHECK_EQ(l.detect(".py"), std::string(""));
        CHECK_EQ(l.detect("tools/.py"), std::string(""));
        CHECK_EQ(l.detect("lib/example.rs"), std::string("rust"));
        CHECK_EQ(l.detect("ui/example.ts"), std::string("typescript"));
        CHECK_EQ(l.detect("cmd/example.go"), std::string("go"));
        const auto prose = l.outline("guide.md", "# Start ###\nbody\n~~~markdown\n# Example\n~~~\n## Next\nbody\n");
        CHECK(prose.language == "markdown" && prose.provider == "markdown@1" && prose.nodes.size() == 2);
        CHECK(prose.nodes[0].name == "Start" && prose.nodes[0].first == 1 && prose.nodes[0].last == 5);
        CHECK(prose.nodes[1].name == "Next" && prose.nodes[1].first == 6 && prose.nodes[1].last == 7);
        CHECK(l.provider("python") != nullptr && l.provider("python")->name() == "indent@2");
        CHECK(l.provider("")->name() == "braces@3");
        // the registry is data: a language added by a client wins by priority, and a filename beats an extension
        Languages mine;
        mine.detects("cmake", {".cmake"}, {"CMakeLists.txt"});
        mine.detects("text", {".txt"});
        CHECK_EQ(mine.detect("a/CMakeLists.txt"), std::string("cmake"));
        CHECK_EQ(mine.detect("a/b.txt"), std::string("text"));
        CHECK(mine.provider("cmake") == nullptr);                // no provider and no fallback: nothing, not a crash
        CHECK(mine.outline("a.cmake", "function(x)\nendfunction()\n").nodes.empty());
        // an extensionless script is outlined by its interpreter's provider
        CHECK_EQ(namesOf("tools/run", "#!/usr/bin/env python3\ndef a():\n    x = 1\n    return x\n"), std::vector<std::string>{"a"});
    }
    {   // The seam changes nothing today: foldRegions over the providers equals the regions of the outline for the path, whatever the view (R-BLK-11, R-BLK-12).
        for (const auto& [path, text] : samples()) {
            const bool py = isPythonPath(path);
            for (const bool all : {false, true}) {
                const auto a = foldRegions(text, py, all), b = foldRegionsFor(path, text, all);
                CHECK_EQ(a.size(), b.size());
                for (const auto& [x, y] : std::views::zip(a, b))
                    CHECK(x.first == y.first && x.bodyFrom == y.bodyFrom && x.bodyTo == y.bodyTo && x.name == y.name);
            }
            const Outline once = defaultLanguages().outline(path, text), twice = defaultLanguages().outline(path, text);
            CHECK(once.nodes.size() == twice.nodes.size() && once.provider == twice.provider);
            // a view asked for (a budget, focus lines, open symbols) never moves a block
            (void)foldedView(text, py, {1, 3}, 40);
            (void)sectionView(text, py, {"f"});
            CHECK_EQ(defaultLanguages().outline(path, text).nodes.size(), once.nodes.size());
        }
    }
    {   // R-BLK-1: total and deterministic on every prefix of every sample and on noise; R-BLK-3: well-formed nesting.
        std::uint32_t seed = 12345;
        const auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 16; };
        const std::string alphabet = "{}()\"'`/*#\\ \n\tdef;:<>=a1";
        bool ok = true, again = true;
        for (const auto& [path, text] : samples())
            for (std::size_t n = 0; n <= text.size(); ++n) {
                const std::string cut = text.substr(0, n);
                const Outline o = defaultLanguages().outline(path, cut);
                ok = ok && wellFormed(o, cut);
                again = again && o.nodes.size() == defaultLanguages().outline(path, cut).nodes.size();
            }
        for (int round = 0; round < 300; ++round) {
            std::string noise;
            for (std::uint32_t k = next() % 120; k > 0; --k) noise += alphabet[next() % alphabet.size()];
            for (const char* path : {"x.cpp", "x.py", "x.rs", "x.go", "x.js"}) ok = ok && wellFormed(defaultLanguages().outline(path, noise), noise);
        }
        CHECK(ok);
        CHECK(again);
        CHECK(defaultLanguages().outline("a.cpp", "").nodes.empty());
        CHECK(defaultLanguages().outline("a.py", "\n\n").nodes.empty());
    }
    {   // R-BLK-5: a brace inside a block comment never opens a block; the functions after it are still found.
        CHECK_EQ(namesOf("a.cpp", "/* { not a block */\nint f() {\n    return 1;\n}\nint g() {\n    return 2;\n}\n"), (std::vector<std::string>{"f", "g"}));
        CHECK_EQ(namesOf("a.cpp", "/*\n * a { across\n * lines\n */\nint f() {\n    return 1;\n}\n"), std::vector<std::string>{"f"});
        CHECK_EQ(namesOf("a.cpp", "int f() { /* } */\n    return 1;\n}\n"), std::vector<std::string>{"f"});   // a closing brace in a trailing comment
        CHECK_EQ(namesOf("a.cpp", "const char* s = \"/*\";\nint f() {\n    return 1;\n}\n"), std::vector<std::string>{"f"});   // a comment opener inside a string
        // a `{` inside a comment in a function body used to leave the function open to the end of the file, so it and every later function vanished
        CHECK_EQ(namesOf("a.cpp", "int f() {\n    /* { */\n    return 1;\n}\nint g() {\n    return 2;\n}\n"), (std::vector<std::string>{"f", "g"}));
        CHECK_EQ(namesOf("a.cpp", "int f() {\n    /*\n     * unbalanced {\n     */\n    return 1;\n}\nint g() {\n    return 2;\n}\n"), (std::vector<std::string>{"f", "g"}));
        CHECK_EQ(namesOf("a.cpp", "/* int fake() { */\nint f() {\n    return 1;\n}\n"), std::vector<std::string>{"f"});   // a signature inside a comment is not one
    }
    {   // R-BLK-4: a node that carries an identity has a name. The faults of BLOCK_DISCOVERY.md section 2 gave empty names; each of these now names every function.
        CHECK_EQ(namesOf("a.rs", "fn parse<T>(s: &str) -> T {\n    s.parse().unwrap()\n}\n"), std::vector<std::string>{"parse"});
        CHECK_EQ(namesOf("a.ts", "function id<T>(x: T): T {\n  return x;\n}\n"), std::vector<std::string>{"id"});
        CHECK_EQ(namesOf("a.js", "const add = (a, b) => {\n  return a + b;\n};\n"), std::vector<std::string>{"add"});
        CHECK_EQ(namesOf("a.go", "func (p *Parser) Term() float64 {\n\treturn 1\n}\n"), std::vector<std::string>{"Term"});
        // more shapes of the same faults: modifiers and wrappers, function expressions, typed bindings, Go generics; C++ operators unchanged
        CHECK_EQ(namesOf("a.rs", "pub async fn load<T: Into<String>>(s: T) -> Vec<T> {\n    todo!()\n}\n"), std::vector<std::string>{"load"});
        CHECK_EQ(namesOf("a.ts", "export const handler: Handler = async (req, res) => {\n  return 1;\n};\n"), std::vector<std::string>{"handler"});
        CHECK_EQ(namesOf("a.js", "let f = function (a) {\n  return a;\n};\n"), std::vector<std::string>{"f"});
        CHECK_EQ(namesOf("a.go", "func Map[T any](xs []T) []T {\n\treturn xs\n}\n"), std::vector<std::string>{"Map"});
        CHECK_EQ(namesOf("a.go", "func (s Stack[T]) Push(x T) {\n\ts.items = append(s.items, x)\n}\n"), std::vector<std::string>{"Push"});
        const auto goQ = defaultLanguages().outline("a.go", "func (p *Parser) Term() float64 {\n\treturn 1\n}\nfunc Term() int {\n\treturn 2\n}\n");
        CHECK_EQ(goQ.nodes.size(), static_cast<std::size_t>(2));   // the method and the free function of one name stay two addressable blocks
        if (goQ.nodes.size() == 2) CHECK(goQ.nodes[0].qualifiedName == "Parser::Term" && goQ.nodes[1].qualifiedName == "Term");
        CHECK_EQ(namesOf("a.cpp", "bool operator<(const A& a, const A& b) {\n    return true;\n}\ntemplate <class T>\nT id(T x) {\n    return x;\n}\n"), (std::vector<std::string>{"operator<", "id"}));   // named, not left empty
    }
    {   // R-BLK-6: decorators are inside the span of their block; the signature line stays the name line, so views and SYMBOL edits address it as before.
        const std::string py = "class P:\n    @staticmethod\n    @other(1)\n    def term(x):\n        a = 1\n        return a\n\n    def plain(self):\n        a = 1\n        return a\n\n@top\nasync def last(y):\n    a = 1\n    return a\n";
        const auto out = defaultLanguages().outline("a.py", py);
        CHECK_EQ(out.nodes.size(), static_cast<std::size_t>(3));
        if (out.nodes.size() == 3) {
            CHECK(out.nodes[0].first == 2 && out.nodes[0].nameLine == 4 && out.nodes[0].last == 6 && out.nodes[0].name == "term");
            CHECK(out.nodes[1].first == 8 && out.nodes[1].nameLine == 8 && out.nodes[1].name == "plain");   // no decorator: unchanged
            CHECK(out.nodes[2].first == 12 && out.nodes[2].nameLine == 13 && out.nodes[2].name == "last");
        }
        const auto regions = foldRegionsFor("a.py", py, true);
        CHECK_EQ(regions.size(), static_cast<std::size_t>(3));
        if (regions.size() == 3) CHECK(regions[0].first == 4 && regions[0].spanFirst == 2 && regions[1].spanFirst == 8);   // the view's signature line is unchanged
        BlockIndex index;
        index.update("a.py", py);
        const Block* onDecorator = index.at("a.py", 3);
        CHECK(onDecorator != nullptr && onDecorator->name == "term");   // a line of the decorator belongs to the block
        if (onDecorator) CHECK(onDecorator->first == 2 && onDecorator->signature == "def term(x):");
    }
    return finish();
}
