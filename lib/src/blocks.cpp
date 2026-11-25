#include "sub0canvas/blocks.hpp"

#include <algorithm>
#include <cctype>

#include "scan.hpp"

namespace sub0canvas {

namespace {

using scan::nameBefore;
using scan::splitViews;
using View = std::string_view;

/// The first column that is not a blank or a tab; `npos` when the line is blank.
std::size_t firstNonBlank(View line)
{
    std::size_t k = 0;
    while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
    return k == line.size() ? View::npos : k;
}

/// Net brace change of a line, ignoring string and character literals and // comments (block comments are blanked beforehand, `blankBlockComments`).
int braceDelta(View line, bool& opensHere)
{
    int d = 0;
    char quote = 0;
    opensHere = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) { if (c == '\\') ++i; else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'' || c == '`') { quote = c; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (c == '#' && i == firstNonBlank(line)) break;   // a preprocessor line
        if (c == '{') { ++d; opensHere = true; }
        else if (c == '}') --d;
    }
    return d;
}

/// `text` with every `/* ... */` comment (across lines too) blanked to spaces, so a brace, a keyword or a signature inside one never counts (R-BLK-5). String and
/// character literals and `//` comments are read as `braceDelta` reads them (a comment opener inside one is not an opener); every offset is kept, so the
/// views of `lines` (into `text`) map onto the result by offset. The caller runs this only for a text that contains "/*" at all: a text without one is scanned as it is.
std::string blankBlockComments(View text, const std::vector<View>& lines)
{
    std::string buf(text);
    bool inComment = false;
    for (const View ln : lines) {
        char* line = buf.data() + (ln.data() - text.data());
        const std::size_t n = ln.size();
        char quote = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (inComment) {
                const bool closes = line[i] == '*' && i + 1 < n && line[i + 1] == '/';
                line[i] = ' ';
                if (closes) { line[++i] = ' '; inComment = false; }
                continue;
            }
            const char c = line[i];
            if (quote) { if (c == '\\') ++i; else if (c == quote) quote = 0; continue; }
            if (c == '"' || c == '\'' || c == '`') { quote = c; continue; }
            if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
            if (c == '#' && i == firstNonBlank(View(line, n))) break;   // a preprocessor line
            if (c == '/' && i + 1 < n && line[i + 1] == '*') { line[i] = line[i + 1] = ' '; ++i; inComment = true; }
        }
    }
    return buf;
}

bool functionLike(View line)
{
    // A signature: has '(' before its '{', and is not a control statement (those live inside a function body that is folded as a whole).
    const auto brace = line.find('{');
    const auto paren = line.find('(');
    if (paren == View::npos || (brace != View::npos && brace < paren)) return false;
    const auto first = firstNonBlank(line);
    const View t = line.substr(first == View::npos ? 0 : first);
    for (const View kw : {View("if"), View("for"), View("while"), View("switch"), View("catch"), View("else"), View("return")})
        if (t.starts_with(kw) && (t.size() == kw.size() || !(std::isalnum(static_cast<unsigned char>(t[kw.size()])) || t[kw.size()] == '_'))) return false;
    return true;
}

std::string collapse(View s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!out.empty() && out.back() != ' ') out += ' '; }
        else out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '{')) out.pop_back();
    return out;
}

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool nameChar(char c) { return identChar(c) || c == ':' || c == '~'; }   // what `nameBefore` reads: identifiers and their `Class::` qualifiers

/// The text of `s[0, end)` read backwards as a name (`nameChar`), skipping spaces before `end` first.
View nameEndingAt(View s, std::size_t end)
{
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    std::size_t b = end;
    while (b > 0 && nameChar(s[b - 1])) --b;
    return s.substr(b, end - b);
}

