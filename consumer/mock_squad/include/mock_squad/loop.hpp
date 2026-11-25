#pragma once
/// @file loop.hpp
/// The mock squad's reference loop (docs/MOCK_SQUAD.md section 1): the squad's canvas loop reduced to what touches the library, on the PUBLIC API only, in
/// the order the squad uses it. A scripted model sends replies; each reply goes through the stream guard (`detectRunaway`, `detectEditOverrun`), the
/// continuation rules (`cutOffInsideEdit`, `continuationPrompt`, `joinContinuation`, `completeCutFile`), the reads (`answerRead`, `layerDetail`), the parse
/// and application of the transaction (`parseCanvasReply`, `applyReply`, with `ApplyOptions::baseRevision` for an author holding a stale revision), the compile check, the
/// commit, the gate, `revertTo` when a revision is worse, and the verdict through `BlockIndex`, `parseIssues`, `layerSummary`, `layerInline`, `layerFocus`.
///
/// What the squad decides by policy (how many free refusals, the narrow scope, the verdict clip) is a field of `LoopPolicy`, so it is named once and a
/// later `mock-loop@1` policy document can supply it (AGENTS.md: data-driven). Every one is a candidate for MS2's question: generic, or the squad's own?
/// Not mirrored in MS0: the prefill continuation (the squad's first choice when the context allows; the join below is its fallback and the path after a
/// loop), thinking and plan turns, concerns, the escalation to a restructure, the folded view the next message carries, forked sessions.

#include "sub0canvas/turn_control.hpp"
#include "mock_squad/scenario.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mock_squad {

/// MS4: whether the loop keeps claims (BACKBONE.md section 3.5). `on`: claim, release, expire and sync steps run, and the claim files are persisted; `enforce`: a reply that
/// changes a block another actor holds is refused before it is committed, with the library's own refusal text.
enum class ClaimsMode { off, on, enforce };

/// The squad's constants and switches (`canvas_agent.cpp`), by their meaning.
struct LoopPolicy
{
    int repairTurns = 12;          ///< turns spent before the attempt stops: a commit costs one, refusals and stops past their free allowance cost one
    int freeRejects = 3;           ///< refusals answered without spending a turn (kRejects)
    int freeAborts = 2;            ///< stream stops answered without spending a turn
    int freeReads = 2;             ///< read replies answered without spending a turn; later ones are refused
    int freeContinuations = 3;     ///< replies cut off at the token limit that are continued
    bool partial = true;           ///< `applyReply` partial (the squad's choice: one bad edit never costs the good ones beside it)
    bool scratch = true;           ///< apply to a copy and keep it only on success, as the squad does; false applies to the canvas itself (what an atomic library must survive)
    bool useStore = false;         ///< MS3: every verdict also goes through a `Store` over a `MemoryLog` (`putLayer` reconciles the issues layer by key), the verdict is built from what the
                                   ///< store holds, and the persisted attempt gains `store/events.ndjson` and `store/snapshot.json`. Off by default, so existing attempts are unchanged
    bool narrowScope = true;       ///< a FILE over a file that exists and is not a stub is refused (edits first, `scopeRefusalFor`)
    ClaimsMode claims = ClaimsMode::off;   ///< MS4: one `Store` replica per actor plus the loop's, claims folded through `registerClaimReducers(ClaimPolicy::standard())`; off: no claim call is made and
                                           ///< no claim file written, so every earlier attempt is unchanged. `enforce` requires `scratch=yes` (the refusal is judged on the copy)
    // MS5 has no policy field: `# views:` in the scenario switches the session side on (it needs scratch=no and claims=off), `loop.cpp` `openSessions`.
    int issueMode = 1;             ///< 0 off, 1 always the layer, 2 adaptive (SUB0SQUAD_ISSUE_LAYER; the squad defaults to 2, the mock to 1 so every verdict uses the library)
    int inlineIssues = 3;          ///< issues shown in full (SUB0SQUAD_ISSUE_INLINE)
    std::size_t adaptiveIssues = 6;   ///< adaptive mode: the layer is shown above this many failures
    int maxEditLines = std::numeric_limits<int>::max();   ///< `detectEditOverrun`'s edit size limit (the squad passes no limit: tokens bound it)
    sub0canvas::RunawayLimits limits;                     ///< the stream guard's limits (library defaults)
};

/// Set one policy field from a scenario's `# policy: key=value`; the message when the key or the value is not understood.
[[nodiscard]] std::expected<void, std::string> applyPolicy(LoopPolicy& policy, const std::string& key, const std::string& value);

