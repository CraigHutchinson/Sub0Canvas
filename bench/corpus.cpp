// The synthetic corpus and its edit stream (bench/corpus.hpp). Integer-only randomness (SplitMix64, modulo picks, bucketed distributions) so the output does
// not depend on the standard library's distributions: the same seed gives the same bytes everywhere.
#include "bench/corpus.hpp"
#include "bench/rng.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <set>
#include <utility>

namespace sub0canvas::bench {

namespace {

constexpr std::size_t kVocabulary = 2048;
constexpr std::size_t kEntriesPerDirectory = 24;
constexpr std::size_t kRetiredPoolMax = 64;

using Rng = rng::SplitMix64;   // bench/rng.hpp: the generator (and its frozen picks) every call site below keeps using
using rng::mix;

std::string syllable(std::size_t s)
{
    static constexpr std::string_view kCons = "bdklmnrt", kVowels = "aeio";
    return {kCons[(s >> 2) & 7], kVowels[s & 3]};
}

std::string word(std::size_t i)
{
    i %= kVocabulary;
    std::string w = syllable(i & 31) + syllable((i >> 5) & 31);
    if (i >= 1024) w += syllable((i * 7 + 3) & 31);
    return w;
}

std::string capital(std::string w)
{
    if (!w.empty()) w[0] = static_cast<char>(w[0] - 'a' + 'A');
    return w;
}

std::string base36(std::size_t n)
{
    static constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    do { s.insert(s.begin(), kDigits[n % 36]); n /= 36; } while (n != 0);
    return s;
}

std::string hex(Rng& r, std::size_t digits)
{
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < digits; ++i) s += kDigits[r.below(16)];
    return s;
}

std::size_t countLines(const std::string& text) { return static_cast<std::size_t>(std::ranges::count(text, '\n')); }

std::string trimmed(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a);
}

// ---- code files ------------------------------------------------------------------------------------------------------------------------------------------

/// What a function body is built from: the variables it uses, the functions it calls, the strings it holds.
struct BodyCtx
{
    Rng& r;
    bool py;
    std::vector<std::string> vars, calls, lits;
    std::size_t numbers = 0;
    const std::string& var() { return vars[r.below(vars.size())]; }
    const std::string& call() { return calls[r.below(calls.size())]; }
    std::string num() { return std::to_string(r.below(1000) + ++numbers % 7); }
};

std::string plainStatement(BodyCtx& c, const std::string& ind)
{
    static constexpr std::array<const char*, 5> kOps = {"+", "-", "*", "/", "%"};
    static constexpr std::array<const char*, 4> kTypes = {"int", "double", "auto", "std::size_t"};
    const std::string x = c.var(), y = c.var(), z = c.var();
    const std::size_t pick = c.r.below(100);
    // Every draw from the generator is its own statement, in the order the output has always had (the last argument first): the order in which a call's arguments are
    // evaluated is unspecified, and gcc and clang differ, which made the corpus (so every byte metric built on it) depend on the compiler.
    if (pick < 30) {
        const std::string number = c.num();
        const char* op = kOps[c.r.below(5)];
        const std::string type = c.py ? "" : std::string(kTypes[c.r.below(4)]) + " ";
        return std::format("{}{}{} = {} {} {}{}", ind, type, x, y, op, number, c.py ? "" : ";");
    }
    if (pick < 50) return std::format("{}{} = {}({}, {}){}", ind, x, c.call(), y, z, c.py ? "" : ";");   // one draw
    if (pick < 62) return std::format("{}{} += {} * {}{}", ind, x, y, c.num(), c.py ? "" : ";");   // one draw
    if (pick < 72) {
        const std::string& second = c.lits[c.r.below(c.lits.size())];
        const std::string& first = c.lits[c.r.below(c.lits.size())];
        return std::format("{}{}{} = \"{} {}\"{}", ind, c.py ? "" : "auto ", x, first, second, c.py ? "" : ";");
    }
    if (pick < 84) {
        const std::string number = c.num();
        const std::string& callee = c.call();
        return std::format("{}{}({}, {}){}", ind, callee, x, number, c.py ? "" : ";");
    }
    if (pick < 92) {
        const std::string& last = c.lits[c.r.below(c.lits.size())];
        const std::string& first = c.lits[c.r.below(c.lits.size())];
        return std::format("{}{} {} {} {}", ind, c.py ? "#" : "//", first, x, last);
    }
    return c.py ? std::format("{}{}.append({})", ind, x, y) : std::format("{}{}.push_back({});", ind, x, y);
}