/// The trailing identifier of `s` (`export const handler` -> `handler`).
View lastIdent(View s)
{
    std::size_t e = s.size();
    while (e > 0 && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::size_t b = e;
    while (b > 0 && identChar(s[b - 1])) --b;
    return s.substr(b, e - b);
}

/// Go: `func (p *Parser) Term(` and `func Map[T any](` (a receiver, a type-parameter list). Gives `Parser::Term` and `Map`; "" when `line` is neither.
std::string goName(View line)
{
    const auto first = firstNonBlank(line);
    if (first == View::npos || line.compare(first, 5, "func ") != 0) return {};
    std::size_t i = first + 5;
    while (i < line.size() && line[i] == ' ') ++i;
    std::string receiver;
    if (i < line.size() && line[i] == '(') {
        const auto close = line.find(')', i);
        if (close == View::npos) return {};
        View r = line.substr(i + 1, close - i - 1);
        if (const auto br = r.find('['); br != View::npos) r = r.substr(0, br);   // `Stack[T]` -> `Stack`
        receiver = std::string(lastIdent(r));
        i = close + 1;
        while (i < line.size() && line[i] == ' ') ++i;
    }
    const std::size_t b = i;
    while (i < line.size() && identChar(line[i])) ++i;
    if (i == b) return {};
    const View name = line.substr(b, i - b);
    if (i < line.size() && line[i] == '[') {   // type parameters: `[T any, U comparable]`
        int depth = 0;
        for (; i < line.size(); ++i) { if (line[i] == '[') ++depth; else if (line[i] == ']' && --depth == 0) { ++i; break; } }
    }
    if (i >= line.size() || line[i] != '(') return {};
    return receiver.empty() ? std::string(name) : receiver + "::" + std::string(name);
}

/// The position of the '(' that opens the parameter list: the first one outside `<...>` type arguments (`fn parse<T>(`, `std::function<void(int)> make(`), or the
/// first '(' when angle brackets do not balance. `operator<` and a comparison are not type arguments.
std::size_t parameterParen(View line)
{
    int angle = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '(' && angle == 0) return i;
        if (c == '<') {
            std::size_t p = i;
            while (p > 0 && std::isspace(static_cast<unsigned char>(line[p - 1]))) --p;
            const bool afterOperator = p >= 8 && line.compare(p - 8, 8, "operator") == 0;   // the name read back from `p` ends in `operator`
            if (p > 0 && (identChar(line[p - 1]) || line[p - 1] == '>') && !afterOperator) ++angle;
        }
        else if (c == '>' && angle > 0 && (i == 0 || line[i - 1] != '-')) --angle;
    }
    return line.find('(');
}

/// The name of a function bound to a variable, property or field: `const add = (a, b) => {`, `let f = function (a) {`, `run: function (a) {`,
/// `handler: Handler = async (req) => {` give `add`, `f`, `run`, `handler`. `head` is the text before the parameter list.
View assignedName(View head)
{
    std::size_t eq = View::npos;
    for (std::size_t i = 0; i < head.size(); ++i)
        if (head[i] == '=' && (i + 1 >= head.size() || (head[i + 1] != '=' && head[i + 1] != '>')) && (i == 0 || View("!<>=").find(head[i - 1]) == View::npos)) { eq = i; break; }
    View lhs = head.substr(0, eq);   // npos: the whole head, an object property or a label
    if (eq == View::npos && lhs.find(':') == View::npos) return {};
    for (std::size_t i = 0; i < lhs.size(); ++i)   // a type annotation: `handler: Handler`
        if (lhs[i] == ':' && !(i + 1 < lhs.size() && lhs[i + 1] == ':') && !(i > 0 && lhs[i - 1] == ':')) { lhs = lhs.substr(0, i); break; }
    return lastIdent(lhs);
}

/// The name of the function a signature line declares, as the source qualifies it. Reads what `nameBefore` reads and, where that gives nothing usable, the
/// declaration forms it could not: Go receivers and type parameters, generic parameter lists, operators, and functions bound to a name.
std::string signatureName(View line)
{
    if (std::string go = goName(line); !go.empty()) return go;
    const std::size_t paren = parameterParen(line);
    if (paren == View::npos) return {};
    std::size_t nameEnd = paren;   // before a type-argument list: `parse<T>(` reads `parse`
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(line[nameEnd - 1]))) --nameEnd;
    if (nameEnd > 0 && line[nameEnd - 1] == '>') {
        int angle = 0;
        std::size_t k = nameEnd;
        while (k > 0) {
            --k;
            if (line[k] == '>' && !(k > 0 && line[k - 1] == '-')) ++angle;
            else if (line[k] == '<' && --angle == 0) break;
        }
        if (angle == 0 && k > 0 && identChar(line[k - 1])) nameEnd = k;
    }
    const View id = nameEndingAt(line, nameEnd);
    if (id.empty()) {   // `bool operator<(`: the symbol before the '(' belongs to an `operator` keyword
        std::size_t e = paren;
        while (e > 0 && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
        std::size_t b = e;
        while (b > 0 && View("+-*/%^&|~!=<>,[]").find(line[b - 1]) != View::npos) --b;
        if (b < e) {
            const View before = nameEndingAt(line, b);
            if (before.size() >= 8 && before.compare(before.size() - 8, 8, "operator") == 0) {
                std::string symbol(line.substr(b, e - b));
                symbol.erase(std::remove_if(symbol.begin(), symbol.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), symbol.end());
                return std::string(before) + symbol;
            }
        }
    }
    if (id.empty() || id == "function" || id == "async") {
        if (const View named = assignedName(line.substr(0, paren)); !named.empty()) return std::string(named);
    }
    return std::string(id);
}

/// One function-like block as the heuristics find it, before it becomes a node.
struct Found { int sig, bodyFrom, bodyTo; std::string qualified; int spanFirst = 0; };