/// What one reply came to. A view's control steps (MS5) are `since`, `advanced` (applied, refused, anchors, the new pin) and `refreshed`. A control step has its own kinds: claimed, claim-refused, released, expired (the claims it wrote down), settled (a sync). `kind` of a reply: read, read-refused, cancelled, continued, empty (no transaction), refused, staged (kept, does not compile), committed
/// (files still missing), failed (gate fails), reverted (worse than the best, undone), passed. `text` is the detail a scenario may name: the refusal, the
/// runaway kind and what repeated, the parts skipped, the failing count. `verdict` is what the next message tells the model; `focus` the text a folded view reads.
struct Outcome
{
    int call = 0;
    std::string actor, kind, text, verdict, focus;
};

/// One block as a turn file names it, by what a person would call it (the path and the function's last name), and its identity.
struct IdAt
{
    std::string path, leaf, id;
};
/// One issue as a turn file lists it.
struct IssueAt
{
    int number = 0;
    std::string key, title;
    std::vector<std::string> anchors;   ///< "path leaf" of the block each anchor resolves to at that turn ("path" alone for text outside every block)
};
/// The state of the layers at one turn (`layers/turn-N.json`, N = the replies handled so far): what the checks of identity read.
struct LayerTurn
{
    std::vector<IdAt> blocks;
    std::vector<IssueAt> issues;
};

/// One claim as the loop's merged replica holds it after a step (MS4): what `expect-claims` reads.
struct ClaimRow
{
    std::string id;                    ///< `ns/writer:lamport`
    std::string holder;                ///< `ns/writer`
    std::string label;                 ///< what it was taken on as a person names it: `path:leaf` for a block, `path` for a file
    std::string status;                ///< the fold's status name: live, released, expired, retired or refused
    std::int64_t expires = 0;          ///< `at + ttl`
    bool liveAt = false;               ///< live at the step's instant (status live and the instant inside `[taken, expires)`)
    std::vector<std::string> covers;   ///< what it covers now, `path:leaf` labels (a merged block's claim covers the survivor)
};
/// The claims after one step: `step` is the position in the scenario's steps, `at` the instant the step ran at.
struct ClaimStep
{
    int step = 0;
    std::int64_t at = 0;
    std::vector<ClaimRow> claims;
};

/// One refresh a tip worker asked for (MS5): what the contract compares across the four forms at one position (R-PIN-6, R-PIN-7).
struct RefreshRow
{
    int step = 0;                ///< the position in the scenario's steps
    std::string actor, form;     ///< the view's owner and the form delivered (`brief`, `elided`, `compacted`, `full`)
    bool ack = false;            ///< the refresh acknowledged its answer: the view moved to its position
    bool degraded = false;       ///< the budget forced a step towards `brief`
    std::size_t files = 0, events = 0;   ///< what the delta held, whichever form was delivered
    std::size_t briefBytes = 0;  ///< the size of the `brief` the answer carries
    std::string to;              ///< the position the answer stands for (`positionJson`)
    std::string json;            ///< the `view-delta-1` answer
};

/// One attempt, run to its end.
struct Attempt
{
    std::string startVerdict;                          ///< what the first message says about the starting files (empty when they pass)
    std::vector<Outcome> outcomes;                     ///< one per step run (a reply or a control step)
    std::map<std::string, std::string> files;          ///< the canvas at the end
    std::map<std::string, int> revisions;              ///< and each file's revision
    std::map<int, LayerTurn> layers;
    std::map<std::string, std::string> record;         ///< the persisted attempt: path under the attempt directory to bytes, in the squad's format plus `mock-trace.json`
    std::set<std::string> libraryCalls;                ///< the public functions the loop called, by name: what the consumer contract's coverage reads
    std::vector<std::string> violations;               ///< invariants that failed during the run (a refusal that changed the canvas, an anchor that does not resolve)
    std::vector<std::map<std::string, std::string>> storeStates;   ///< `useStore`: after each verdict, every issue the store holds (key to state), the hand-checked truth of `expect-store`
    std::vector<ClaimStep> claimStates;                ///< `claims` on: after each step run, the claims the merged replica holds (the hand-checked truth of `expect-claims`)
    std::vector<RefreshRow> refreshes;                 ///< `views`: every refresh a tip worker made, in order (the hand-checked truth lives in the contract's `checkViews`)
    std::size_t storeEvents = 0;                       ///< `useStore`: the events the store made
    bool verified = false;                             ///< the gate passed
    int commits = 0, refused = 0, reads = 0, aborted = 0, continued = 0, reverted = 0, staged = 0;   ///< the squad's per-attempt counts (`agent.json`)
    std::size_t unusedReplies = 0;                     ///< scripted replies never sent (the attempt ended first; control steps still run)
};

/// Run a scenario's scripted replies through the loop and return the attempt, or why the scenario cannot run (an unknown actor, a bad gate rule or policy).
[[nodiscard]] std::expected<Attempt, std::string> runScenario(const Scenario& scenario);

}  // namespace mock_squad