/// Emit statements until `budget` more lines exist in `out`; a block statement (if, for, while) spends its header, braces and nested statements from the budget.
void emitStatements(BodyCtx& c, std::vector<std::string>& out, std::size_t budget, std::size_t depth)
{
    const std::string ind(4 * (depth + 1), ' ');
    const std::size_t start = out.size();
    while (out.size() - start < budget)
    {
        const std::size_t left = budget - (out.size() - start);
        const std::size_t overhead = c.py ? 1 : 3;
        if (depth < 2 && left >= overhead + 1 && c.r.percent(22))
        {
            const std::string x = c.var(), y = c.var();
            const std::size_t kind = c.r.below(3);
            if (c.py)
                out.push_back(kind == 0 ? std::format("{}if {} < {}:", ind, x, y) : kind == 1 ? std::format("{}for i in range({}):", ind, c.num()) : std::format("{}while {} > {}:", ind, x, c.num()));
            else
            {
                out.push_back(kind == 0 ? std::format("{}if ({} < {})", ind, x, y) : kind == 1 ? std::format("{}for (int i = 0; i < {}; ++i)", ind, c.num()) : std::format("{}while ({} > {})", ind, x, c.num()));
                out.push_back(ind + "{");
            }
            emitStatements(c, out, c.r.range(1, std::min<std::size_t>(5, left - overhead)), depth + 1);
            if (!c.py) out.push_back(ind + "}");
        }
        else
        {
            std::string line = plainStatement(c, ind);
            // A block opens with a real statement: a comment-only body is not a valid Python body and makes a hollow block.
            while (out.size() == start && trimmed(line).starts_with(c.py ? "#" : "//")) line = plainStatement(c, ind);
            out.push_back(std::move(line));
        }
    }
}

std::size_t bodyLines(Rng& r)
{
    const std::size_t p = r.below(100);
    if (p < 18) return r.range(1, 3);
    if (p < 58) return r.range(4, 10);
    if (p < 84) return r.range(11, 25);
    if (p < 96) return r.range(26, 60);
    return r.range(61, 150);
}

/// The names a file may use, shared by its functions so that they have tokens in common.
struct FilePool
{
    std::vector<std::string> words;
    bool py = false;
    std::string joined(Rng& r) const
    {
        const std::string& a = words[r.below(words.size())];
        const std::string& b = words[r.below(words.size())];
        return py ? a + "_" + b : a + capital(b);
    }
};

FilePool makePool(Rng& r, bool py)
{
    FilePool p;
    p.py = py;
    for (int i = 0; i < 40; ++i) p.words.push_back(word(r.skewed(kVocabulary)));
    return p;
}

std::string uniqueName(Rng& r, const FilePool& pool, const std::set<std::string>& taken, const std::string& klass)
{
    std::string base = pool.joined(r);
    std::string name = klass.empty() ? base : klass + "::" + base;
    for (std::size_t n = 2; taken.contains(name); ++n)
        name = (klass.empty() ? "" : klass + "::") + base + (pool.py ? "_" : "") + std::to_string(n);
    return name;
}

Function makeFunction(Rng& r, Language lang, bool header, const FilePool& pool, const std::set<std::string>& taken, std::uint64_t uid, const std::string& klass)
{
    static constexpr std::array<const char*, 6> kTypes = {"int", "double", "const std::string&", "std::size_t", "bool", "const std::vector<int>&"};
    static constexpr std::array<const char*, 6> kReturns = {"void", "int", "double", "bool", "std::size_t", "std::string"};
    const bool py = lang == Language::python;
    Function f;
    f.uid = uid;
    f.name = uniqueName(r, pool, taken, klass);
    std::vector<std::string> params;
    for (std::size_t i = 0, n = r.below(5); i < n; ++i) params.push_back(pool.words[r.below(pool.words.size())] + std::to_string(i));
    std::string list;
    for (std::size_t i = 0; i < params.size(); ++i)
        list += (i ? ", " : "") + (py ? params[i] : std::string(kTypes[r.below(kTypes.size())]) + " " + params[i]);
    const std::string ret = py ? "" : kReturns[r.below(kReturns.size())];
    f.signature = py ? std::format("def {}({})", f.name, list) : std::format("{}{} {}({})", header ? "inline " : "", ret, f.name, list);

    BodyCtx c{r, py, params, {}, {}, 0};
    for (std::size_t i = 0, n = r.range(3, 6); i < n; ++i) c.vars.push_back(pool.words[r.below(pool.words.size())]);
    for (std::size_t i = 0, n = r.range(2, 4); i < n; ++i) c.calls.push_back(pool.joined(r));
    for (int i = 0; i < 3; ++i) c.lits.push_back(pool.words[r.below(pool.words.size())]);
    const std::size_t total = bodyLines(r);
    const bool returns = py ? r.percent(60) : ret != "void";
    const std::size_t statements = returns && total >= 2 ? total - 1 : total;
    emitStatements(c, f.body, statements, 0);
    if (returns && total >= 2) f.body.push_back(std::format("    return {}{}", c.var(), py ? "" : ";"));
    return f;
}

