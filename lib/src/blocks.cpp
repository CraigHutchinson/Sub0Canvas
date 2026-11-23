#include "sub0canvas/blocks.hpp"

#include <algorithm>
#include <cctype>

#include "scan.hpp"

namespace sub0canvas {

namespace {

using scan::nameBefore;
using scan::splitLines;

/// Net brace change of a line, ignoring string and character literals and // comments (block comments are rare in generated code and not tracked).
int braceDelta(const std::string& line, bool& opensHere)
{
    int d = 0;
    char quote = 0;
    opensHere = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) { if (c == '\\') ++i; else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'' || c == '`') { quote = c; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (c == '#' && i == line.find_first_not_of(" \t")) break;   // a preprocessor line
        if (c == '{') { ++d; opensHere = true; }
        else if (c == '}') --d;
    }
    return d;
}

/// The lines with every `/* ... */` comment (across lines too) blanked to spaces, so a brace, a keyword or a signature inside one never counts (R-BLK-5). String and
/// character literals and `//` comments are read as `braceDelta` reads them (a comment opener inside one is not an opener); the line count and every column are kept.
std::vector<std::string> blankBlockComments(std::vector<std::string> lines)
{
    bool inComment = false;
    for (auto& line : lines) {
        char quote = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (inComment) {
                const bool closes = line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/';
                line[i] = ' ';
                if (closes) { line[++i] = ' '; inComment = false; }
                continue;
            }
            const char c = line[i];
            if (quote) { if (c == '\\') ++i; else if (c == quote) quote = 0; continue; }
            if (c == '"' || c == '\'' || c == '`') { quote = c; continue; }
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
            if (c == '#' && i == line.find_first_not_of(" \t")) break;   // a preprocessor line
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') { line[i] = line[i + 1] = ' '; ++i; inComment = true; }
        }
    }
    return lines;
}

bool functionLike(const std::string& line)
{
    // A signature: has '(' before its '{', and is not a control statement (those live inside a function body that is folded as a whole).
    const auto brace = line.find('{');
    const auto paren = line.find('(');
    if (paren == std::string::npos || (brace != std::string::npos && brace < paren)) return false;
    const std::string t = line.substr(line.find_first_not_of(" \t") == std::string::npos ? 0 : line.find_first_not_of(" \t"));
    for (const char* kw : {"if", "for", "while", "switch", "catch", "else", "return"})
        if (t.rfind(kw, 0) == 0 && (t.size() == std::string(kw).size() || !(std::isalnum(static_cast<unsigned char>(t[std::string(kw).size()])) || t[std::string(kw).size()] == '_'))) return false;
    return true;
}

std::string collapse(const std::string& s)
{
    std::string out;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!out.empty() && out.back() != ' ') out += ' '; }
        else out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '{')) out.pop_back();
    return out;
}

/// One function-like block as the heuristics find it, before it becomes a node.
struct Found { int sig, bodyFrom, bodyTo; std::string qualified; };

OutlineNode toNode(const Found& f, const std::vector<std::string>& lines)
{
    OutlineNode n;
    n.kind = "function";
    n.qualifiedName = f.qualified;
    const auto colon = f.qualified.rfind(':');
    n.name = colon == std::string::npos ? f.qualified : f.qualified.substr(colon + 1);
    n.first = f.sig;
    n.last = std::max(f.sig, f.bodyTo);
    n.nameLine = f.sig;
    n.bodyFrom = f.bodyFrom;
    n.bodyTo = f.bodyTo;
    n.signature = collapse(lines[static_cast<std::size_t>(f.sig) - 1]);
    return n;
}

class BracesProvider final : public OutlineProvider
{
public:
    [[nodiscard]] std::string name() const override { return "braces@2"; }
    [[nodiscard]] Outline outline(std::string_view text) const override
    {
        const auto raw = splitLines(std::string(text));
        const auto lines = blankBlockComments(raw);
        Outline out;
        out.provider = name();
        int depth = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            bool opens = false;
            const int d = braceDelta(lines[i], opens);
            // Allman style: the signature is on the line before a lone '{'.
            const auto firstNonBlank = lines[i].find_first_not_of(" \t");
            const bool loneBrace = firstNonBlank != std::string::npos && lines[i][firstNonBlank] == '{' && lines[i].find_first_not_of(" \t", firstNonBlank + 1) == std::string::npos;
            const bool allman = loneBrace && i > 0 && lines[i - 1].find('(') != std::string::npos && lines[i - 1].find('{') == std::string::npos && lines[i - 1].find(';') == std::string::npos && functionLike(lines[i - 1] + " {");
            if (opens && d > 0 && (allman || functionLike(lines[i]))) {
                const int start = depth;
                const std::size_t sig = allman ? i - 1 : i;
                int dd = depth + d;
                std::size_t j = i;
                while (dd > start && j + 1 < lines.size()) { ++j; bool o2 = false; dd += braceDelta(lines[j], o2); }
                if (dd <= start) out.nodes.push_back(toNode({static_cast<int>(sig) + 1, static_cast<int>(i) + 2, static_cast<int>(j) + 1, nameBefore(lines[sig])}, raw));   // the closing line stays visible
                else out.partial = true;
                depth = dd;
                i = j;
                continue;
            }
            if (opens && d == 0 && functionLike(lines[i]) && lines[i].find('}') != std::string::npos && lines[i].find(';') != std::string::npos && lines[i].find('{') < lines[i].rfind('}'))
                out.nodes.push_back(toNode({static_cast<int>(i) + 1, static_cast<int>(i) + 1, static_cast<int>(i) + 1, nameBefore(lines[i])}, raw));   // a function written on one line
            depth += d;
        }
        return out;
    }
};

