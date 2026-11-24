#include "sub0canvas/outline.hpp"

#include "scan.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sub0canvas {

namespace {

using scan::nameBefore;
using scan::splitLines;

/// The last component of a qualified name (`Parser::term` -> `term`).
std::string leafName(const std::string& name) { const auto c = name.rfind(':'); return c == std::string::npos ? name : name.substr(c + 1); }

/// Bodies of at most this many lines are never folded.
constexpr int kShowBodyLines = 2;

/// The numbered view with every region folded to one line except those marked open.
std::string renderFolded(const std::vector<std::string>& lines, const std::vector<FoldRegion>& regions, const std::vector<bool>& open, bool python)
{
    const auto row = [&](std::size_t i) { char n[16]; std::snprintf(n, sizeof n, "%4zu| ", i + 1); return std::string(n) + lines[i] + "\n"; };
    std::string out;
    std::size_t r = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const int ln = static_cast<int>(i) + 1;
        while (r < regions.size() && regions[r].bodyTo < ln) ++r;
        if (r < regions.size() && !open[r] && ln >= regions[r].bodyFrom && ln <= regions[r].bodyTo - (python ? 0 : 1)) {
            if (ln == regions[r].bodyFrom) {
                const int hidden = regions[r].bodyTo - regions[r].bodyFrom + (python ? 1 : 0);
                out += "      ... (" + std::to_string(hidden) + " lines folded: " + std::to_string(regions[r].bodyFrom) + "-" + std::to_string(regions[r].bodyTo - (python ? 0 : 1)) + (regions[r].name.empty() ? "" : "; " + regions[r].name) + ")\n";
            }
            continue;
        }
        out += row(i);
    }
    return out;
}

}  // namespace

namespace {

/// The parameter names of a C++ signature (`double f(const std::string& s, int n = 2)` -> s, n): the last identifier of each top-level parameter, with
/// defaults dropped. A parameter with no name, `void`, or a declarator this cannot read contributes nothing.
std::vector<std::string> parameterNames(const std::string& signature)
{
    std::vector<std::string> names;
    const auto open = signature.find('(');
    if (open == std::string::npos) return names;
    int depth = 0;
    std::string cur;
    std::vector<std::string> params;
    for (std::size_t i = open + 1; i < signature.size(); ++i) {
        const char c = signature[i];
        if (c == '(' || c == '<' || c == '[' || c == '{') ++depth;
        else if (c == '>' || c == ']' || c == '}') --depth;
        else if (c == ')') { if (depth == 0) break; --depth; }
        if (c == ',' && depth == 0) { params.push_back(cur); cur.clear(); continue; }
        cur.push_back(c);
    }
    params.push_back(cur);
    for (std::string p : params) {
        if (const auto eq = p.find('='); eq != std::string::npos) p.erase(eq);
        while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.pop_back();
        std::size_t e = p.size(), b = e;
        while (b > 0 && (std::isalnum(static_cast<unsigned char>(p[b - 1])) || p[b - 1] == '_')) --b;
        const std::string name = p.substr(b, e - b);
        // a name is preceded by a type: something must come before it, and it must not itself be a type keyword standing alone
        const bool hasType = p.find_first_not_of(" \t") < b;
        static const char* const notNames[] = {"void", "int", "double", "float", "bool", "char", "long", "short", "unsigned", "auto", "const", "size_t"};
        bool keyword = false;
        for (const char* k : notNames) if (name == k) keyword = true;
        if (!name.empty() && hasType && !keyword && !std::isdigit(static_cast<unsigned char>(name[0]))) names.push_back(name);
    }
    return names;
}

}  // namespace