std::size_t functionCount(Rng& r)
{
    const std::size_t p = r.below(100);
    if (p < 50) return r.range(2, 12);
    if (p < 83) return r.range(13, 30);
    return r.range(31, 70);
}

CodeFile makeCodeFile(Rng& r, Language lang, bool header, std::uint64_t uidSalt)
{
    static constexpr std::array<const char*, 5> kIncludes = {"<algorithm>", "<cstdint>", "<map>", "<string>", "<vector>"};
    CodeFile file;
    file.language = lang;
    const bool py = lang == Language::python;
    const FilePool pool = makePool(r, py);
    if (py)
    {
        for (std::size_t i = 0, n = r.range(1, 4); i < n; ++i) {
            if (i % 2) {
                const std::string imported = word(r.skewed(kVocabulary));
                const std::string module = word(r.skewed(kVocabulary));
                file.preamble.push_back(std::format("from {} import {}", module, imported));
            } else {
                file.preamble.push_back("import " + word(r.skewed(kVocabulary)));
            }
        }
        for (std::size_t i = 0, n = r.below(3); i < n; ++i) {
            const std::size_t limit = r.below(1000);
            file.preamble.push_back(std::format("{}_LIMIT = {}", capital(word(r.skewed(kVocabulary))), limit));
        }
    }
    else
    {
        const std::string ns = word(r.skewed(kVocabulary));
        file.preamble.push_back(header ? "#pragma once" : std::format("#include \"{}.hpp\"", word(r.skewed(kVocabulary))));
        file.preamble.push_back("");
        std::set<std::size_t> chosen;
        for (std::size_t i = 0, n = r.range(2, 4); i < n; ++i) chosen.insert(r.below(kIncludes.size()));
        for (const std::size_t i : chosen) file.preamble.push_back(std::string("#include ") + kIncludes[i]);
        file.preamble.push_back("");
        file.preamble.push_back(std::format("namespace {} {{", ns));
        for (std::size_t i = 0, n = r.below(3); i < n; ++i) {
            const std::size_t value = r.below(1000);
            file.preamble.push_back(std::format("constexpr int k{} = {};", capital(word(r.skewed(kVocabulary))), value));
        }
        file.epilogue = {"", std::format("}}  // namespace {}", ns)};
    }
    const std::size_t count = functionCount(r);
    std::set<std::string> taken;
    std::string klass;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!py && !header && i % 6 == 0) klass = r.percent(35) ? capital(word(r.skewed(kVocabulary))) : std::string();   // a run of methods, or free functions
        Function f = makeFunction(r, lang, header, pool, taken, mix(uidSalt, i), klass);
        taken.insert(f.name);
        file.functions.push_back(std::move(f));
    }
    return file;
}

// ---- other files -----------------------------------------------------------------------------------------------------------------------------------------

std::string makeSidecar(Rng& r, const char* importer)
{
    std::string t = std::format("fileFormatVersion: 2\nguid: {}\n{}Importer:\n  externalObjects: {{}}\n  serializedVersion: {}\n", hex(r, 32), importer, r.range(5, 12));
    for (std::size_t i = 0, n = r.range(3, 8); i < n; ++i) {
        const std::size_t value = r.below(8);
        t += std::format("  {}: {}\n", word(r.skewed(kVocabulary)), value);
    }
    t += "  userData: \n  assetBundleName: \n  assetBundleVariant: \n";
    return t;
}

std::string makeImage(Rng& r)
{
    std::string bytes = "\x89PNG\r\n\x1a\n";
    const std::size_t size = std::size_t{256} << r.below(6);
    while (bytes.size() < size)
    {
        std::uint64_t v = r.next();
        for (int i = 0; i < 8; ++i, v >>= 8) bytes.push_back(static_cast<char>(v & 0xFF));
    }
    return bytes;
}

std::string makeSerialised(Rng& r)
{
    std::string t = "%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n";
    for (std::size_t i = 0, n = r.range(5, 80); i < n; ++i)
    {
        {
            const std::size_t active = r.below(2);
            const std::string name = word(r.skewed(kVocabulary));
            const std::size_t fileId = r.range(100, 99999);
            const std::size_t classId = r.range(1, 200);
            t += std::format("--- !u!{} &{}\nGameObject:\n  m_ObjectHideFlags: 0\n  m_Name: {}\n  m_IsActive: {}\n  m_Components:\n", classId, fileId, name, active);
        }
        for (std::size_t k = 0, m = r.range(1, 3); k < m; ++k) t += std::format("  - component: {{fileID: {}}}\n", r.range(100, 99999));
    }
    return t;
}

