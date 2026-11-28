# Block discovery: finding the blocks of any language

Status: **D0 built** (2026-11-23: the seam, `braces@3` and `indent@2`, the block-comment, name and decorator fixes); D1-D3 are design (2026-11-25). Section 2 is the limits measured against v0.2.1, and
section 2.1 says what D0 changed and what is left. Sections 4.3 to 4.6 (declarative and data providers, tree-sitter, the snapshot record) are not built. It answers the owner's note that folding needs language-aware
processing (YAML, XML, JSON, Rust, Python, C++, JavaScript, TypeScript and more), that Notepad++ has an extensible mechanism, and that existing tools
should be drawn on rather than reinvented. It extends [BACKBONE.md](BACKBONE.md) (section 14, amendment A8) and [SCALE.md](SCALE.md) (section 4, "block discovery") and
states its requirements in [REQUIREMENTS.md](REQUIREMENTS.md) (R-BLK).

## 1. Why it matters more than folding

The same function, `foldRegions`, decides three things: what a folded view hides (CANVAS.md), what `SYMBOL name` addresses (the canvas), and **what a
block is**, so what carries an identity (LAYERED_DOCUMENT.md). A wrong fold is cosmetic. A wrong block is not: identities attach to it, layers anchor to
it, and every later use case (history, PR comments, an editor's margin) inherits it. Discovery is the foundation of the foundation.

## 2. Today's limits, measured

`foldRegions(text, bool python, bool all)` is a line heuristic: braces for C-like text, `def` and indentation for Python. The language is a `bool`,
chosen by `isPythonPath` (a `.py` suffix), and threaded through five call sites (`outline.cpp`, `canvas.cpp`, `doc_layers.cpp` twice, the public
API). A probe against the v0.2.1 library (2026-11-18, `foldRegions(text, python, true)`) gave:

| input | found | what is wrong |
|---|---|---|
| C++ functions (the baseline) | `f`, `g` | right |
| Rust `fn first(x: &'a str) {` | `first` | right, by luck: the `'` of a lifetime opens a "character literal" that a second `'` happens to close |
| Rust `fn parse<T>(s: &str) -> T {` | a block with **no name** | the name is read as the identifier just before `(`, and `<T>` stops it |
| TypeScript `function id<T>(x: T): T {` | **no name** | the same |
| JavaScript `const add = (a, b) => {` | **no name** | the name is before `=`, not before `(` |
| Go `func (p *Parser) Term() float64 {` | named **`func`** | the receiver's `(` comes first |
| a `{` inside a `/* ... */` comment, then two functions | **no blocks at all** | block comments are not tracked, so depth never returns to zero |
| a C++ raw string holding `{` over several lines | `f` | right, by luck (the `"` and `)` balance) |
| Python `@staticmethod` then `def term` | `term`, from the `def` line | the decorator is left outside its block |
| YAML, JSON | nothing | data formats have no `(`, so nothing is function-like |
| (by reading the code) Python classes, nested functions, C++ classes and namespaces | not blocks | by design today: only outermost functions are blocks |

### 2.1 As built (D0, 2026-11-23)

Built: `lib/include/sub0canvas/blocks.hpp`, `lib/src/blocks.cpp` (`OutlineNode`, `Outline`, `OutlineProvider`, `Languages` with detection by file name, extension, then the `#!` interpreter; the providers `braces` and `indent`; `defaultLanguages()`),
`lib/src/scan.hpp` (shared line helpers), `foldRegionsOf` and `foldRegionsFor(path, ...)` in `outline.hpp`, `lib/tests/test_outline.cpp`. `foldRegions(text, python, all)` is a wrapper over the seam (Sub0Squad's calls build unchanged); canvas SYMBOL, the block index
and test localisation pass a path.

**The seam was byte-identical.** A dump of `foldRegions` (both `all` values), `sectionView` and `foldedView` over every source, doc and fixture file of the repository (about 3 MB) was unchanged against the previous library; `test_golden_replay`, the mock fixtures,
the viewer outputs and the benchmark metrics were untouched. The faults, one commit each, every case written first and seen failing:

| fault | provider | what changed |
|---|---|---|
| a brace or signature inside `/* */` (R-BLK-5) | `braces@2` | block comments blanked before the scan, across lines. The section 2 probe (a `{` in a comment, then two functions) did **not** reproduce: depth is relative, so a stray `{` before a function is tolerated. The fault that does reproduce is a `{` or `}` inside a comment within a function: the function never closes, so it and every later function vanish (four such cases failed before the fix). No file of the repository had it, so no output changed |
| names (R-BLK-4) | `braces@3` | generics (Rust, TypeScript), functions bound to a name (arrow functions, function expressions, typed bindings, assigned C++ lambdas), Go receivers (`Parser::Term`, so a method and a free function of one name stay two blocks) and Go type parameters, C++ `operator<` and friends. Over the repository the only output changes are about 19 distinct blocks that had an empty name and now have one; no block appears or disappears, no span moves |
| decorators (R-BLK-6) | `indent@2` | the `@name` lines directly above a `def` are inside the node (`first`); `FoldRegion::first` stays the signature line (views and SYMBOL unchanged) and `spanFirst` is the whole span, which the block index uses. No Python file of the repository carries a decorator, so no output changed |

**Cost, and the lesson.** The first pre-push performance gate blocked the push: against the committed baseline the D0 commits made `foldRegions C++ 1e5 lines` +90%, Python +15% and `index cold build 1e5 lines` +12.5% (an interleaved `compare` against the build before D0 reproduced it). The
cost was in how the seam was written, not in what it computes: whole-vector copies and a character walk in the block-comment pass, a `std::string` copy and an `istringstream` split per text, a `std::string` per character of a signature line. The fix was one change: the providers read
`string_view` lines (`scan::splitViews`, one `memchr` per line), the comment pass runs only on a text that contains `/*` and works on one copy of the buffer with every offset kept, the name helpers take views and the fold regions take names by move. Output stayed byte-identical (9.2 MB, the repository
and two sibling sources); against the build before D0 C++ is -34% and Python -57%. What the correctness fixes still cost is **not measured**: the generated corpus has no `/*`, so the blanking copy is never paid there (a text with a block comment pays one extra copy and one linear pass). A probe of
that case belongs in the next benchmark revision.

**Left (D0's faults and gaps):** a Rust lifetime on a signature line (`fn f<'a>(x: &'a str) {`) opens a "character literal" that never closes, so that function is not found; CRLF text keeps a `
` on every line, which defeats the Allman rule (a signature line followed by a lone `{`);
multi-line decorators and the doc comments and attributes of C-like text are not yet inside the span; nested functions and classes are not nodes (outlines are flat); unnamed nodes still carry an identity (skipping them is an identity-policy change: check it against the golden replay first);
R-BLK-4 is asserted for the probe cases, not for arbitrary text; `index.outliners` is not in the snapshot (it is additive, R-FMT-2, but changes `layeredJson` and so every snapshot fixture and the exact byte metrics: one reviewed batch with the format change).

A block with no name breaks two rules at once: identity's second rule (same name, new signature) and `SYMBOL name` editing. A file whose blocks
vanish orphans every item anchored to it. These are the failure modes the design below must make impossible to ship unnoticed (section 6).

### 2.2 Language registration in Git replay

The default registry names the existing brace languages and selects the indentation provider for Python, including recognised shebangs. Markdown
files use `markdown@1` for ATX headings. Sections run up to the next heading and fenced examples do not introduce headings. Setext headings
and nested Markdown sections are follow-up work. Snapshots use the registry's path detection before fetching candidates. A scoped history
read can also recognise a shebang after fetching the text.

A native client can copy `defaultLanguages()`, register an `OutlineProvider` and its detection rules, then pass the registry to `BlockIndex` or
`RevisionIndex::Options::languages`. Finish registration before replay and keep the registry fixed while any index uses it. A new provider
version can change spans and identities, so replay from source when switching versions. The block index takes spans from the outline and
indexes each child as a separate block. A line lookup prefers the smallest containing span. The native, JSON and binary projections all read
the resulting index and need no language-specific decoding.

## 3. Prior art

Listed from knowledge, not from a fresh search; versions and licences are to be confirmed before anything is adopted (the owner approves every
download, AGENTS.md).

| system | how it finds structure | what to borrow | limits for us |
|---|---|---|---|
| **Notepad++** (Scintilla editor, Lexilla lexers) | a lexer per language (about 100, C++) styles the text and sets a **fold level per line** (a depth number plus a "header" flag on the line that opens a fold); **User Defined Languages**: an XML file declares comment and string delimiters, keywords, and fold open/middle/close keywords, for any language without code; external lexers as plugins through one interface | the fold-level-per-line representation (cheap, incremental, any language); a **declarative definition** for the long tail; one interface every lexer implements | fold levels give ranges, not names or kinds; written for highlighting, so a lexer's view of "a block" is whatever folds |
| **KDE KSyntaxHighlighting** (Kate, KWrite) | XML syntax definitions for about 300 languages; rules carry `beginRegion` / `endRegion` attributes that define folds; indentation folding as a per-language switch | a large, maintained, permissively licensed **data set** of language definitions with fold regions, readable without the Qt library | regions, not symbols; reading the XML means implementing its rule engine |
| **tree-sitter** | a generated, error-tolerant, incremental parser per language (C runtime, no dependencies; grammars for well over 100 languages); **queries** over the tree: `folds.scm` (Neovim, Helix), `outline.scm` (Zed: items and their names), `tags.scm` (definitions and their names; GitHub's code navigation), `locals.scm`, and **injections** (JavaScript inside HTML, a fenced block inside Markdown) | real syntax trees with **names, kinds and nesting**; error recovery (a half-written file still has blocks); incremental reparsing (an editor, scale); the upstream `tags.scm` / `outline.scm` queries, so we write no grammar and few queries | each grammar is generated C, some large (C++), so build time and binary size grow per language; a dependency to approve; licences per grammar (mostly MIT); trees can change between grammar versions, which moves block spans |
| **Language Server Protocol** | `textDocument/foldingRange` (ranges, kind: comment, imports, region) and `textDocument/documentSymbol` (a tree of symbols: name, detail, **kind** from a fixed list of 26, `range` for the whole and `selectionRange` for the name) | the **vocabulary**: symbol kinds, and the split between a block's whole span and its name's span; an LSP-fed outline for consumers that already run a language server (E1) | needs a running server per language: a process and I/O, so never in the pure core |
| **VS Code** | without a language extension: **indentation folding**, plus `folding.markers` (start/end regexes, such as `#region`) and `offSide` (a block ends before trailing blank lines, Python-style) in a language configuration; with one: folding and symbol providers | the **fallback chain**: a real outline when available, else markers, else indentation; markers as a cheap way to name regions | regex markers find regions, not functions |
| **TextMate grammars** (VS Code, GitHub, Shiki) | regex tokenisers with `foldingStartMarker` / `foldingStopMarker` | little: regexes per line, the same weakness as today | |
| **Vim** `foldmethod`, **Emacs** imenu, hideshow, `treesit` | fold by indent, marker, syntax or an expression; imenu lists definitions per mode; Emacs 29 builds tree-sitter in | the taxonomy (indent, marker, syntax, expression); that two mature editors converged on tree-sitter | |
| **Universal Ctags** | about 150 languages, hand-written and regex parsers (**optlib** declares new ones without code); emits name, kind, scope, line, end line, signature | the **kind and scope** fields; a second, independent opinion to check our outlines against on real repositories (S3) | GPL: usable as an external tool for checking, not linked |
| **GitHub Linguist** | detects the language by modeline, filename, shebang, extension, then heuristics and a classifier; `languages.yml` maps them | the **detection order** and its data, instead of a `.py` suffix test | Ruby; the data file is what is useful |
| **JSON Pointer** (RFC 6901), **XPath**, Markdown heading anchors | standard ways to name a place in structured data and documents | **names for blocks of data formats**: `/spec/containers/0`, `/project/dependencies/dependency[2]`, `#install` | |
| **GumTree**, **difftastic** | tree differencing with move and rename detection (difftastic uses tree-sitter) | reconciliation over trees later (LAYERED_DOCUMENT.md, "Prior art"), and further evidence for tree-sitter as the substrate | |

**What the prior art agrees on.** (1) Discovery is a per-language plug-in behind one interface (Lexilla, LSP, tree-sitter, ctags). (2) There is a
fallback chain from a real parser down to indentation (VS Code, Vim). (3) The long tail is served by declarations, not code (Notepad++'s UDL,
KSyntaxHighlighting's XML, ctags' optlib). (4) Names and kinds come from a parser or from queries over one (LSP, tree-sitter's `tags.scm`); fold
levels alone do not give them. (5) Language detection is its own step (Linguist).

## 4. The design

### 4.1 One result, the outline

Every provider returns the same thing: a tree of **outline nodes**.

```cpp
struct OutlineNode {
    std::string kind;              // the LSP symbol-kind vocabulary, lower-case: "function", "method", "class", "namespace", "module", "struct",
                                   // "interface", "enum", "constant", "property" (a key), "field"; plus "heading" (Markdown), "element" (XML),
                                   // "region" (a marker region), "test" (a TEST(...) or test_ function)
    std::string name;              // the leaf name, never empty for a block that can carry an identity (a provider that cannot name a node marks it
                                   // anonymous: kind kept, name "", never a block for identity; section 6, R-BLK-4)
    std::string qualifiedName;     // the path to it: "Parser::term", "spec.containers[0]", "Install/From source"
    int first = 0, last = 0;       // the whole span, 1-based lines, decorators and doc comments included (LSP `range`)
    int nameLine = 0;              // where its name or signature is (LSP `selectionRange`)
    int bodyFrom = 0, bodyTo = 0;  // what a fold hides (0 when nothing can be hidden)
    std::string signature;         // the header, whitespace collapsed: identity rule 1 compares it
    std::vector<OutlineNode> children;
};
struct Outline { std::string language, provider; std::vector<OutlineNode> nodes; bool partial = false; };   // partial: syntax errors were recovered
```

Folding and identity are **two policies over one outline**: a fold policy picks the nodes whose bodies may be hidden (today: outermost functions,
bodies of three lines or more); an **identity policy** per language picks the nodes that carry identities (today: function-like nodes at any depth
that `all` reaches; for YAML, perhaps mappings two levels deep; for Markdown, headings). The canvas's `SYMBOL name` resolves against the qualified
name, so `Parser::term` and a free `term` can no longer be confused.

### 4.2 Providers, chosen per path

```cpp
class OutlineProvider {
public:
    virtual ~OutlineProvider() = default;
    [[nodiscard]] virtual std::string name() const = 0;                       // "braces@1", "indent@1", "treesitter-rust@0.23", "yaml@1"
    [[nodiscard]] virtual Outline outline(std::string_view text) const = 0;   // pure, total (never throws), deterministic, O(n)
};
class Languages {                                                              // the registry
public:
    void add(std::string language, std::shared_ptr<const OutlineProvider>, int priority);
    [[nodiscard]] std::string detect(std::string_view path, std::string_view firstLine) const;   // Linguist's order: modeline, filename, shebang, extension
    [[nodiscard]] Outline outline(std::string_view path, std::string_view text) const;          // the best provider, falling back down the chain
};
```

The chain, best first: a **tree-sitter** provider where one is built in for the language; a **dedicated** provider for a well-specified data format;
a **declarative** provider (section 4.3); the **braces** or **indentation** heuristic (today's code, kept as the last resort, so no language is
ever worse than today); nothing (the file is loose text). The core stays pure: every provider is a function of the text. LSP outlines (E1) come in from
outside as an `Outline` value, never by the core calling a server.

`foldRegions(text, python, all)` stays as a wrapper over the heuristic providers, because Sub0Squad calls it 19 times; new code passes a path.

### 4.3 Declarative providers for the long tail

A language definition as data (JSON, read once), in the spirit of Notepad++'s user-defined languages and KSyntaxHighlighting's regions:

```json
{ "language": "cmake", "extensions": [".cmake"], "filenames": ["CMakeLists.txt"],
  "comments": { "line": ["#"], "block": [["#[[", "]]"]] }, "strings": [["\"", "\"", "\\"]],
  "blocks": [ { "open": "^\\s*function\\s*\\(\\s*(?<name>\\w+)", "close": "^\\s*endfunction\\b", "kind": "function" },
              { "open": "^\\s*macro\\s*\\(\\s*(?<name>\\w+)",    "close": "^\\s*endmacro\\b",    "kind": "function" } ],
  "offside": false }
```

Strings and comments are lexed first, so a keyword or brace inside them never opens a block (the failure in section 2). This serves CMake, shell,
SQL, Makefiles, INI and TOML, Lua, Fortran and anything a user adds, with no compiled code.

### 4.4 Data formats and documents

Small dedicated providers, because the formats are specified exactly and their naming is standard:

| format | nodes | qualified name |
|---|---|---|
| JSON (and JSONC) | objects and arrays as `property` nodes | JSON Pointer: `/spec/containers/0/image` |
| YAML | mappings and sequences by indentation; block scalars (`|`, `>`) kept whole; one tree per `---` document | the same pointer form, with `#n` for the n-th document |
| XML and HTML | elements | `/project/dependencies/dependency[2]`, or `#id` when the element has an `id` |
| Markdown | headings by level (ATX and Setext); fenced code blocks as children, **outlined in their own language** (an injection) | the heading path: `Install/From source` |
| TOML, INI | tables and sections | `tool.ruff`, `[section]` |

Markdown matters beyond code: the backlog and these design docs become layers anchored to headings (BACKBONE.md section 8: "docs/BACKLOG.md as a
markdown item source"), and a prose-first reader (U9) needs headings as blocks.

### 4.5 tree-sitter as an optional backend

A separate target (`sub0canvas::treesitter`), as the GitHub adapter is, so the core never needs it and Sub0Squad links it only when it chooses. Per
language it holds a grammar and a query; where an upstream `tags.scm` or `outline.scm` exists it is used as is, so the project writes queries only
for kinds those miss. Languages are added **by measurement**: S3's real repositories, compared against the heuristic and against Universal Ctags,
show which languages the heuristic gets wrong enough to justify a grammar. Error recovery means a file in the middle of an edit (the normal state for
an agent or an editor) still has its blocks, marked `partial`.

**This is a decision for the owner** (BACKBONE.md section 11, question 8): it is a download and a build-time cost per grammar.

### 4.6 What a change of provider does to identity

Moving a language from the heuristic to tree-sitter changes spans (decorators now inside, nested methods now blocks). Identities must survive it:
the snapshot records the provider of each language (`index.outliners: {"cpp": "braces@1"}`, BACKBONE.md section 7), and when it changes, the next
update reconciles old blocks with new ones by the usual rules (signature, then name, then body), logging `kept` or `resigned`, never retiring a block
just because the provider changed. A block the old provider did not see is `added`; one it saw and the new one does not is reported, not silently
retired, so the change is reviewed.

### 4.7 Who decides: structure is derived, identity granularity is the document's, folding is the caller's

Today one call (`foldRegions(text, python, all)`) answers three questions, and its `all` flag lets a caller change the answer to the second. They have
different owners, and mixing them is what would make identities depend on who asked:

| question | decided by | why | where it lives |
|---|---|---|---|
| **What exists**: the blocks and sub-blocks of a text (the outline) | **derived** from the content by the language's provider; never a caller's choice | identities attach to blocks; if a caller could change what a block is, two callers would see two documents with different ids, and every layer would anchor differently depending on who wrote it | a pure function of (text, language, provider version); the provider is recorded in the snapshot (4.6) |
| **What carries an identity**: which outline nodes are blocks in the index (functions only, or methods too; YAML mappings to depth 2; Markdown headings to `###`) | **the document's policy**, per language, set once in the document's configuration | one document must have one identity space; changing granularity is a migration, reconciled and reported like a change of provider (4.6), never a per-request option | the document configuration (`.sub0canvas/config.json` or the store's first events), recorded in the snapshot (`index.identity: {"python": ["function", "method"]}`) |
| **What is shown**: which nodes are folded or open, to what depth, within what budget | **the caller**, per view: a person clicking, an agent's READ request, a budget, the focus from layers (the anchors of the reader's inbox open their blocks) | presentation must never change the document; two readers looking differently at one document is normal | a parameter of the view (`ViewSpec`: open nodes, depth, budget, focus query); remembered, if at all, as a reader's view state, never as an event on the document |

Two consequences:

* **An author shapes structure through the content, not through the API.** An explicit region (`// #region parsing` ... `// #endregion`,
  `<!-- block: install -->` in Markdown) is part of the text, so it is reviewed, versioned and seen by every reader alike; the provider derives a
  `region` node from it. An agent or a person who wants different structure proposes a text change or a policy change (both reviewable), never a
  private override.
* **`all` stops being a caller's flag.** "Worth folding" (bodies of three lines or more) is a fold policy of the view; "addressable by name" is every
  node of the outline. `SYMBOL name` and identities use the outline; folded views apply the caller's fold policy on top. `foldRegions(text, python,
  all)` remains as a compatibility wrapper (Sub0Squad), and new code asks for the outline and a view separately.

R-BLK-11 and R-BLK-12 (REQUIREMENTS.md) state this as checks: the same text gives the same outline and the same identities whatever view is asked for,
and no view request changes the document or its history.

## 5. Order of work

1. **D0, the seam** (no new dependency): `OutlineNode`, `Outline`, `OutlineProvider`, `Languages`; today's heuristics as `braces@1` and `indent@1`;
   every internal caller passes a path; `foldRegions` becomes a wrapper; byte-identical output on every existing test and on the golden replay. Then
   fix the measured faults of section 2 inside the heuristic providers (block comments, generic names, arrow functions, Go receivers, decorators),
   each with a conformance case. Built: section 2.1.
2. **D1, the conformance corpus**: `fixtures/languages/<language>/*.{ext}` with a hand-checked `outline.json` each (R-BLK-7), for C++, C, Python,
   JavaScript, TypeScript, Rust, Go, Java, C#, JSON, YAML, XML, Markdown, CMake. Any provider runs against it; a provider is adopted for a language
   only when it scores at least as well as the one it replaces.
3. **D2, data formats and declarative definitions**: the dedicated providers of 4.4 and the definition reader of 4.3.
4. **D3, tree-sitter** (after the owner's answer and S3's numbers): the optional target, languages chosen by measurement.

D0 should land **before S1 and G1** build further on `foldRegions`: both replay many files and would carry today's faults into history.

## 6. Requirements (summarised; REQUIREMENTS.md, R-BLK, is the list)

Every provider, whatever its technique: deterministic; total (any input, never throws; `partial` on errors); linear; nodes nest (a child inside its
parent, siblings disjoint, spans within the file); a node that carries an identity has a name; strings and comments never open a block; names stable
under body edits; decorators and doc comments inside the span. The conformance corpus checks all of these per language, so swapping a provider is a
measured, test-gated change, not a hope.
