#pragma once
/// @file scenario.hpp
/// A scenario of the mock squad (docs/MOCK_SQUAD.md section 1): one whole attempt written by hand, as a consumer would live it. It reuses the section syntax of
/// `fixtures/replies` (`--- base`, `--- reply`, `--- want`) and adds the header keys and sections an attempt needs (actors, a gate, an outcome sequence,
/// identity expectations). Parsing is pure text work; nothing here touches the library.
///
/// ```
/// # name: g1-symbol-fix               # cites: the T0 case or intervention the scenario mimics
/// # actors: a b                       (who may send replies; the first is the default)
/// # allowed: src/calc.cpp             (files the actors may write; default: those of base and want)
/// # policy: partial=no scratch=no     (LoopPolicy overrides, `applyPolicy`)
/// # expect: failed | refused:the file has | passed      (the outcome of each reply, in order; `kind` or `kind:text it contains`)
/// --- base <path>                     (a file before; several allowed)
/// --- gate                            (rules: `test <name> passes when <path> contains|lacks <text>`, `compile <message> when <path> contains <text>`)
/// --- reply [as=NAME] [base=rN] [finish=length|stop] [pieces=N]
/// --- want <path>                     (a file after)
/// --- expect-ids                      (see `checkIds` in the contract test)
/// --- expect-store                    (under `# policy: useStore=yes`: `events N`, `items N`, `state <key part> : <state after each verdict>`; see `checkStore`)
/// ```
/// A section's text runs to the next `--- ` line. A reply loses its final newline (a cut-off reply ends mid-line).

#include <expected>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mock_squad {

/// One reply of the scripted model (mirrors the squad's `fake::Reply`: content, finish reason, characters streamed per piece) and who sent it.
struct ScriptedReply
{
    std::string content;
    std::string actor;          ///< the actor that sent it ("" is the first actor)
    bool cut = false;           ///< `finish=length`: the reply ended at the token limit
    int baseRevision = 0;       ///< `base=rN`: the author was shown revision N of every file it edits (a second actor holding a stale view); 0: the current one
    int pieces = 48;            ///< characters per streamed piece: the stream guard judges after each piece
};

/// One expected outcome of one reply: its kind, and a text the outcome's detail must contain (refusal text, runaway kind, skipped parts).
struct ExpectedOutcome
{
    std::string kind, needle;
};

struct Scenario
{
    std::string name, cites;
    std::vector<std::string> actors, allowed, gateRules, expectIds, expectStore;
    std::vector<std::pair<std::string, std::string>> policy;   ///< `key=value` overrides of the loop's policy, in the order written
    std::vector<ExpectedOutcome> expect;
    std::map<std::string, std::string> base, want;
    std::vector<ScriptedReply> replies;
};

/// Read a scenario file's text, or say what is wrong with it (an unknown section, a missing name, no reply, no expectation).
[[nodiscard]] std::expected<Scenario, std::string> parseScenario(const std::string& text);

}  // namespace mock_squad