std::string makeText(Rng& r)
{
    std::string t;
    for (std::size_t i = 0, n = r.range(10, 200); i < n; ++i)
    {
        for (std::size_t w = 0, m = r.range(6, 12); w < m; ++w) t += (w ? " " : "") + word(r.skewed(kVocabulary));
        t += '\n';
    }
    return t;
}

std::string makeMarkdown(Rng& r)
{
    std::string t = "# " + capital(word(r.skewed(kVocabulary))) + "\n\n";
    for (std::size_t i = 0, n = r.range(4, 30); i < n; ++i)
    {
        const std::size_t p = r.below(100);
        if (p < 15) t += "## " + capital(word(r.skewed(kVocabulary))) + "\n\n";
        else if (p < 45) { for (std::size_t k = 0, m = r.range(2, 5); k < m; ++k) {
                const std::string second = word(r.skewed(kVocabulary));
                const std::string first = word(r.skewed(kVocabulary));
                t += "* " + first + " " + second + "\n";
            } t += '\n'; }
        else { for (std::size_t w = 0, m = r.range(12, 40); w < m; ++w) t += (w ? " " : "") + word(r.skewed(kVocabulary)); t += "\n\n"; }
    }
    return t;
}

/// The directory of entry `index`: a pure function of it, 24 entries to a directory, depth 1-5, each segment a vocabulary word chosen by one mixed-radix digit.
std::string directoryOf(const CorpusSpec& spec, std::size_t index, const char* root)
{
    const std::size_t dir = index / kEntriesPerDirectory;
    const std::size_t depth = 1 + mix(spec.seed, dir) % 5;
    std::string path = root;
    std::size_t rest = dir;
    for (std::size_t k = 0; k < depth; ++k, rest /= 12) path += "/" + word((rest % 12) * 37 + k * 211);
    return path;
}

std::string fileName(const CorpusSpec& spec, std::size_t index)
{
    return word(mix(spec.seed, index) % kVocabulary) + capital(word(mix(index, spec.seed) % kVocabulary)) + "_" + base36(index);
}

GeneratedFile textFile(std::string path, std::string text, FileKind kind)
{
    GeneratedFile f;
    f.path = std::move(path);
    f.lines = countLines(text);
    f.text = std::move(text);
    f.kind = kind;
    return f;
}

}  // namespace

KindMix profileA() { return KindMix{}; }

KindMix profileB()
{
    // docs/SCALE.md section 2, profile B, as entry weights: images and serialised data (with the "other" assets folded in) each bring a sidecar, so the file
    // shares come out near the measured third of sidecars and 14% of images; code is the C#, header and source counts together.
    KindMix m;
    m.code = 0.259;
    m.image = 0.141;
    m.serialised = 0.182;
    m.text = 0.059;
    m.markdown = 0.025;
    return m;
}

std::string CodeFile::render() const
{
    const bool py = language == Language::python;
    std::string out;
    for (const std::string& l : preamble) { out += l; out += '\n'; }
    for (const Function& f : functions)
    {
        out += py ? "\n\n" : "\n";
        out += f.signature;
        out += py ? ":\n" : "\n{\n";
        for (const std::string& l : f.body) { out += l; out += '\n'; }
        if (!py) out += "}\n";
    }
    for (const std::string& l : epilogue) { out += l; out += '\n'; }
    return out;
}

std::vector<GeneratedFile> entry(const CorpusSpec& spec, std::size_t index)
{
    Rng r(mix(spec.seed, index));
    const KindMix& m = spec.mix;
    const double total = m.code + m.image + m.serialised + m.text + m.markdown;
    // The pick uses 1/65536 steps of the total: integer-stable.
    const double pick = static_cast<double>(r.below(65536)) / 65536.0 * total;
    const std::string name = fileName(spec, index);
    std::vector<GeneratedFile> files;
    if (pick < m.code)
    {
        const bool py = r.below(1000) < static_cast<std::size_t>(m.pythonShare * 1000);
        const bool header = !py && r.below(1000) < static_cast<std::size_t>(m.headerShare * 1000);
        GeneratedFile f;
        f.kind = FileKind::code;
        f.path = directoryOf(spec, index, "src") + "/" + name + (py ? ".py" : header ? ".hpp" : ".cpp");
        f.code = makeCodeFile(r, py ? Language::python : Language::cpp, header, mix(spec.seed, index ^ 0x5bd1e995));
        f.text = f.code->render();
        f.lines = countLines(f.text);
        files.push_back(std::move(f));
    }
    else if (pick < m.code + m.image)
    {
        GeneratedFile img;
        img.kind = FileKind::image;
        img.path = directoryOf(spec, index, "assets") + "/" + name + ".png";
        img.text = makeImage(r);
        files.push_back(std::move(img));
        files.push_back(textFile(files[0].path + ".meta", makeSidecar(r, "Texture"), FileKind::sidecar));
    }
    else if (pick < m.code + m.image + m.serialised)
    {
        static constexpr std::array<const char*, 4> kExtensions = {".asset", ".unity", ".prefab", ".mat"};
        files.push_back(textFile(directoryOf(spec, index, "assets") + "/" + name + kExtensions[r.below(kExtensions.size())], makeSerialised(r), FileKind::serialised));
        files.push_back(textFile(files[0].path + ".meta", makeSidecar(r, "Native"), FileKind::sidecar));
    }
    else if (pick < m.code + m.image + m.serialised + m.text)
        files.push_back(textFile(directoryOf(spec, index, "docs") + "/" + name + ".txt", makeText(r), FileKind::text));
    else
        files.push_back(textFile(directoryOf(spec, index, "docs") + "/" + name + ".md", makeMarkdown(r), FileKind::markdown));
    return files;
}

