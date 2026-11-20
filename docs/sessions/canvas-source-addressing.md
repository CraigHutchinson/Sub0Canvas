# Follow-up session for shared canvas source addresses

Status is backlogged as of 2026-11-20. The Sub0Squad model-free edit shootout supplies the starting evidence. [Backbone source anchors](../BACKBONE.md#5-sources-the-adapter-interfaces) · [Runtime evidence](canvas-runtime-evidence.md) · [Prose verification](canvas-prose-verification.md)

## Goal

Let an edit request, compiler diagnostic, stack frame, assertion observation or prose note refer to source through the same revision-aware resolver. A worker can start from an error, open the source that produced it, and use the current source view to prepare a guarded edit. A reader can start from source and find related evidence. Keep the relationship the producer actually observed, including distinctions among a diagnostic site, a suspected cause, a stack frame and a confirmed failing assertion.

## Resolution and authority

An incoming observation carries its producer and original source coordinates where available. Bind those coordinates to the document, path, revision, range and content identity that produced the observation. Resolve a stable block identity when possible. Keep original coordinates when a producer used generated source or an unresolved location. Source maps and compiled-artifact identities can supply the mapping later.

Use the same resolver for two different outcomes. Historical display opens the exact original revision and can also show a probable location at the current head. An edit requires an exact target on the revision it will change. An old line number alone cannot authorize a mutation. The resolver may return an exact current target when it can establish the same intended block and a unique unchanged target range. If it cannot, return the original evidence and a reason to refresh. A location estimate used to render an issue carries no edit authority.

Let a scoped view issue a short address or show a revision-guarded numeric range. Keep the complete binding server-side. Test a block-scoped content hash and a short context pair against simple ranges. A line hash alone is ambiguous where bodies repeat the same text. A repeated line can sometimes be disambiguated with its block and nearby context. If these still leave multiple choices, refuse the edit. Keep the submitted address, resolution, base revision and outcome in the transaction history. Keep coverage, prose, reviews and test evidence as separate linked items.

## Actor views and pending work

Issue a view or context identifier to each actor, human or worker. Bind it to the source revision, visible ranges, edit ownership and the actor's pending edits or scratch snapshot. A new read or scratch change can produce a new view identifier, with a link to the previous one. Retain enough of the view manifest to reconstruct what was visible and editable when the actor made the request. The same shared file can therefore have several actor-specific views at once.

Resolve an edit within the submitted view and its pending changes first. A SEARCH, line address or hash can only match in the editable ranges the actor was given. A matching line elsewhere in the file cannot broaden the target. After validating the transaction in that view, rebase its intended change onto the current shared revision. Keep unrelated edits outside the actor's viewport. If another actor changed the claimed range, a required dependency or its ownership, return the conflict and the smallest refreshed view that can resolve it. Use a revision and scope check at commit time so a successful precheck cannot race a concurrent edit.

A scratch result is evidence about the actor's scratch snapshot. It may support a local decision, and it becomes shared acceptance evidence only after the relevant revisions and dependencies are received in the merged view. Record the actor, view, base and scratch revision, resolution and commit outcome. Keep separate readable histories for the actor's request and the shared result.

Add two receiving cases to the shared-address fixture. First, give two workers different blocks in one file. Let one worker insert lines and publish while the other holds an uncommitted edit. Both disjoint changes should survive, and a duplicate outside the second worker's view should have no effect on its resolution. Second, let the actors edit an overlapping range. The second apply should identify the overlap and retain both scratch histories. Run these after the single-worker resolver cases establish the base contract.


## Clone a viewpoint at a pinned revision

Sub0Squad's first receiving consumer is a manager-led isolated retry of a production job whose current attempt appears stuck. A worker must be able to start
a new session from exactly the Canvas viewpoint the original job received, including a viewpoint saved before later source changes. Restore the source
revision or per-file revision set, canonical content identities, visible ranges and folds, edit scope, and any scratch state included in that checkpoint.
Record a versioned fingerprint of the canonical viewpoint manifest. Verify it when restoring the checkpoint and report a missing revision, unavailable
content or mismatch explicitly.

The clone has its own view and actor or session identity, scratch history and newly issued edit addresses. It records its parent view, source revision and
checkpoint fingerprint for provenance. Changes in either branch stay local to that branch until explicitly integrated. Keep checks and trace evidence bound
to the branch and revision that produced them. Canvas supplies the clone, verification and lineage. The caller decides when to fork and which result to use. Track the caller policy in [Sub0Squad #2](https://github.com/CraigHutchinson/Sub0Squad/issues/2).

Prove the contract with a Sub0Squad repair. Capture the start, advance the original view, restore the pinned viewpoint into a second session, and check that
both sessions see the expected content and writable ranges. Edit both branches independently, reject addresses issued to the other branch, and report
a mismatched or unavailable checkpoint. Verify that the receiving path can inspect both histories and integrate one candidate through the usual revision
and scope checks. Keep storage and copy strategy open until this receiving case establishes the required semantics.

## First receiving slice

Use one Sub0Squad C++ repair. Produce a compiler diagnostic from a known revision, then request its historical source and a current guarded edit. Verify the base case, an unrelated insertion, a moved block, duplicate text and a changed target. Repeat with an independently recorded test assertion or stack frame anchored to the same function. From the current function, find both original records and distinguish their evidence types. A checked-out revision can have the same body and a different source line. The views should show the original and resolved locations clearly.

Compare reply tokens and resolver outcomes in the model-free fixture first. For every form, establish that it either changes the intended text or refuses with a recoverable reason. Verify that archived errors remain readable after each mutation. Carry only the smallest address and provenance contract that the receiving harness needs into the backbone. The production edit protocol and persistence schema stay open until the fixture distinguishes the alternatives.