Skeleton expandSkeleton(const std::string& declared, const std::string& path)
{
    Skeleton out;
    const bool python = isPythonPath(path);
    const auto ext = path.rfind('.') == std::string::npos ? std::string{} : path.substr(path.rfind('.'));
    const bool script = ext == ".js" || ext == ".mjs" || ext == ".ts";
    const bool cpp = !python && !script;
    const auto lines = splitLines(declared);
    struct Decl { std::size_t line; std::string indent, signature, name; };
    std::vector<Decl> decls;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto first = lines[i].find_first_not_of(" \t");
        if (first == std::string::npos || lines[i].compare(first, 2, "@@") != 0) continue;
        Decl d;
        d.line = i;
        d.indent = lines[i].substr(0, first);
        d.signature = lines[i].substr(first + 2);
        d.signature.erase(0, d.signature.find_first_not_of(" \t"));
        while (!d.signature.empty() && (std::isspace(static_cast<unsigned char>(d.signature.back())) || d.signature.back() == '{' || d.signature.back() == ';' || (python && d.signature.back() == ':'))) d.signature.pop_back();
        d.name = nameBefore(d.signature);
        // A member defined outside its class keeps its class (`Parser::parse`): a file may hold `Parser::evaluate` and a free `evaluate` (catalogue row 71).
        if (python || script) { if (const auto c = d.name.rfind(':'); c != std::string::npos) d.name = d.name.substr(c + 1); }
        if (d.name.empty()) { out.error = "line " + std::to_string(i + 1) + " is not a function signature (`@@ " + d.signature.substr(0, 60) + "`): write `@@` then the signature with its parameter list, on one line"; return out; }
        for (const auto& e : decls) if (e.name == d.name && e.indent == d.indent) { out.error = "the function `" + d.name + "` is declared twice (lines " + std::to_string(e.line + 1) + " and " + std::to_string(i + 1) + ")"; return out; }
        decls.push_back(std::move(d));
    }
    // C++ file-scope functions get prototypes ahead of the first of them, so sections can call each other whatever their order.
    std::string prototypes;
    std::size_t protoAt = lines.size();
    if (cpp)
        for (const auto& d : decls) {
            if (!d.indent.empty() || nameBefore(d.signature).find(':') != std::string::npos || d.name == "main") continue;
            if (protoAt == lines.size()) protoAt = d.line;
            prototypes += d.signature + ";\n";
        }
    bool needStdexcept = cpp && !decls.empty() && declared.find("<stdexcept>") == std::string::npos;
    std::size_t lastInclude = lines.size();
    if (needStdexcept) for (std::size_t i = 0; i < lines.size(); ++i) if (lines[i].rfind("#include", 0) == 0) lastInclude = i;
    if (needStdexcept && lastInclude == lines.size()) { out.text += "#include <stdexcept>\n"; needStdexcept = false; }
    std::size_t next = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == protoAt && !prototypes.empty()) out.text += prototypes + "\n";
        if (next < decls.size() && decls[next].line == i) {
            const Decl& d = decls[next++];
            if (python) out.text += d.indent + d.signature + ":\n" + d.indent + "    raise NotImplementedError(\"not implemented: " + d.name + "\")\n";
            else if (script) out.text += d.indent + d.signature + " {\n" + d.indent + "    throw new Error('not implemented: " + d.name + "');\n" + d.indent + "}\n";
            else {
                // The placeholder is clean under the strictest warnings: every named parameter is used once, and it leaves by throwing on every path.
                out.text += d.indent + d.signature + "\n" + d.indent + "{\n";
                for (const auto& p : parameterNames(d.signature)) out.text += d.indent + "    (void)" + p + ";\n";
                out.text += d.indent + "    throw std::logic_error(\"not implemented: " + d.name + "\");\n" + d.indent + "}\n";
            }
            out.sections.push_back({d.name, d.signature});
            continue;
        }
        out.text += lines[i] + "\n";
        if (needStdexcept && i == lastInclude) out.text += "#include <stdexcept>\n";
    }
    return out;
}

bool isPythonPath(const std::string& path) { return path.size() > 3 && path.compare(path.size() - 3, 3, ".py") == 0; }

std::vector<FoldRegion> foldRegionsOf(const Outline& outline, bool all)
{
    // The fold policy over the outline (BLOCK_DISCOVERY.md 4.7): a body of two lines or more is worth folding; `all` keeps every node, which is what
    // addressing a function by name needs. A node's body is empty when bodyFrom == bodyTo (a one-line function, or an indentation block with no more than one body line).
    std::vector<FoldRegion> out;
    for (const auto& n : outline.nodes)
        if (all || n.bodyTo > n.bodyFrom) out.push_back({n.nameLine, n.bodyFrom, n.bodyTo, n.qualifiedName, n.first});
    return out;
}

namespace {

/// `foldRegionsOf` for an outline nobody else keeps: the names move into the regions instead of being copied.
std::vector<FoldRegion> takeRegions(Outline&& outline, bool all)
{
    std::vector<FoldRegion> out;
    out.reserve(outline.nodes.size());
    for (auto& n : outline.nodes)
        if (all || n.bodyTo > n.bodyFrom) out.push_back({n.nameLine, n.bodyFrom, n.bodyTo, std::move(n.qualifiedName), n.first});
    return out;
}

}  // namespace

std::vector<FoldRegion> foldRegionsFor(const std::string& path, const std::string& text, bool all)
{
    return takeRegions(defaultLanguages().outline(path, text), all);
}

std::vector<FoldRegion> foldRegions(const std::string& text, bool python, bool all)
{
    return takeRegions((python ? indentProvider() : bracesProvider()).outline(text), all);
}