std::optional<std::vector<GeneratedFile>> CorpusStream::next()
{
    if (lines_ >= spec_.targetLines) return std::nullopt;
    std::vector<GeneratedFile> files = entry(spec_, index_++);
    for (const GeneratedFile& f : files) lines_ += f.lines;
    return files;
}

std::expected<Corpus, std::string> generateCorpus(const CorpusSpec& spec)
{
    if (spec.targetLines > kMaterialiseLimitLines)
        return std::unexpected(std::format("{} lines exceeds the in-memory limit of {}: stream it with CorpusStream or sample it with entry()", spec.targetLines, kMaterialiseLimitLines));
    Corpus corpus;
    CorpusStream stream(spec);
    while (auto files = stream.next())
    {
        ++corpus.stats.entries;
        for (GeneratedFile& f : *files)
        {
            CorpusStats& s = corpus.stats;
            ++s.files;
            s.lines += f.lines;
            s.bytes += f.text.size();
            switch (f.kind)
            {
            case FileKind::code: ++s.codeFiles; s.codeLines += f.lines; s.blocks += f.code->functions.size(); break;
            case FileKind::sidecar: ++s.sidecars; break;
            case FileKind::image: ++s.images; break;
            case FileKind::serialised: ++s.serialised; break;
            case FileKind::text: ++s.texts; break;
            case FileKind::markdown: ++s.markdowns; break;
            }
            if (f.code) corpus.models.emplace(f.path, std::move(*f.code));
            corpus.files.emplace(f.path, std::move(f.text));
        }
    }
    return corpus;
}

double meanLinesPerEntry(const CorpusSpec& spec, std::size_t sample)
{
    if (sample == 0) return 0;
    std::size_t lines = 0;
    for (std::size_t i = 0; i < sample; ++i)
        for (const GeneratedFile& f : entry(spec, i)) lines += f.lines;
    return static_cast<double>(lines) / static_cast<double>(sample);
}

// ---- the edit stream -------------------------------------------------------------------------------------------------------------------------------------

namespace {

std::size_t indentOf(const std::string& s) { return std::min(s.find_first_not_of(' '), s.size()); }

/// A line that stands alone as a statement, so it can be replaced or a line inserted beside it without breaking the block structure.
bool isPlain(const std::string& line, bool py)
{
    const std::string t = trimmed(line);
    if (t.empty() || t.starts_with("return") || t.starts_with("//") || t.starts_with("#")) return false;
    return py ? t.back() != ':' : t.back() == ';';
}

std::vector<std::string> identifiersOf(const Function& f)
{
    static const std::set<std::string> kKeywords = {"if", "for", "while", "return", "int", "double", "auto", "void", "bool", "std", "string", "size_t", "const",
                                                    "def", "range", "in", "inline", "vector", "append", "push_back", "i"};
    std::set<std::string> found;
    auto scan = [&](const std::string& line)
    {
        for (std::size_t i = 0; i < line.size();)
        {
            if (std::isalpha(static_cast<unsigned char>(line[i])))
            {
                std::size_t j = i;
                while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
                std::string w = line.substr(i, j - i);
                if (!kKeywords.contains(w) && (j >= line.size() || line[j] != ':')) found.insert(w);
                i = j;
            }
            else ++i;
        }
    };
    for (const std::string& l : f.body) if (!trimmed(l).starts_with("//") && !trimmed(l).starts_with("#")) scan(l);
    return {found.begin(), found.end()};
}

/// The class qualifier of a name (`Klass::`), or "".
std::string qualifierOf(const std::string& name)
{
    const std::size_t p = name.rfind("::");
    return p == std::string::npos ? std::string() : name.substr(0, p);
}

std::string unqualified(const std::string& name)
{
    const std::size_t p = name.rfind("::");
    return p == std::string::npos ? name : name.substr(p + 2);
}

}  // namespace

