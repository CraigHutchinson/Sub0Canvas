#include "mock_squad/record.hpp"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace mock_squad {

std::string jsonString(const std::string& s)
{
    std::string o = "\"";
    for (const unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o + "\"";
}

void seal(Attempt& a, const std::string& scenarioName)
{
    std::ostringstream agent;   // a subset of the squad's agent.json: no tokens, no clock, so the file is the same on every run
    agent << "{\"treatment\":\"mock\",\"engine\":\"mock-squad\",\"turns\":" << a.outcomes.size() << ",\"issueLayer\":true,\"selfcheck\":" << (a.verified ? "true" : "false")
          << ",\"canvas\":{\"commits\":" << a.commits << ",\"refused\":" << a.refused << ",\"reads\":" << a.reads << ",\"aborted\":" << a.aborted << ",\"continued\":" << a.continued
          << ",\"reverted\":" << a.reverted << ",\"staged\":" << a.staged << "}}\n";
    a.record["agent.json"] = agent.str();
    std::ostringstream trace;
    trace << "{\"scenario\":" << jsonString(scenarioName) << ",\"verified\":" << (a.verified ? "true" : "false") << ",\"start\":" << jsonString(a.startVerdict) << ",\"outcomes\":[";
    for (std::size_t i = 0; i < a.outcomes.size(); ++i) {
        const Outcome& o = a.outcomes[i];
        trace << (i ? ",\n" : "\n") << "{\"call\":" << o.call << ",\"actor\":" << jsonString(o.actor) << ",\"kind\":" << jsonString(o.kind) << ",\"text\":" << jsonString(o.text)
              << ",\"verdict\":" << jsonString(o.verdict) << ",\"focus\":" << jsonString(o.focus) << "}";
    }
    trace << "\n],\"libraryCalls\":[";
    bool first = true;
    for (const auto& c : a.libraryCalls) {
        trace << (first ? "" : ",") << jsonString(c);
        first = false;
    }
    trace << "]}\n";
    a.record["mock-trace.json"] = trace.str();
}

void writeTree(const fs::path& dir, const std::map<std::string, std::string>& files)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (const auto& [rel, bytes] : files) {
        const fs::path p = dir / rel;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary);
        out << bytes;
    }
}

std::map<std::string, std::string> readTree(const fs::path& dir)
{
    std::map<std::string, std::string> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        files[fs::relative(e.path(), dir).generic_string()] = s.str();
    }
    return files;
}

}  // namespace mock_squad
