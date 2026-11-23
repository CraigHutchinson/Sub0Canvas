#pragma once
// Text scanning shared by the library's sources (internal, not installed).

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace sub0canvas::scan {

/// The lines of `text` without their '\n' (a '\r' stays; a final newline gives no empty last line).
inline std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

/// The identifier (with `::`, `~`, `$`) just before the first '(' of a line; "" when there is none.
inline std::string nameBefore(const std::string& line)
{
    const auto paren = line.find('(');
    if (paren == std::string::npos) return {};
    std::size_t e = paren;
    while (e > 0 && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
    std::size_t b = e;
    while (b > 0 && (std::isalnum(static_cast<unsigned char>(line[b - 1])) || line[b - 1] == '_' || line[b - 1] == ':' || line[b - 1] == '~' || line[b - 1] == '$')) --b;
    return line.substr(b, e - b);
}

}  // namespace sub0canvas::scan
