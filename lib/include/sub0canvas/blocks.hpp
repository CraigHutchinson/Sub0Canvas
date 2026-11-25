#pragma once
/// @file blocks.hpp
/// Block discovery behind one seam (docs/BLOCK_DISCOVERY.md sections 4.1, 4.2 and 4.7): every language's provider turns a text into the same result, an
/// `Outline` (a tree of nodes with kind, name, qualified name and spans), and `Languages` chooses the provider from the path. Structure is DERIVED here, a pure
/// function of (text, language, provider version): what carries an identity and what a view folds are policies applied to the outline (`foldRegionsOf`,
/// outline.hpp), never a caller's reason to change what a block is (R-BLK-11, R-BLK-12).

#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

/// One block of a file. Lines are 1-based and inclusive.
struct OutlineNode
{
    std::string kind;            ///< the LSP symbol-kind vocabulary, lower-case ("function", "method", "class", "namespace", ...); the heuristic providers emit "function"
    std::string name;            ///< the leaf name (`term` of `Parser::term`); empty when the provider could not name it, which never carries an identity (R-BLK-4)
    std::string qualifiedName;   ///< the path to it as the source writes it (`Parser::term`); equals `name` when unqualified
    int first = 0, last = 0;     ///< the whole span: signature (decorators and doc comments included, R-BLK-6) to the closing line
    int nameLine = 0;            ///< the line of the signature that carries the name
    int bodyFrom = 0, bodyTo = 0;///< what a fold hides: for braces `bodyTo` is the closing line, which stays visible; for indentation the last body line
    std::string signature;       ///< the signature line, whitespace collapsed
    std::vector<OutlineNode> children;
};

/// What a provider returns for one text.
struct Outline
{
    std::string language;                ///< "cpp", "python", ... ("" when the file is loose text)
    std::string provider;                ///< the provider's name and version ("braces@1"); recorded in the snapshot so a change of provider is reported (section 4.6)
    std::vector<OutlineNode> nodes;      ///< in file order; siblings disjoint
    bool partial = false;                ///< the text had unbalanced structure that the provider recovered from
};

/// Finds the blocks of one language. Contract (R-BLK-1, R-BLK-2): pure, total (any text, never throws), deterministic, linear in the text.
class OutlineProvider
{
public:
    virtual ~OutlineProvider() = default;
    /// The provider's name and version, `braces@1`: bumped whenever its output for some text changes.
    [[nodiscard]] virtual std::string name() const = 0;
    /// The outline of `text`. `language` and `provider` are filled by the provider's own name; `Languages` sets the language.
    [[nodiscard]] virtual Outline outline(std::string_view text) const = 0;
};

/// The provider registry and language detection (section 4.2). The default registry is what the library itself uses.
class Languages
{
public:
    /// Registers `provider` for `language` at `priority`; the highest priority wins, the first added breaks a tie.
    void add(std::string language, std::shared_ptr<const OutlineProvider> provider, int priority);
    /// Declares how a language is recognised: file name extensions (".py"), whole file names ("CMakeLists.txt") and interpreters named by a `#!` first line.
    void detects(const std::string& language, std::vector<std::string> extensions, std::vector<std::string> filenames = {}, std::vector<std::string> interpreters = {});
    /// The last-resort provider for text of a language with no provider of its own, or none detected: today's brace heuristic keeps every language no worse than it was.
    void setFallback(std::shared_ptr<const OutlineProvider> provider);

    /// The language of a path, by (Linguist's order, less modelines) file name, then extension, then the `#!` interpreter of `firstLine`; "" when unknown.
    /// Case-sensitive, and an extension needs a non-empty stem (`.py` alone is a hidden file, not Python).
    [[nodiscard]] std::string detect(std::string_view path, std::string_view firstLine = {}) const;
    /// The provider that serves `language` (the fallback for "" or an unserved language); null only when none was registered.
    [[nodiscard]] const OutlineProvider* provider(std::string_view language) const;
    /// The outline of a file: detects the language from the path and the text's first line, then asks its provider.
    [[nodiscard]] Outline outline(std::string_view path, std::string_view text) const;

private:
    struct Entry { std::string language; std::shared_ptr<const OutlineProvider> provider; int priority; };
    struct Detect { std::string language; std::vector<std::string> extensions, filenames, interpreters; };
    std::vector<Entry> entries_;
    std::vector<Detect> detects_;
    std::shared_ptr<const OutlineProvider> fallback_;
};

/// `braces@3`: the brace heuristic for C-like text (C, C++, JavaScript, TypeScript, Rust, Go, Java, C#, ...).
[[nodiscard]] const OutlineProvider& bracesProvider();
/// `indent@2`: the `def` and indentation heuristic for Python.
[[nodiscard]] const OutlineProvider& indentProvider();
/// The library's own registry: python on `indent@2`, everything else on `braces@3`. Built once; immutable.
[[nodiscard]] const Languages& defaultLanguages();

}  // namespace sub0canvas