std::string foldedView(const std::string& text, bool python, const std::vector<int>& focus, std::size_t budget)
{
    const auto lines = splitLines(text);
    const auto row = [&](std::size_t i) { char n[16]; std::snprintf(n, sizeof n, "%4zu| ", i + 1); return std::string(n) + lines[i] + "\n"; };
    std::string full;
    for (std::size_t i = 0; i < lines.size(); ++i) full += row(i);
    if (full.size() <= budget) return full;
    auto regions = foldRegions(text, python);
    // Regions holding a focus line stay open (nearest to the failure first) while the budget allows.
    std::vector<bool> open(regions.size(), false);
    std::size_t used = 0;
    {   // the size of the view with everything folded
        std::vector<bool> folded(lines.size(), false);
        for (const auto& r : regions) for (int k = r.bodyFrom; k <= r.bodyTo - (python ? 0 : 1); ++k) folded[static_cast<std::size_t>(k) - 1] = true;
        for (std::size_t i = 0; i < lines.size(); ++i) used += folded[i] ? 0 : row(i).size();
        used += regions.size() * 60;
    }
    // A body of one or two lines is shown, before any focus: folding it saves at most one line and hides the very line a short test or function is
    // (tier 0 write-tests: a 365-line test file with every one-line TEST body folded, the failing tests unreadable, two empty replies an attempt).
    for (std::size_t r = 0; r < regions.size(); ++r) {
        if (regions[r].bodyTo - regions[r].bodyFrom + (python ? 1 : 0) > kShowBodyLines) continue;
        std::size_t cost = 0;
        for (int k = regions[r].bodyFrom; k <= regions[r].bodyTo - (python ? 0 : 1); ++k) cost += row(static_cast<std::size_t>(k) - 1).size();
        open[r] = true;
        used += cost;
    }
    for (std::size_t r = 0; r < regions.size(); ++r) {
        if (open[r]) continue;
        bool has = false;
        for (int f : focus) if (f >= regions[r].first && f <= regions[r].bodyTo + 1) has = true;
        if (!has) continue;
        std::size_t cost = 0;
        for (int k = regions[r].bodyFrom; k <= regions[r].bodyTo; ++k) cost += row(static_cast<std::size_t>(k) - 1).size();
        if (used + cost <= budget) { open[r] = true; used += cost; }
    }
    return renderFolded(lines, regions, open, python) + "(folded regions hide their lines: ask for them with  === READ: <path> LINES a-b ===  or  === READ: <path> SYMBOL name ===  )\n";
}

std::string sectionView(const std::string& text, bool python, const std::vector<std::string>& openSymbols)
{
    const auto lines = splitLines(text);
    const auto regions = foldRegions(text, python);
    std::vector<bool> open(regions.size(), false);
    for (std::size_t r = 0; r < regions.size(); ++r)
        for (const auto& s : openSymbols) if (regions[r].name == s || leafName(regions[r].name) == s) open[r] = true;
    return renderFolded(lines, regions, open, python);
}

std::vector<ReadRequest> parseReadRequests(const std::string& reply)
{
    std::vector<ReadRequest> out;
    std::size_t pos = 0;
    while ((pos = reply.find("=== READ:", pos)) != std::string::npos) {
        const auto eol = reply.find('\n', pos);
        const std::string head = reply.substr(pos + 9, (eol == std::string::npos ? reply.size() : eol) - pos - 9);
        pos += 9;
        std::string h = head;
        const auto close = h.rfind("===");
        if (close != std::string::npos) h.erase(close);
        std::istringstream in(h);
        ReadRequest r;
        std::string kind;
        in >> r.path >> kind;
        if (r.path.empty()) continue;
        if (kind == "LINES" || kind == "LINE") {
            std::string spec;
            in >> spec;
            r.from = std::atoi(spec.c_str());
            const auto dash = spec.find('-');
            r.to = dash == std::string::npos ? r.from : std::atoi(spec.c_str() + dash + 1);
        } else if (kind == "SYMBOL") in >> r.symbol;
        else if (!kind.empty()) continue;
        out.push_back(r);   // a path alone (no LINES, no SYMBOL) asks for the outline, never the whole file (catalogue row 36)
    }
    return out;
}

std::string answerRead(const ReadRequest& r, const std::string& text, bool python, int maxLines)
{
    const auto lines = splitLines(text);
    int from = r.from, to = r.to;
    if (r.symbol.empty() && from == 0 && to == 0)
        return "=== " + r.path + " outline (" + std::to_string(lines.size()) + " lines; bodies are folded: name LINES a-b or SYMBOL name to read one) ===\n" + sectionView(text, python, {});
    if (!r.symbol.empty()) {
        bool found = false;
        for (const auto& f : foldRegions(text, python, true)) {
            const std::string n = f.name.substr(f.name.rfind(':') == std::string::npos ? 0 : f.name.rfind(':') + 1);
            if (f.name == r.symbol || n == r.symbol) { from = f.first; to = f.bodyTo + (python ? 0 : 1); found = true; break; }
        }
        if (!found) return "=== " + r.path + ": no function named '" + r.symbol + "' (use LINES a-b) ===\n";
    }
    if (from < 1 || to < from || from > static_cast<int>(lines.size())) return "=== " + r.path + ": lines " + std::to_string(r.from) + "-" + std::to_string(r.to) + " do not exist (the file has " + std::to_string(lines.size()) + " lines) ===\n";
    to = std::min({to, static_cast<int>(lines.size()), from + maxLines - 1});
    std::string out = "=== " + r.path + " lines " + std::to_string(from) + "-" + std::to_string(to) + " ===\n";
    for (int k = from; k <= to; ++k) { char n[16]; std::snprintf(n, sizeof n, "%4d| ", k); out += std::string(n) + lines[static_cast<std::size_t>(k) - 1] + "\n"; }
    return out;
}

}  // namespace sub0canvas
