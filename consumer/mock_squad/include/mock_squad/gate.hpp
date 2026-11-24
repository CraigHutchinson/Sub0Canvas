#pragma once
/// @file gate.hpp
/// The mock squad's gate (docs/MOCK_SQUAD.md section 1): a FUNCTION OF THE CURRENT FILES, never a scripted verdict. A model that fixes the code makes the
/// gate pass and one that does not leaves it failing, so a scenario's outcome follows from what the replies did to the files. It writes the verdict in the
/// squad's text formats (`GATE FAIL: n of m tests failed`, `FAIL 1 - name: message`, `path:line:col: error: message`), which `parseIssues` reads.
///
/// Rules, one per line of a scenario's `--- gate` section:
///   `test <name> passes when <path> contains <text>`       a test: passes while the file holds the text (`lacks`: while it does not)
///   `compile <message> when <path> contains <text>`        a compile error: reported at the first line holding the text, for as long as it is there
///   `compile <message> when <path> contains <text> and lacks <cure>`   the same, until the file holds `cure` too (a declaration added elsewhere, as in an undeclared name)
///   `flap=<seed> <name>`                                   a flapping test (B15): it fails on the runs where (run number + seed) is even and passes on the others, whatever
///                                                          the files hold: the same name and message each time it fails, so it is one item that comes and goes

#include <expected>
#include <map>
#include <string>
#include <vector>

namespace mock_squad {

/// One gate rule (a test or a compile check).
struct GateRule
{
    enum class Kind { test, compile, flap };
    Kind kind = Kind::test;
    std::string name;        ///< a test's name; a compile error's message
    std::string path, text;
    std::string unless;      ///< compile: the error stands only while the file does NOT hold this text (empty: no cure needed beyond removing `text`)
    bool contains = true;    ///< test: passes when the text is present (true) or absent (false); compile: the error stands while the text is present
    int seed = 0;            ///< flap: see the rule above
};

/// What the test run says: `text` is gate.txt (the first six failures), `failures` is every failure (failures.txt; empty when none).
struct GateVerdict
{
    bool pass = false;
    int failing = 0, total = 0;
    std::string text, failures;
};

/// Read the rules of a `--- gate` section, or say which line is not a rule.
[[nodiscard]] std::expected<std::vector<GateRule>, std::string> parseGate(const std::vector<std::string>& lines);

/// The compile check over the current files: the diagnostics, empty when the files compile.
[[nodiscard]] std::string compileCheck(const std::vector<GateRule>& rules, const std::map<std::string, std::string>& files);

/// The test run over the current files. `run` counts the test runs of the attempt from 0: only a flapping test reads it.
[[nodiscard]] GateVerdict runTests(const std::vector<GateRule>& rules, const std::map<std::string, std::string>& files, int run = 0);

}  // namespace mock_squad
