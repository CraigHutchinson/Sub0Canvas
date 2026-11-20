# Follow-up session for verifying prose attached to code blocks

Status is backlogged as of 2026-11-20. This brief sketches the receiving slice for the backbone's event log, claims and revision-aware source anchors. [Backbone](../BACKBONE.md) · [Runtime evidence](canvas-runtime-evidence.md) · [Coverage and assertions](canvas-test-coverage.md)

## User view

A worker reading a folded function can see a short description of the invariant that matters and a compact verification badge. The badge opens the recorded claim, its exact code and prose revisions, the checks that support it and any later change that made it stale. A change to the function or to a test already linked to the claim records that it needs checking again. A test failure also leads back to the code and relevant prose on the revision the test ran. Historical links still open the original source.

Start with an explicit, bounded claim. A comment explaining why a parser can recurse is suitable when it states a checkable condition, such as the cursor advancing before that repeated call. A short explanation of why a named numerical limit exists may need a reviewed derivation as well as boundary tests. Give each claim its own scope so one verified sentence does not imply the rest of the comment was checked.

## Code and comments in a bounded view

A focused code-and-comments read returns the exact numbered source for the selected block and revision, with inline comments in their original order. A linked prose item can appear alongside it with its anchor and verification badge. When the body stays folded, show a short, explicitly sourced comment excerpt just above the fold marker if the requested view has room. The excerpt may be a quote from the hidden source or a linked prose item. Label the origin, mark omission or truncation, and keep projected text outside numbered editable source. Opening the original always returns the actual file text.

Use the existing scoped view mechanism to select code, inline comments and optional linked items together. Keep enough room for relevant source and the response before spending the remaining budget on optional context. Let a worker request comment-only detail, the full source range, or a linked item by scope. Select default excerpts by the focused block, pending review or active failure and make other items available on request. Compare the modes on a small-context worker using prompt tokens, additional reads, chosen regions and accepted edits.

Freeform comments and other attached information use extensible layer kinds, open item data and typed links. The canvas can assemble a view from these without making each kind a fixed property of a code block. Coverage, test outcomes and traces retain their own linked layers, identity, scope and revision. Avoid embedding a copy of coverage in the prose or moving source comments out of their files. A reader can combine the layers when needed and open the exact evidence on demand.

## Verification and the journal

Use the existing item, revision anchor and link model for the prototype. A stable claim identity follows the block across moves. Bind each evidence record to the claim text revision or digest, its implementation revision and block range, the supporting test or assertion identity and revision, and the run or review that checked it. Include the checker or reviewer, outcome, and any scope or capture limits. Keep recorded evidence immutable. Keep the content identity that ran even if a later edit moves the block.

| Badge | Meaning |
| --- | --- |
| Unchecked | The claim exists and has no applicable verification evidence. |
| Check pending | A change or explicit request scheduled verification for this revision. |
| Verified | The named check or review supported the claim on these exact inputs and revisions. |
| Stale | Prior evidence exists for an older claim, implementation or supporting test. |
| Contradicted | A current observation or review demonstrates where the claim fails. |

A behavior test can verify only the part of the prose its assertion covers. Execution coverage can help select a check. It gives weaker evidence than a linked assertion with a known observation, and an ordinary green suite cannot review arbitrary prose. Allow an explicit review to verify claims a harness cannot decide. A test passing after a rewrite cannot inherit the previous decision unless the relevant claim and dependencies are matched and rechecked as needed.

Append claim changes, evidence and check outcomes through the backbone log or a prototype that replays into the same views. Derive the visible badge from the evidence and dependencies for the viewed revision. Index claim-to-test and test-to-claim links in both directions. On a code, prose or linked test change, identify the affected claims and register a check for the new versions. Preserve old evidence. When a delayed result arrives, attach it to the revisions it checked. A moved block with unchanged relevant content can retain the link. A changed body, unresolved anchor or uncertain split requires re-evaluation. Start with explicit links. Add coverage-based dependency discovery only after an end-to-end use case shows it improves check selection.

## Developer and QA handoff

When a change affects a linked claim, the author acknowledges it during the same review. Record which disposition applies and the reason.

- When the claim still holds, keep its wording and link the check or review that supports it at the new revision.
- When approved behavior changes, update the comment, linked assertions and the claim together so the next worker sees the new contract and its reason.
- When the code, prose and tests disagree and the intended behavior is unclear, record a contract gap with the conflicting evidence. Keep the claim open for an owner decision where necessary. A changed assertion by itself gives no authority to reinterpret the contract.

Start with a recorded disposition on one reviewed change. Give later workers the current wording and a way to inspect the prior disagreement. A possible later producer is an executable C++ contract. Preconditions, postconditions and contract assertions could support claims that have a suitable predicate, once a supported toolchain and checking configuration have been demonstrated. Record the selected checking semantics and whether the relevant path ran. A build can ignore a contract predicate, and a predicate does not capture design rationale. Keep the first slice on the current toolchain. [WG21 P2900R14](https://wg21.link/p2900r14)

## First receiving slice

Use the Sub0Squad C++ precedence replay and one parser function. Write one claim about cursor progress, link it to a named check with a reproducible input, and record the source revision and bounded trace. Show the claim from a folded block and from the check. Record a failing revision that contradicts the claim and a known-good revision that supports it. Then replay a code edit, a prose edit, a linked test edit and an unrelated edit. Confirm that only the affected evidence becomes stale, the original snapshots remain readable in either direction, and a late result cannot verify the new version.

Keep the first prototype in the receiving harness and use scoped layer detail to present its evidence. Measure how often invalidation is accurate and how many new checks it schedules. Decide on persistent event fields, review policies, and wider language support after the prototype is received. The broader backbone, storage schema and automatic prose verification remain future work.