struct EditStream::Impl
{
    Rng r;
    EditMix mixture;
    std::map<std::string, CodeFile> models;
    std::vector<std::pair<Function, std::string>> retiredPool;   ///< removed functions that may be revived, with the file they left
    std::uint64_t nextUid = std::uint64_t{1} << 62;
    std::size_t commits = 0, counter = 0, newFiles = 0;

    Impl(const Corpus& base, std::uint64_t seed, EditMix m) : r(mix(seed, 0xED17)), mixture(m), models(base.models) {}

    [[nodiscard]] static std::set<std::string> namesOf(const CodeFile& f)
    {
        std::set<std::string> n;
        for (const Function& fn : f.functions) n.insert(fn.name);
        return n;
    }
    [[nodiscard]] std::string freshStatement(const Function& f, bool py)
    {
        std::vector<std::string> ids = identifiersOf(f);
        if (ids.size() < 2) ids = {word(r.skewed(kVocabulary)), word(r.skewed(kVocabulary))};
        const std::string& right = ids[r.below(ids.size())];
        const std::string& left = ids[r.below(ids.size())];
        return std::format("{} = {} + {}{}", left, right, 1000 + ++counter, py ? "" : ";");
    }
    /// Change the body without changing its structure: one standalone statement replaced, or one inserted at the top when there is none.
    void touchBody(Function& f, bool py)
    {
        std::vector<std::size_t> plain;
        for (std::size_t i = 0; i < f.body.size(); ++i) if (isPlain(f.body[i], py)) plain.push_back(i);
        if (plain.empty()) { f.body.insert(f.body.begin(), "    " + freshStatement(f, py)); return; }
        std::string& line = f.body[plain[r.below(plain.size())]];
        line = std::string(indentOf(line), ' ') + freshStatement(f, py);
    }
    [[nodiscard]] std::string freshName(const CodeFile& file, const std::string& qualifier)
    {
        const std::set<std::string> taken = namesOf(file);
        const std::string a = word(r.skewed(kVocabulary)), b = word(r.skewed(kVocabulary));
        const bool py = file.language == Language::python;
        std::string name = py ? std::format("{}_{}_{}", a, b, ++counter) : std::format("{}{}{}", a, capital(b), ++counter);
        if (!qualifier.empty()) name = qualifier + "::" + name;
        return taken.contains(name) ? name + "x" : name;
    }
    [[nodiscard]] std::size_t pickPosition(const CodeFile& file) { return r.below(file.functions.size() + 1); }
    void retire(std::vector<std::pair<Function, std::string>>& staged, Function f, const std::string& path) { staged.emplace_back(std::move(f), path); }

    Commit next()
    {
        Commit c;
        c.number = ++commits;
        std::vector<std::string> paths;
        for (const auto& [p, f] : models) paths.push_back(p);
        std::set<std::string> touched;
        std::vector<std::pair<Function, std::string>> staged;
        const std::size_t edits = 1 + r.below(std::max<std::size_t>(1, mixture.editsPerCommitMax));
        const std::array<std::pair<double, int>, 11> weights = {{{mixture.edited, 0}, {mixture.resigned, 1}, {mixture.renamed, 2}, {mixture.added, 3}, {mixture.removed, 4},
                                                                 {mixture.revived, 5}, {mixture.moved, 6}, {mixture.split, 7}, {mixture.merged, 8}, {mixture.newFile, 9},
                                                                 {mixture.deleteFile, 10}}};
        double sum = 0;
        for (const auto& w : weights) sum += w.first;
        for (std::size_t e = 0; e < edits; ++e)
            for (int attempt = 0; attempt < 12; ++attempt)
            {
                double pick = static_cast<double>(r.below(65536)) / 65536.0 * sum;
                int kind = 0;
                for (const auto& w : weights) { kind = w.second; if (pick < w.first) break; pick -= w.first; }
                if (apply(kind, c, paths, touched, staged)) break;
            }
        for (const std::string& p : touched)
        {
            const auto it = models.find(p);
            c.changes.push_back({p, it == models.end() ? std::nullopt : std::optional<std::string>(it->second.render())});
        }
        for (auto& s : staged) retiredPool.push_back(std::move(s));
        if (retiredPool.size() > kRetiredPoolMax) retiredPool.erase(retiredPool.begin(), retiredPool.begin() + static_cast<std::ptrdiff_t>(retiredPool.size() - kRetiredPoolMax));
        return c;
    }

    /// A file not yet touched in this commit with at least `minFunctions` functions, or "".
    [[nodiscard]] std::string pickFile(const std::vector<std::string>& paths, const std::set<std::string>& touched, std::size_t minFunctions)
    {
        if (paths.empty()) return {};
        for (int i = 0; i < 16; ++i)
        {
            const std::string& p = paths[r.below(paths.size())];
            if (!touched.contains(p) && models.count(p) && models.at(p).functions.size() >= minFunctions) return p;
        }
        return {};
    }