OutlineNode toNode(Found&& f, const std::vector<View>& lines)
{
    OutlineNode n;
    n.kind = "function";
    const auto colon = f.qualified.rfind(':');
    n.name = colon == std::string::npos ? f.qualified : f.qualified.substr(colon + 1);
    n.first = f.spanFirst > 0 ? f.spanFirst : f.sig;
    n.last = std::max(f.sig, f.bodyTo);
    n.nameLine = f.sig;
    n.bodyFrom = f.bodyFrom;
    n.bodyTo = f.bodyTo;
    n.signature = collapse(lines[static_cast<std::size_t>(f.sig) - 1]);
    n.qualifiedName = std::move(f.qualified);
    return n;
}

class BracesProvider final : public OutlineProvider
{
public:
    [[nodiscard]] std::string name() const override { return "braces@3"; }
    [[nodiscard]] Outline outline(std::string_view text) const override
    {
        const auto raw = splitViews(text);
        // A text with no "/*" has no block comment to blank: it is scanned as it is, with no copy (the cost of R-BLK-5 is paid by the texts that have one).
        std::string blanked;
        std::vector<View> blankedLines;
        if (text.find("/*") != View::npos) {
            blanked = blankBlockComments(text, raw);
            blankedLines.reserve(raw.size());
            for (const View l : raw) blankedLines.emplace_back(blanked.data() + (l.data() - text.data()), l.size());
        }
        const auto& lines = blankedLines.empty() ? raw : blankedLines;
        Outline out;
        out.provider = name();
        int depth = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            bool opens = false;
            const int d = braceDelta(lines[i], opens);
            // Allman style: the signature is on the line before a lone '{'.
            const auto firstNb = firstNonBlank(lines[i]);
            const bool loneBrace = firstNb != View::npos && lines[i][firstNb] == '{' && firstNonBlank(lines[i].substr(firstNb + 1)) == View::npos;
            const bool allman = loneBrace && i > 0 && lines[i - 1].find('(') != View::npos && lines[i - 1].find('{') == View::npos && lines[i - 1].find(';') == View::npos && functionLike(lines[i - 1]);
            if (opens && d > 0 && (allman || functionLike(lines[i]))) {
                const int start = depth;
                const std::size_t sig = allman ? i - 1 : i;
                int dd = depth + d;
                std::size_t j = i;
                while (dd > start && j + 1 < lines.size()) { ++j; bool o2 = false; dd += braceDelta(lines[j], o2); }
                if (dd <= start) out.nodes.push_back(toNode({static_cast<int>(sig) + 1, static_cast<int>(i) + 2, static_cast<int>(j) + 1, signatureName(lines[sig])}, raw));   // the closing line stays visible
                else out.partial = true;
                depth = dd;
                i = j;
                continue;
            }
            if (opens && d == 0 && functionLike(lines[i]) && lines[i].find('}') != View::npos && lines[i].find(';') != View::npos && lines[i].find('{') < lines[i].rfind('}'))
                out.nodes.push_back(toNode({static_cast<int>(i) + 1, static_cast<int>(i) + 1, static_cast<int>(i) + 1, signatureName(lines[i])}, raw));   // a function written on one line
            depth += d;
        }
        return out;
    }
};

class IndentProvider final : public OutlineProvider
{
public:
    [[nodiscard]] std::string name() const override { return "indent@2"; }
    [[nodiscard]] Outline outline(std::string_view text) const override
    {
        const auto lines = splitViews(text);
        Outline out;
        out.provider = name();
        std::size_t floor = 0;   // the first line after the previous block: a decorator search never reaches into it
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto ind = firstNonBlank(lines[i]);
            if (ind == View::npos) continue;
            const View t = lines[i].substr(ind);
            if (!t.starts_with("def ") && !t.starts_with("async def ")) continue;
            std::size_t j = i + 1, lastBody = i;
            for (; j < lines.size(); ++j) {
                const auto in2 = firstNonBlank(lines[j]);
                if (in2 == View::npos) continue;   // blank line
                if (in2 <= ind) break;
                lastBody = j;
            }
            // decorators: the `@name` lines directly above, at the same indent, belong to the block (R-BLK-6)
            std::size_t span = i;
            while (span > floor && firstNonBlank(lines[span - 1]) == ind && lines[span - 1][ind] == '@') --span;
            // a multi-line signature: the body starts after the line that ends the signature
            out.nodes.push_back(toNode({static_cast<int>(i) + 1, static_cast<int>(std::min(i + 1, lastBody)) + 1, static_cast<int>(lastBody) + 1, nameBefore(lines[i]), static_cast<int>(span) + 1}, lines));
            i = lastBody;
            floor = lastBody + 1;
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
        if (std::ranges::find(d.filenames, file) != d.filenames.end()) return d.language;
    for (const auto& d : detects_)
        for (const auto& e : d.extensions) if (endsWith(path, e)) return d.language;
    if (firstLine.starts_with("#!")) {
        const std::string word = interpreterOf(firstLine);
        for (const auto& d : detects_)
            if (std::ranges::find(d.interpreters, word) != d.interpreters.end()) return d.language;
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

}  // namespace sub0canvas