class IndentProvider final : public OutlineProvider
{
public:
    [[nodiscard]] std::string name() const override { return "indent@1"; }
    [[nodiscard]] Outline outline(std::string_view text) const override
    {
        const auto lines = splitLines(std::string(text));
        Outline out;
        out.provider = name();
        const auto indentOf = [](const std::string& l) { return l.find_first_not_of(" \t"); };
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto ind = indentOf(lines[i]);
            if (ind == std::string::npos) continue;
            const std::string t = lines[i].substr(ind);
            if (t.rfind("def ", 0) != 0 && t.rfind("async def ", 0) != 0) continue;
            std::size_t j = i + 1, lastBody = i;
            for (; j < lines.size(); ++j) {
                const auto in2 = indentOf(lines[j]);
                if (in2 == std::string::npos) continue;   // blank line
                if (in2 <= ind) break;
                lastBody = j;
            }
            // a multi-line signature: the body starts after the line that ends the signature
            out.nodes.push_back(toNode({static_cast<int>(i) + 1, static_cast<int>(std::min(i + 1, lastBody)) + 1, static_cast<int>(lastBody) + 1, nameBefore(lines[i])}, lines));
            i = lastBody;
        }
        return out;
    }
};

bool endsWith(std::string_view s, std::string_view suffix) { return s.size() > suffix.size() && s.substr(s.size() - suffix.size()) == suffix; }

std::string lastComponent(const std::string& s) { return s.substr(s.find_last_of('/') == std::string::npos ? 0 : s.find_last_of('/') + 1); }

/// The interpreter a `#!` line names: `#!/usr/bin/env python3` and `#!/usr/bin/python` both give "python".
std::string interpreterOf(std::string_view shebang)
{
    std::string_view rest = shebang.substr(2);
    std::size_t pos = 0;
    const auto next = [&] {
        while (pos < rest.size() && std::isspace(static_cast<unsigned char>(rest[pos]))) ++pos;
        const auto b = pos;
        while (pos < rest.size() && !std::isspace(static_cast<unsigned char>(rest[pos]))) ++pos;
        return std::string(rest.substr(b, pos - b));
    };
    std::string word = lastComponent(next());
    if (word == "env") word = lastComponent(next());
    while (!word.empty() && (std::isdigit(static_cast<unsigned char>(word.back())) || word.back() == '.')) word.pop_back();   // python3.12 -> python
    return word;
}

}  // namespace

// ---- the registry -------------------------------------------------------------------------------------------------------------------------------------

void Languages::add(std::string language, std::shared_ptr<const OutlineProvider> provider, int priority)
{
    entries_.push_back({std::move(language), std::move(provider), priority});
}

void Languages::detects(const std::string& language, std::vector<std::string> extensions, std::vector<std::string> filenames, std::vector<std::string> interpreters)
{
    detects_.push_back({language, std::move(extensions), std::move(filenames), std::move(interpreters)});
}

void Languages::setFallback(std::shared_ptr<const OutlineProvider> provider) { fallback_ = std::move(provider); }

std::string Languages::detect(std::string_view path, std::string_view firstLine) const
{
    const auto slash = path.find_last_of("/\\");
    const std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    for (const auto& d : detects_)
        if (std::find(d.filenames.begin(), d.filenames.end(), file) != d.filenames.end()) return d.language;
    for (const auto& d : detects_)
        for (const auto& e : d.extensions) if (endsWith(path, e)) return d.language;
    if (firstLine.starts_with("#!")) {
        const std::string word = interpreterOf(firstLine);
        for (const auto& d : detects_)
            if (std::find(d.interpreters.begin(), d.interpreters.end(), word) != d.interpreters.end()) return d.language;
    }
    return {};
}

const OutlineProvider* Languages::provider(std::string_view language) const
{
    const Entry* best = nullptr;
    for (const auto& e : entries_)
        if (e.language == language && (!best || e.priority > best->priority)) best = &e;
    return best ? best->provider.get() : fallback_.get();
}

Outline Languages::outline(std::string_view path, std::string_view text) const
{
    const std::string language = detect(path, text.substr(0, text.find('\n')));
    const OutlineProvider* p = provider(language);
    if (!p) return {language, {}, {}, false};
    Outline o = p->outline(text);
    o.language = language;
    return o;
}

const OutlineProvider& bracesProvider() { static const BracesProvider p; return p; }
const OutlineProvider& indentProvider() { static const IndentProvider p; return p; }

const Languages& defaultLanguages()
{
    static const Languages languages = [] {
        Languages l;
        const auto borrowed = [](const OutlineProvider& p) { return std::shared_ptr<const OutlineProvider>(std::shared_ptr<void>{}, &p); };   // the providers are static
        l.detects("python", {".py"}, {}, {"python"});
        l.add("python", borrowed(indentProvider()), 0);
        l.setFallback(borrowed(bracesProvider()));
        return l;
    }();
    return languages;
}

std::vector<std::pair<std::string, std::string>> outlinersOf(const std::vector<std::string>& paths)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& path : paths) {
        const std::string language = defaultLanguages().detect(path);
        const OutlineProvider* p = defaultLanguages().provider(language);
        if (!p) continue;
        std::pair<std::string, std::string> row{language, p->name()};
        if (std::find(out.begin(), out.end(), row) == out.end()) out.push_back(row);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace sub0canvas
