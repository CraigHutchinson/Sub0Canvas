// Views and skeletons the consumers depend on (docs/REQUIREMENTS.md, R-VIEW and R-SKEL), checked here and not only in Sub0Squad: the outline of a file,
// one section open with its neighbours folded, a function addressed by name down to one line, and `@@ signature` lines completed into stubs per language.
// These checks were first written in Sub0Squad's test_blocks (block 4, row 46); they state the library's contract, so they live with the library.
#include "check.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/outline.hpp"

#include <string>

using namespace sub0canvas;

int main()
{
    {   // R-VIEW-3: functions worth folding by default; R-VIEW-4: every function by name, down to one line.
        // R-VIEW-5: a section view opens the owner's function and folds the rest, naming each fold.
        // R-VIEW-6: a read that names only the path returns the outline, never the whole file.
        const std::string py = "import math\n\n\ndef area(w, h):\n    a = w * h\n    return a\n\n\ndef one(x):\n    return x\n\n\nclass P:\n    def long(self):\n        a = 1\n        b = 2\n        return a + b\n";
        CHECK_EQ(foldRegions(py, true).size(), static_cast<std::size_t>(2));          // worth folding: area and long
        CHECK_EQ(foldRegions(py, true, true).size(), static_cast<std::size_t>(3));    // by name: the one-line function too
        const std::string view = sectionView(py, true, {"area"});
        CHECK(view.find("a = w * h") != std::string::npos);
        CHECK(view.find("b = 2") == std::string::npos && view.find("lines folded: 15-17; long") != std::string::npos);
        const std::string outline = answerRead(ReadRequest{"m.py", 0, 0, {}}, py, true);
        CHECK(outline.find("outline (17 lines") != std::string::npos && outline.find("a = w * h") == std::string::npos && outline.find("def area(w, h):") != std::string::npos);
        const auto reads = parseReadRequests("=== READ: m.py ===\n=== READ: m.py SYMBOL one ===\n=== READ: m.py WHATEVER ===\n");
        CHECK_EQ(reads.size(), static_cast<std::size_t>(2));                           // an unknown form is not a read
        CHECK(reads.size() == 2 && answerRead(reads[1], py, true).find("return x") != std::string::npos);
    }
    {   // R-CAN-6: one-line functions are replaced by name; R-CAN-8: the log records each file's revision.
        Canvas cv;
        cv.addFile("a.cpp", "int tiny() { return 1; }\n\nint two()\n{\n    return 2;\n}\n");
        CHECK(cv.apply({"a.cpp", 1, "t", {{CanvasOp::Kind::replaceSymbol, 0, 0, "tiny", "int tiny() { return 10; }"}}}).ok);
        CHECK(cv.apply({"a.cpp", 2, "t", {{CanvasOp::Kind::replaceSymbol, 0, 0, "two", "int two() { return 20; }"}}}).ok);
        CHECK_EQ(cv.text("a.cpp"), std::string("int tiny() { return 10; }\n\nint two() { return 20; }\n"));
        CHECK(cv.logJson().find("\"a.cpp\":3") != std::string::npos);
    }
    {   // R-CAN-2: two insertions after one line keep the order given; R-CAN-7: a whole-text replacement is a new revision and the old one stays readable.
        Canvas cv;
        cv.addFile("f", "a\nb\n");
        CHECK(cv.apply({"f", 1, "t", {{CanvasOp::Kind::insertAfter, 1, 0, "", "first"}, {CanvasOp::Kind::insertAfter, 1, 0, "", "second"}}}).ok);
        CHECK_EQ(cv.text("f"), std::string("a\nfirst\nsecond\nb\n"));
        CHECK(cv.replaceText("f", "z\n", "t").ok && cv.revision("f") == 3 && cv.textAt("f", 2) == "a\nfirst\nsecond\nb\n");
        CHECK(cv.replaceText("new.txt", "n\n", "t").ok && cv.revision("new.txt") == 1);   // a file not yet on the canvas is added
        CHECK((cv.paths() == std::vector<std::string>{"f", "new.txt"}));
    }
    {   // R-SKEL-1: `@@ signature` lines become stubbed functions that raise "not implemented: <name>", per language, and each is addressable by name.
        // R-SKEL-2: C++ gets what the stubs need (<stdexcept>, prototypes so sections may call each other in any order; none for methods).
        // R-SKEL-3: a line that is not a signature, or a function declared twice, is refused with a reason; a file without `@@` is unchanged.
        const Skeleton py = expandSkeleton("import math\n\nclass P:\n    def __init__(self):\n        self.n = 0\n\n    @@ def term(self, x)\n\n@@ def evaluate(expr):\n", "calc/engine.py");
        CHECK(py.error.empty() && py.sections.size() == 2 && py.sections[0].name == "term" && py.sections[1].name == "evaluate");
        CHECK(py.text.find("    def term(self, x):\n        raise NotImplementedError(\"not implemented: term\")\n") != std::string::npos);
        CHECK(py.text.find("def evaluate(expr):\n    raise NotImplementedError(\"not implemented: evaluate\")\n") != std::string::npos && py.text.find("@@") == std::string::npos);
        CHECK_EQ(foldRegions(py.text, true, true).size(), static_cast<std::size_t>(3));
        const Skeleton cpp = expandSkeleton("#include <cmath>\n\nnamespace n {\n\nclass P\n{\n    @@ double term();\n};\n\n@@ static double sq(double x)\n@@ double area(double r) {\n\n}\n", "src/a.cpp");
        CHECK(cpp.error.empty() && cpp.sections.size() == 3);
        CHECK(cpp.text.find("#include <cmath>\n#include <stdexcept>\n") != std::string::npos);
        CHECK(cpp.text.find("static double sq(double x);\ndouble area(double r);\n\nstatic double sq(double x)\n{\n    (void)x;\n    throw std::logic_error(\"not implemented: sq\");\n}\n") != std::string::npos);
        CHECK(cpp.text.find("    double term()\n    {\n        throw std::logic_error(\"not implemented: term\");\n    }\n") != std::string::npos);
        CHECK(cpp.text.find("double term();") == std::string::npos);
        const Skeleton js = expandSkeleton("@@ export function evaluate(expr) {\n", "src/calc.js");
        CHECK_EQ(js.text, std::string("export function evaluate(expr) {\n    throw new Error('not implemented: evaluate');\n}\n"));
        CHECK(expandSkeleton("x = 1\n@@ compute the total\n", "a.py").error.find("line 2 is not a function signature") != std::string::npos);
        CHECK(expandSkeleton("@@ def f(a)\n@@ def f(b)\n", "a.py").error.find("declared twice") != std::string::npos);
        CHECK(expandSkeleton("x = 1\n", "a.py").sections.empty() && expandSkeleton("x = 1\n", "a.py").text == "x = 1\n");
        // R-SKEL-2: a placeholder uses every named parameter, whatever its type; unnamed ones and defaults are left alone.
        const Skeleton sk = expandSkeleton("@@ double f(const std::string& name, std::vector<std::pair<int, int>> pairs, int n = 2, double)\n", "a.cpp");
        CHECK(sk.text.find("{\n    (void)name;\n    (void)pairs;\n    (void)n;\n    throw std::logic_error") != std::string::npos);
    }
    {   // R-VIEW-7: the language is chosen by extension.
        CHECK(isPythonPath("calc/engine.py") && !isPythonPath("src/a.cpp") && !isPythonPath("src/a.js"));
    }
    return finish();
}
