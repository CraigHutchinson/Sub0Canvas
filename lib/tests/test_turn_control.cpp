// Turn control (docs/REQUIREMENTS.md, R-TURN-1 to R-TURN-5): pure rules over the text of a model's reply, used while it streams and after it ends:
// cut-off replies joined at a line boundary (R-TURN-1, R-TURN-2), the re-prompt for a cut (R-TURN-3), runaway replies caught early (R-TURN-4), edits
// that grow into rewrites (R-TURN-5). Moved from Sub0Squad's test_blocks (block 1, rows 34 to 36) with the code, 2026-11-18; every check is as it was.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/turn_control.hpp"

#include <map>
#include <string>

using namespace sub0canvas;

int main()
{
    {
        // trimOverlap: the longest suffix that is a prefix, at least 8 characters, and never indentation alone.
        CHECK_EQ(trimOverlap("throw Error(\"sqrt requires", "sqrt requires 1 argument\");"), std::string(" 1 argument\");"));
        CHECK_EQ(trimOverlap("abc", "abcdef"), std::string("abcdef"));                              // 3 characters: too short to be evidence
        CHECK_EQ(trimOverlap("    x = 1\n        ", "        y = 2\n"), std::string("        y = 2\n"));   // eight spaces are not a repeat
        CHECK_EQ(trimOverlap("return value;", "nothing in common"), std::string("nothing in common"));
        CHECK_EQ(trimOverlap("", "text"), std::string("text"));
        // continuationText: a fence and a repeated header are not content.
        CHECK_EQ(continuationText("int a = 1;\nint b", "```\n = 2;\nint c = 3;\n```\n"), std::string(" = 2;\nint c = 3;\n"));
        CHECK_EQ(continuationText("=== FILE: a.cpp ===\nint a;\n", "=== FILE: a.cpp ===\nint b;\n"), std::string("int b;\n"));
        // joinContinuation: every seam class of the 18 recorded cut-offs of the 2026-11-18 gate (35B; out/runs/t0-101e19c), from their own lines.
        {
            const std::string head = "=== FILE: a.cpp ===\nint f(int a)\n{\n";
            // The cut line written again in full, as asked: joined at the line boundary.
            CHECK_EQ(joinContinuation(head + "    if (a > 0)\n    return std", "    return std::max(a, 0);\n}\n=== END FILE ===\n"),
                     head + "    if (a > 0)\n    return std::max(a, 0);\n}\n=== END FILE ===\n");
            // A stray `===` and a fence, then two lines before the cut again, then the cut line in full (seam 08).
            CHECK_EQ(joinContinuation(head + "    int m = std::min(a, 1);\n    if (a > 9) {\n        return std",
                                      "===\n```cpp\n    int m = std::min(a, 1);\n    if (a > 9) {\n        return std::max(a, 0);\n    }\n```\n"),
                     head + "    int m = std::min(a, 1);\n    if (a > 9) {\n        return std::max(a, 0);\n    }\n");
            // Continued from the cut, the token's space lost: after a keyword (seam 04), before a comparison (seams 06, 09); a word split is not touched (seam 16).
            CHECK_EQ(joinContinuation(head + "    return", "std::min(a, 1);\n"), head + "    return std::min(a, 1);\n");
            CHECK_EQ(joinContinuation(head + "    } else if (name", "== \"pow\") {\n"), head + "    } else if (name == \"pow\") {\n");
            CHECK_EQ(joinContinuation(head + "    return std::log", "2(a);\n"), head + "    return std::log2(a);\n");
            CHECK_EQ(joinContinuation(head + "    if (a[0] <= 0) fail", "(\"log of non-positive\");\n"), head + "    if (a[0] <= 0) fail(\"log of non-positive\");\n");   // seam 01
            // The rest as a SEARCH/REPLACE of the block's own tail (seams 02, 05): spliced.
            CHECK_EQ(joinContinuation(head + "    std::string s_;\n    bool",
                                      "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n    std::string s_;\n    bool\n=======\n    std::string s_;\n    bool d_;\n>>>>>>> REPLACE\n=== END EDIT ===\n};\n"),
                     head + "    std::string s_;\n    bool d_;\n};\n");
            // ... its last line's indentation lost (seen from the 35B in wrong-approach, 2026-11-18).
            CHECK_EQ(joinContinuation(head + "    int r = g(a);\n    if (r",
                                      "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n    int r = g(a);\nif (r\n=======\n    int r = g(a);\n    if (r > 0) return r;\n>>>>>>> REPLACE\n}\n"),
                     head + "    int r = g(a);\n    if (r > 0) return r;\n}\n");
            // A block of its own replaces the partial: read requests (seam 12), the file begun again from its first line (seam 13).
            CHECK_EQ(joinContinuation(head + "    int x", "=== READ: a.cpp SYMBOL f ===\n"), std::string("=== READ: a.cpp SYMBOL f ===\n"));
            CHECK_EQ(joinContinuation(head + "    int x", head + "    return a;\n}\n"), head + "    return a;\n}\n");
            // A different block instead of the rest (T0 write-engine): the open block ends at its last complete line, the new one follows whole.
            CHECK_EQ(joinContinuation(head + "    int v = a;\n    if (v < 0", "=== EDIT: a.cpp SYMBOL g ===\nint g() { return 1; }\n=== END EDIT ===\n"),
                     head + "    int v = a;\n=== END FILE ===\n=== EDIT: a.cpp SYMBOL g ===\nint g() { return 1; }\n=== END EDIT ===\n");
            // Cut again mid-line: the continuation's own unfinished line stays unfinished for the next join.
            CHECK_EQ(joinContinuation(head + "    int x = 1;\n", "    int y = 2;\n    int z"), head + "    int x = 1;\n    int y = 2;\n    int z");
            // The prompt: whole lines only, the unfinished line named.
            const std::string p = continuationPrompt(head + "    int x = 1;\n    return std");
            CHECK(p.find("    int x = 1;\n```") != std::string::npos && p.find("the unfinished line `return std`") != std::string::npos && p.find("write it again in full") != std::string::npos);
            CHECK(continuationPrompt(head + "    int x = 1;\n").find("Continue with the line that follows them") != std::string::npos);
        }
        // detectEditOverrun: a SEARCH of most of a file that exists (the file replaced in disguise), one edit past the limit; a stub or new file is not judged.
        {
            std::string file, many;
            for (int i = 0; i < 12; ++i) file += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
            for (int i = 0; i < 101; ++i) many += "    x += " + std::to_string(i) + ";\n";
            const std::map<std::string, std::string> locked{{"a.cpp", file}};
            CHECK_EQ(detectEditOverrun("=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n" + file, locked, 100, 0.8, 10).kind, std::string("rewrite-search"));
            CHECK(!detectEditOverrun("=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n" + file, locked));   // 12 lines, under the default 60: one function is most of it
            CHECK(!detectEditOverrun("=== EDIT: a.cpp ===\n<<<<<<< SEARCH\nint v1 = 1;\nint v2 = 2;\n=======\nint v1 = 2;\n", locked));
            CHECK_EQ(detectEditOverrun("=== EDIT: a.cpp SYMBOL f ===\nvoid f()\n{\n" + many, locked).kind, std::string("big-edit"));
            CHECK(!detectEditOverrun("=== EDIT: b.cpp SYMBOL f ===\nvoid f()\n{\n" + many, locked));   // not locked: a stub or a new file
            CHECK(!detectEditOverrun("=== EDIT: a.cpp SYMBOL f ===\nvoid f()\n{\n" + many.substr(0, many.size() / 2), locked));
            // Reasoning written as comments: 25 in a row stop the reply; preprocessor lines are not comments.
            std::string ramble = "=== FILE: a.cpp ===\n", code = "=== FILE: a.py ===\n";
            for (int i = 0; i < 25; ++i) { ramble += "        // Wait, what about case " + std::to_string(i) + "?\n"; code += "#include <x" + std::to_string(i) + ">\n"; }
            CHECK_EQ(detectRunaway(ramble, {}).kind, std::string("comment-run"));
            CHECK(!detectRunaway(code, {}));
        }
        // Loops (the 35B's write-tests reply: the same 8 tests, 32 lines, 18 times): a long cycle three times over, or a definition a third time in one file.
        {
            std::string cycle, file = "=== FILE: t.cpp ===\n";
            for (int i = 0; i < 8; ++i) cycle += "TEST(error_" + std::to_string(i) + ") {\n    CHECK_THROWS(ev(\"" + std::to_string(i) + "/0\"));\n}\n\n";
            CHECK_EQ(detectRunaway(file + cycle + cycle + cycle, {}).kind, std::string("repeat"));
            CHECK(detectRunaway(file + cycle + cycle, {}).instruction.empty());   // twice is not yet a loop
            const std::string again = "TEST(error_ln_of_negative) {\n    CHECK_THROWS(ev(\"ln(-1)\"));\n}\n";
            const Runaway third = detectRunaway(file + again + cycle + again + "TEST(x) {\n}\n" + again, {});
            CHECK(third.kind == "repeat" && third.instruction.find("written a third time") != std::string::npos && third.instruction.find("different angle") != std::string::npos);
            // Not loops: `namespace {` and closing braces recur; distinct functions with similar bodies.
            std::string ok = file;
            for (int i = 0; i < 4; ++i) ok += "namespace {\nint f" + std::to_string(i) + "(int a) {\n    return a + " + std::to_string(i) + ";\n}\n}\n";
            CHECK(!detectRunaway(ok, {}));
            // Nor an edit of a head the file holds: SEARCH and REPLACE repeat it (a rolled-back FILE, then an EDIT of its enum: 35B, 2026-11-18).
            const std::string head = "enum class TokType {\n    Number,\n    Plus,\n};\n";
            CHECK(!detectRunaway(file + head + "int f(int a) {\n    return a;\n}\n=== EDIT: t.cpp ===\n<<<<<<< SEARCH\n" + head + "=======\nenum class TokType {\n", {}));
        }
        // A FILE cut part-way is made whole from the previous revision: back to its last complete definition, then the functions it had not reached.
        {
            const std::string prev = "#include <x>\n\nint a() {\n    return 1;\n}\n\nint b() {\n    return 2;\n}\n\nint c() {\n    return 3;\n}\n";
            const std::string cut = "#include <x>\nenum class T { A, B };\n\nint a() {\n    return 10;   // \"}\" in a comment\n}\n\nint b() {\n    // I will rethink this\n    // I will rethink this\n";
            CHECK_EQ(completeCutFile(cut, prev, false),
                     std::string("#include <x>\nenum class T { A, B };\n\nint a() {\n    return 10;   // \"}\" in a comment\n}\n\nint b() {\n    return 2;\n}\n\nint c() {\n    return 3;\n}\n"));
            const std::string py = "class E:\n    def a(self):\n        return 1\n\n    def b(self):\n        return 2\n\n    def c(self):\n        return 3\n";
            CHECK_EQ(completeCutFile("class E:\n    def a(self):\n        return 10\n\n    def b(self):\n        # rethink\n", py, true),
                     std::string("class E:\n    def a(self):\n        return 10\n\n\n    def b(self):\n        return 2\n\n    def c(self):\n        return 3\n"));
        }
        // An EDIT of another part of the file after a cut FILE is a block of its own, not the FILE's tail (its SEARCH does not match the cut's last lines).
        CHECK_EQ(joinContinuation("=== FILE: a.cpp ===\nint a() {\n    return 1;\n}\nint b() {\n", "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\nint a() {\n=======\nint a(int) {\n>>>>>>> REPLACE\n=== END EDIT ===\n"),
                 std::string("=== FILE: a.cpp ===\nint a() {\n    return 1;\n}\nint b() {\n=== END FILE ===\n=== EDIT: a.cpp ===\n<<<<<<< SEARCH\nint a() {\n=======\nint a(int) {\n>>>>>>> REPLACE\n=== END EDIT ===\n"));
        CHECK(cutOffInsideEdit("=== EDIT: a.cpp LINES 3 ===\nint x"));
        CHECK(!cutOffInsideEdit("=== EDIT: a.cpp LINES 3 ===\nint x;\n=== END EDIT ===\n"));
        CHECK(!cutOffInsideEdit("=== FILE: a.cpp ===\nint x"));

        // detectRunaway: a long SEARCH block, a repeated line, a restated draft, an echoed view; a normal reply is left alone.
        std::string draft, search = "=== EDIT: a.cpp ===\n<<<<<<< SEARCH\n";
        for (int i = 1; i <= 80; ++i) draft += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        for (int i = 1; i <= 40; ++i) search += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        CHECK(!detectRunaway(search, {{"a.cpp", draft}}));                                         // 40 lines: at the limit
        CHECK_EQ(detectRunaway(search + "int v41 = 41;\n", {{"a.cpp", draft}}).kind, std::string("long-search"));
        CHECK(!detectRunaway(search + "int v41 = 41", {{"a.cpp", draft}}));                        // the 41st line is not complete yet: no verdict on a partial line
        CHECK(detectRunaway(search + "int v41 = 41;\n", {{"a.cpp", draft}}).instruction.find("LINES a-b") != std::string::npos);
        CHECK(!detectRunaway(search + "=======\nint v1 = 0;\n>>>>>>> REPLACE\n" + search, {{"a.cpp", draft}}));   // two blocks of 40 are two blocks
        std::string repeat = "=== EDIT: a.cpp AFTER 2 ===\n";
        for (int i = 0; i < 9; ++i) repeat += "    total += value;\n";
        CHECK(!detectRunaway(repeat, {}));
        CHECK_EQ(detectRunaway(repeat + "    total += value;\n", {}).kind, std::string("repeat"));
        std::string braces = "=== FILE: a.cpp ===\n";
        for (int i = 0; i < 40; ++i) braces += "}\n";
        CHECK(!detectRunaway(braces, {}));                                                          // short lines (a brace) repeat legitimately
        std::string cycle = "=== FILE: a.cpp ===\n";
        for (int i = 0; i < 6; ++i) cycle += "if (x) {\n    doTheThing();\n}\n";
        CHECK_EQ(detectRunaway(cycle, {}).kind, std::string("repeat"));                             // a 3-line block over and over
        CHECK_EQ(detectRunaway("=== FILE: a.cpp ===\n" + draft.substr(0, draft.find("int v31")), {{"a.cpp", draft}}).kind, std::string("restate"));
        CHECK(!detectRunaway("=== FILE: a.cpp ===\n" + draft.substr(0, draft.find("int v31")), {}));                    // no draft: a first whole file is not a restatement
        CHECK(!detectRunaway("=== FILE: b.cpp ===\n" + draft.substr(0, draft.find("int v31")), {{"a.cpp", draft}}));    // another file
        std::string rewritten = "=== FILE: a.cpp ===\n";
        for (int i = 1; i <= 40; ++i) rewritten += "long w" + std::to_string(i) + " = 0;\n";
        CHECK(!detectRunaway(rewritten, {{"a.cpp", draft}}));                                       // a genuine rewrite is allowed
        std::string echo = "Here is the file:\n";
        for (int i = 1; i <= 30; ++i) echo += "  " + std::to_string(i) + "| int v" + std::to_string(i) + ";\n";
        CHECK_EQ(detectRunaway(echo, {}).kind, std::string("view-echo"));
        CHECK(!detectRunaway("=== EDIT: a.cpp LINES 5 ===\nint v5 = 50;\n=== END EDIT ===\n", {{"a.cpp", draft}}));
        RunawayLimits off;
        off.maxSearchLines = 0;
        CHECK(!detectRunaway(search + "int v41 = 41;\nint v42 = 42;\n", {}, off));                  // a limit of 0 turns its rule off

        // R-TURN-6: a loop names what repeated in `detail`, for a message of the caller's own; other runaways have none.
        const auto looping = detectRunaway(repeat + "    total += value;\n", {});
        CHECK(!looping.detail.empty());
        CHECK(detectRunaway(echo, {}).detail.empty());
        // R-TURN-7: endsInsideBlock: a half-written function is open, a closed one is not; braces in strings, characters and comments are ignored.
        CHECK(endsInsideBlock("int a() {\n    if (x) {\n        return 1;\n"));
        CHECK(!endsInsideBlock("int a() {\n    return 1;\n}\n"));
        CHECK(!endsInsideBlock("int a() {\n    return 1;   // {{{\n}\n"));
        CHECK(!endsInsideBlock("const char* s = \"{\";\nchar c = '{';\nchar q = '\'';\n"));
        CHECK(!endsInsideBlock("/* { open\n   still { in the comment */\nint a;\n"));
        CHECK(endsInsideBlock("int a() { /* } */\n    return 1;\n"));
        CHECK(endsInsideBlock("int a() {\n    return 1;"));                 // an unfinished last line counts
        CHECK(endsInsideBlock("int a() {"));
        CHECK(!endsInsideBlock("int a() { return 1; }"));
        CHECK(!endsInsideBlock(""));
    }
    return finish();
}
