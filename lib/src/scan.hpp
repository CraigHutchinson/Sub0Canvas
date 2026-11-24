#pragma once
// Text scanning shared by the library's sources (internal, not installed).

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas::scan {

/// The same lines as `splitLines`, as views into `text` (which must outlive them): no copy, no stream, one memchr per line. A CR stays; a final
/// newline gives no empty last line; an empty text gives no line.
inline std::vector<std::string_view> splitViews(std::string_view text)
{
    std::vector<std::string_view> lines;
    lines.reserve(text.size() / 32 + 1);
    std::size_t b = 0;
    while (b < text.size()) {
        const void* nl = std::memchr(text.data() + b, '\n', text.size() - b);
        const std::size_t e = nl ? static_cast<std::size_t>(static_cast<const char*>(nl) - text.data()) : text.size();
        lines.push_back(text.substr(b, e - b));
        b = e + 1;
    }
    return lines;
}

/// The lines of `text` without their '\n' (a CR stays; a final newline gives no empty last line).
inline std::vector<std::string> splitLines(const std::string& text)
{
    const auto views = splitViews(text);
    std::vector<std::string> lines;
    lines.reserve(views.size());
    for (const auto v : views) lines.emplace_back(v);
    return lines;
}

/// The identifier (with `::`, `~`, `$`) just before the first '(' of a line; "" when there is none.
inline std::string nameBefore(std::string_view line)
{
    const auto paren = line.find('(');
    if (paren == std::string::npos) return {};
    std::size_t e = paren;
    while (e > 0 && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
    std::size_t b = e;
    while (b > 0 && (std::isalnum(static_cast<unsigned char>(line[b - 1])) || line[b - 1] == '_' || line[b - 1] == ':' || line[b - 1] == '~' || line[b - 1] == '$')) --b;
    return std::string(line.substr(b, e - b));
}

}  // namespace sub0canvas::scan
