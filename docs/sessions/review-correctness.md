# Follow-up session: canvas rebasing and bounded snapshots

Backlog: [Correctness repairs](../BACKLOG.md#ordered-list). Requirements: R-CAN-4 and R-FMT-1 in [REQUIREMENTS.md](../REQUIREMENTS.md).

## Goal

Preserve unrelated edits during rebase and make a bounded Git snapshot show only the file versions it can include at each commit.

## Left

* `Canvas::apply` records the envelope of a transaction’s operations. A stale edit in the unchanged gap between two operations is rejected. Keep
  transaction history while tracking the spans and shifts needed for rebase.
* `sub0canvas snapshot` leaves earlier text and blocks visible if a file’s replacement blob exceeds the byte limit or becomes binary. Retire or
  explicitly mark unavailable paths for that turn. Report omissions and check that an eligible later blob can return.
* Snapshot capacity is checked before deletions take effect. Apply deletions before admitting new paths so a file that replaces a deleted one is
  eligible in the same turn.

## Done means

A regression test covers a stale edit in the gap between disjoint operations with distinct line deltas over successive revisions. An actual
overlap still reports the conflicting author and revision. A temporary Git repository checks the contents, blocks and omission reporting of
each turn when a file becomes oversized or binary and when a file replaces a deleted path at capacity. The quick suite and viewer tests pass.

## Environment and constraints

Follow [AGENTS.md](../../AGENTS.md). These tests use local fixtures.
