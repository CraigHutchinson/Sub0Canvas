#include "mock_squad/gate.hpp"

#include <cctype>
#include <cstdlib>
#include <ranges>
#include <sstream>

namespace mock_squad {

namespace {

bool holds(const std::map<std::string, std::string>& files, const GateRule& r)
{
    const auto it = files.find(r.path);
    return it != files.end() && it->second.contains(r.text);
}

}  // namespace

std::expected<std::vector<GateRule>, std::string> parseGate(const std::vector<std::string>& lines)
{
    std::vector<GateRule> rules;
    for (const auto& line : lines) {
        GateRule r;
        const auto mid = [&](const std::string& s, const std::string& sep) { return s.find(sep); };
        if (line.starts_with("flap=")) {
            const auto space = line.find(' ');
            if (space == std::string::npos) return std::unexpected("not a gate rule (flap=<seed> <name>): " + line);
            char* end = nullptr;
            const std::string seed = line.substr(5, space - 5);
            const long n = std::strtol(seed.c_str(), &end, 10);
            if (seed.empty() || *end != '\0' || n < 0) return std::unexpected("a flap rule's seed is a number: " + line);
            r.kind = GateRule::Kind::flap;
            r.seed = static_cast<int>(n);
            r.name = line.substr(space + 1);
            if (r.name.empty()) return std::unexpected("an incomplete gate rule: " + line);
            rules.push_back(std::move(r));
            continue;
        }
        if (line.starts_with("test ")) {
            const auto passes = mid(line, " passes when ");
            if (passes == std::string::npos) return std::unexpected("not a gate rule (test <name> passes when <path> contains|lacks <text>): " + line);
            r.kind = GateRule::Kind::test;
            r.name = line.substr(5, passes - 5);
            std::istringstream rest(line.substr(passes + 13));
            std::string verb;
            rest >> r.path >> verb;
            if (verb != "contains" && verb != "lacks") return std::unexpected("a test rule needs 'contains' or 'lacks': " + line);
            r.contains = verb == "contains";
            std::getline(rest, r.text);
            r.text.erase(0, r.text.find_first_not_of(' '));
        } else if (line.starts_with("compile ")) {
            const auto when = mid(line, " when ");
            if (when == std::string::npos) return std::unexpected("not a gate rule (compile <message> when <path> contains <text>): " + line);
            r.kind = GateRule::Kind::compile;
            r.name = line.substr(8, when - 8);
            std::istringstream rest(line.substr(when + 6));
            std::string verb;
            rest >> r.path >> verb;
            if (verb != "contains") return std::unexpected("a compile rule needs 'contains': " + line);
            std::getline(rest, r.text);
            r.text.erase(0, r.text.find_first_not_of(' '));
            if (const auto lacks = r.text.find(" and lacks "); lacks != std::string::npos) {   // the cure: the error stands until the file holds this text
                r.unless = r.text.substr(lacks + 11);
                r.text.erase(lacks);
            }
        } else {
            return std::unexpected("not a gate rule: " + line);
        }
        if (r.name.empty() || r.path.empty() || r.text.empty()) return std::unexpected("an incomplete gate rule: " + line);
        rules.push_back(std::move(r));
    }
    return rules;
}

std::string compileCheck(const std::vector<GateRule>& rules, const std::map<std::string, std::string>& files)
{
    std::string out;
    for (const auto& r : rules) {
        if (r.kind != GateRule::Kind::compile) continue;
        const auto it = files.find(r.path);
        if (it == files.end()) continue;
        const std::string& text = it->second;
        const auto at = text.find(r.text);
        if (at == std::string::npos || (!r.unless.empty() && text.contains(r.unless))) continue;
        const std::size_t before = at == 0 ? std::string::npos : text.rfind('\n', at - 1);
        const std::size_t lineStart = before == std::string::npos ? 0 : before + 1;
        int line = 1;
        for (std::size_t i = 0; i < lineStart; ++i) line += text[i] == '\n';
        const auto eol = text.find('\n', at);
        const std::string src = text.substr(lineStart, eol == std::string::npos ? std::string::npos : eol - lineStart);
        const std::size_t col = at - lineStart + 1;
        const std::string number = std::to_string(line);
        out += r.path + ":" + number + ":" + std::to_string(col) + ": error: " + r.name + "\n";
        out += std::string(5 - std::min<std::size_t>(5, number.size()), ' ') + number + " | " + src + "\n";
        std::size_t width = 0;   // as GCC underlines the identifier it names: the caret, then tildes
        while (width < r.text.size() && (std::isalnum(static_cast<unsigned char>(r.text[width])) || r.text[width] == '_')) ++width;
        out += "      | " + std::string(col - 1, ' ') + "^" + std::string(width > 1 ? width - 1 : 0, '~') + "\n";
    }
    return out;
}

GateVerdict runTests(const std::vector<GateRule>& rules, const std::map<std::string, std::string>& files, int run)
{
    GateVerdict v;
    std::vector<std::string> failures;
    for (const auto& r : rules) {
        if (r.kind == GateRule::Kind::flap) {
            ++v.total;
            if ((run + r.seed) % 2 != 0) continue;
            failures.push_back("FAIL " + std::to_string(v.total) + " - " + r.name + ": CHECK failed: the result depended on timing\n");
            continue;
        }
        if (r.kind != GateRule::Kind::test) continue;
        ++v.total;
        if (holds(files, r) == r.contains) continue;
        failures.push_back("FAIL " + std::to_string(v.total) + " - " + r.name + ": CHECK failed: " + r.path + (r.contains ? " does not contain \"" : " still contains \"") + r.text + "\"\n");
    }
    v.failing = static_cast<int>(failures.size());
    v.pass = failures.empty();
    if (v.pass) {
        v.text = "GATE PASS: " + std::to_string(v.total) + " tests, 0 failing\n";
        return v;
    }
    const std::string head = "GATE FAIL: " + std::to_string(v.failing) + " of " + std::to_string(v.total) + " tests failed\n";
    v.text = head;
    v.failures = head;
    for (const auto& [i, failure] : std::views::enumerate(failures)) {
        if (i < 6) v.text += failure;   // the gate's own excerpt keeps six failures
        v.failures += failure;
    }
    return v;
}

}  // namespace mock_squad
