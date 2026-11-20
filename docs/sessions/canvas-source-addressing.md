# Follow-up session for shared canvas source addresses

Status is backlogged as of 2026-11-20. The Sub0Squad model-free edit shootout supplies the starting evidence. [Backbone source anchors](../BACKBONE.md#5-sources-the-adapter-interfaces) · [Runtime evidence](canvas-runtime-evidence.md) · [Prose verification](canvas-prose-verification.md)

## Goal

Let an edit request, compiler diagnostic, stack frame, assertion observation or prose note refer to source through the same revision-aware resolver. A worker can start from an error, open the source that produced it, and use the current source view to prepare a guarded edit. A reader can start from source and find related evidence. Keep the relationship the producer actually observed, including distinctions among a diagnostic site, a suspected cause, a stack frame and a confirmed failing assertion.

## Resolution and authority

An incoming observation carries its producer and original source coordinates where available. Bind those coordinates to the document, path, revision, range and content identity that produced the observation. Resolve a stable block identity when possible. Keep original coordinates when a producer used generated source or an unresolved location. Source maps and compiled-artifact identities can supply the mapping later.

Use the same resolver for two different outcomes. Historical display opens the exact original revision and can also show a probable location at the current head. An edit requires an exact target on the revision it will change. An old line number alone cannot authorize a mutation. The resolver may return an exact current target when it can establish the same intended block and a unique unchanged target range. If it cannot, return the original evidence and a reason to refresh. A location estimate used to render an issue carries no edit authority.

Let a scoped view issue a short address or show a revision-guarded numeric range. Keep the complete binding server-side. Test a block-scoped content hash and a short context pair against simple ranges. A line hash alone is ambiguous where bodies repeat the same text. A repeated line can sometimes be disambiguated with its block and nearby context. If these still leave multiple choices, refuse the edit. Keep the submitted address, resolution, base revision and outcome in the transaction history. Keep coverage, prose, reviews and test evidence as separate linked items.

## First receiving slice

Use one Sub0Squad C++ repair. Produce a compiler diagnostic from a known revision, then request its historical source and a current guarded edit. Verify the base case, an unrelated insertion, a moved block, duplicate text and a changed target. Repeat with an independently recorded test assertion or stack frame anchored to the same function. From the current function, find both original records and distinguish their evidence types. A checked-out revision can have the same body and a different source line. The views should show the original and resolved locations clearly.

Compare reply tokens and resolver outcomes in the model-free fixture first. For every form, establish that it either changes the intended text or refuses with a recoverable reason. Verify that archived errors remain readable after each mutation. Carry only the smallest address and provenance contract that the receiving harness needs into the backbone. The production edit protocol and persistence schema stay open until the fixture distinguishes the alternatives.