    bool apply(int kind, Commit& c, const std::vector<std::string>& paths, std::set<std::string>& touched, std::vector<std::pair<Function, std::string>>& staged)
    {
        const auto truth = [&](EditKind k, const std::string& path, const std::string& name, const std::string& newName, std::uint64_t uid) -> EditTruth&
        {
            c.truth.push_back({});
            EditTruth& t = c.truth.back();
            t.kind = k; t.path = path; t.name = name; t.newName = newName; t.uid = uid;
            return t;
        };
        switch (kind)
        {
        case 0:   // edited
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            Function& f = file.functions[r.below(file.functions.size())];
            touchBody(f, file.language == Language::python);
            truth(EditKind::edited, p, f.name, f.name, f.uid);
            touched.insert(p);
            return true;
        }
        case 1:   // resigned
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const bool py = file.language == Language::python;
            Function& f = file.functions[r.below(file.functions.size())];
            const std::size_t open = f.signature.find('('), close = f.signature.rfind(')');
            if (open == std::string::npos || close == std::string::npos) return false;
            std::string list = f.signature.substr(open + 1, close - open - 1);
            if (list.empty() || r.percent(60))
                list += (list.empty() ? "" : ", ") + (py ? std::string() : std::string("int ")) + word(r.skewed(kVocabulary)) + std::to_string(++counter);
            else
            {
                const std::size_t cut = list.rfind(", ");
                list = cut == std::string::npos ? std::string() : list.substr(0, cut);
            }
            f.signature = f.signature.substr(0, open + 1) + list + f.signature.substr(close);
            truth(EditKind::resigned, p, f.name, f.name, f.uid);
            touched.insert(p);
            return true;
        }
        case 2:   // renamed
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const bool py = file.language == Language::python;
            Function& f = file.functions[r.below(file.functions.size())];
            const std::string oldName = f.name, newName = freshName(file, qualifierOf(oldName));
            const std::size_t at = f.signature.find(oldName + "(");
            if (at == std::string::npos) return false;
            f.signature.replace(at, oldName.size(), newName);
            f.name = newName;
            if (f.body.size() >= 8) touchBody(f, py);   // "a few lines touched": only where the body is big enough to stay similar
            truth(EditKind::renamed, p, oldName, newName, f.uid);
            touched.insert(p);
            return true;
        }
        case 3:   // added
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const bool py = file.language == Language::python;
            const FilePool pool = makePool(r, py);
            Function f = makeFunction(r, file.language, !py && file.preamble.front() == "#pragma once", pool, namesOf(file), nextUid++, std::string());
            const std::string name = f.name;
            const std::uint64_t uid = f.uid;
            file.functions.insert(file.functions.begin() + static_cast<std::ptrdiff_t>(pickPosition(file)), std::move(f));
            truth(EditKind::added, p, std::string(), name, uid);
            touched.insert(p);
            return true;
        }
        case 4:   // removed
        {
            const std::string p = pickFile(paths, touched, 2);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const std::size_t i = r.below(file.functions.size());
            Function f = std::move(file.functions[i]);
            file.functions.erase(file.functions.begin() + static_cast<std::ptrdiff_t>(i));
            truth(EditKind::removed, p, f.name, std::string(), f.uid);
            touched.insert(p);
            retire(staged, std::move(f), p);
            return true;
        }
        case 5:   // revived
        {
            if (retiredPool.empty()) return false;
            const std::size_t i = r.below(retiredPool.size());
            const std::string target = r.percent(50) && models.count(retiredPool[i].second) ? retiredPool[i].second : pickFile(paths, touched, 1);
            if (target.empty() || touched.contains(target) || !models.count(target)) return false;
            CodeFile& file = models.at(target);
            if (file.language != (retiredPool[i].first.signature.starts_with("def ") ? Language::python : Language::cpp)) return false;
            if (namesOf(file).contains(retiredPool[i].first.name)) return false;
            Function f = retiredPool[i].first;
            const std::string from = retiredPool[i].second;
            retiredPool.erase(retiredPool.begin() + static_cast<std::ptrdiff_t>(i));
            const std::string name = f.name;
            const std::uint64_t uid = f.uid;
            file.functions.insert(file.functions.begin() + static_cast<std::ptrdiff_t>(pickPosition(file)), std::move(f));
            truth(EditKind::revived, target, std::string(), name, uid).otherPath = from;
            touched.insert(target);
            return true;
        }
        case 6:   // moved
        {
            const std::string from = pickFile(paths, touched, 2);
            if (from.empty()) return false;
            touched.insert(from);
            const std::string to = pickFile(paths, touched, 1);
            touched.erase(from);
            if (to.empty()) return false;
            CodeFile& src = models.at(from);
            CodeFile& dst = models.at(to);
            if (src.language != dst.language) return false;
            const std::size_t i = r.below(src.functions.size());
            if (namesOf(dst).contains(src.functions[i].name)) return false;
            Function f = std::move(src.functions[i]);
            src.functions.erase(src.functions.begin() + static_cast<std::ptrdiff_t>(i));
            const std::string name = f.name;
            const std::uint64_t uid = f.uid;
            dst.functions.insert(dst.functions.begin() + static_cast<std::ptrdiff_t>(pickPosition(dst)), std::move(f));
            truth(EditKind::moved, from, name, name, uid).otherPath = to;
            touched.insert(from);
            touched.insert(to);
            return true;
        }
        case 7:   // split
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const bool py = file.language == Language::python;
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const std::size_t fi = r.below(file.functions.size());
                Function& f = file.functions[fi];
                if (f.body.size() < 8) continue;
                std::vector<std::size_t> cuts;   // a statement at the body's top level, leaving at least two lines on each side
                for (std::size_t k = 2; k + 2 <= f.body.size(); ++k)
                    if (indentOf(f.body[k]) == 4 && f.body[k].find_first_not_of(' ') != std::string::npos && f.body[k][4] != '{' && f.body[k][4] != '}') cuts.push_back(k);
                if (cuts.empty()) continue;
                const std::size_t k = cuts[r.below(cuts.size())];
                Function tail;
                tail.uid = nextUid++;
                const std::string qualifier = qualifierOf(f.name);
                tail.name = freshName(file, qualifier);
                tail.signature = py ? std::format("def {}()", tail.name)
                                    : std::format("{}void {}()", f.signature.starts_with("inline ") ? "inline " : "", tail.name);
                tail.body.assign(f.body.begin() + static_cast<std::ptrdiff_t>(k), f.body.end());
                f.body.resize(k);
                f.body.push_back(std::format("    {}(){}", unqualified(tail.name), py ? "" : ";"));
                const std::string name = f.name, newName = tail.name;
                const std::uint64_t uid = f.uid;
                file.functions.insert(file.functions.begin() + static_cast<std::ptrdiff_t>(fi + 1), std::move(tail));
                truth(EditKind::split, p, name, newName, uid);
                touched.insert(p);
                return true;
            }
            return false;
        }
        case 8:   // merged
        {
            const std::string p = pickFile(paths, touched, 2);
            if (p.empty()) return false;
            CodeFile& file = models.at(p);
            const std::size_t a = r.below(file.functions.size());
            std::size_t b = r.below(file.functions.size() - 1);
            if (b >= a) ++b;
            Function absorbed = std::move(file.functions[b]);
            Function& survivor = file.functions[a];
            survivor.body.insert(survivor.body.end(), absorbed.body.begin(), absorbed.body.end());
            const std::string survivorName = survivor.name;
            file.functions.erase(file.functions.begin() + static_cast<std::ptrdiff_t>(b));
            truth(EditKind::merged, p, absorbed.name, survivorName, absorbed.uid).otherPath = p;
            touched.insert(p);
            return true;
        }
        case 9:   // a new file
        {
            const bool py = r.percent(30);
            const bool header = !py && r.percent(30);
            const std::string path = std::format("src/added/{}_{}{}", word(r.skewed(kVocabulary)), ++newFiles, py ? ".py" : header ? ".hpp" : ".cpp");
            if (models.count(path)) return false;
            CodeFile file = makeCodeFile(r, py ? Language::python : Language::cpp, header, mix(nextUid++, newFiles));
            models.emplace(path, std::move(file));
            truth(EditKind::added, path, std::string(), std::string(), 0).fileAdded = true;
            touched.insert(path);
            return true;
        }
        default:  // 10: a deleted file
        {
            const std::string p = pickFile(paths, touched, 1);
            if (p.empty() || models.size() < 2) return false;
            for (Function& f : models.at(p).functions) retire(staged, std::move(f), p);
            models.erase(p);
            truth(EditKind::removed, p, std::string(), std::string(), 0).fileDeleted = true;
            touched.insert(p);
            return true;
        }
        }
    }
};

EditStream::EditStream(const Corpus& base, std::uint64_t seed, EditMix mix) : impl_(std::make_shared<Impl>(base, seed, mix)) {}

Commit EditStream::next() { return impl_->next(); }

FileMap EditStream::texts() const
{
    FileMap files;
    for (const auto& [path, model] : impl_->models) files.emplace(path, model.render());
    return files;
}

std::size_t EditStream::liveFunctions() const
{
    std::size_t n = 0;
    for (const auto& [path, model] : impl_->models) n += model.functions.size();
    return n;
}

}  // namespace sub0canvas::bench
