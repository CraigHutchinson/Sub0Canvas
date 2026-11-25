# Briefs for follow-up sessions

A brief is a self-contained description of one piece of work that a fresh session (or a person) picks up without the conversation that produced it. Start a session with: "Read `docs/sessions/<file>.md` and do it."
The project's state is in [../RESUME.md](../RESUME.md) and [../BACKLOG.md](../BACKLOG.md); a brief is for one task (goal, what is left, done means), and it links to the evidence rather than holding it. A brief carries no progress log.

## The workflow

1. **Write** one when work is found that belongs in its own session. Copy the template below, name the file after the task (`kebab-case.md`), add it to *Open* below, and point the backlog item at it.
2. **Pick up**: work from it. Ask the owner where it is unclear rather than guessing.
3. **Close**: on close, move the evidence (numbers, decisions, what was left) to its design doc, RESUME or BACKLOG, then delete the brief in the same change. Git history and the tags keep it.

## Template

```markdown
# Follow-up session: <task>

Backlog: <item>. Design: <BACKBONE.md section and slice>.

## Goal
<one paragraph: what to build or establish, and what not to do>

## Left
<the work, briefly, with links to the evidence>

## Done means
<the observable end state: which test passes, which doc says what, which measurement shows it>

## Environment and constraints
<toolchain, paths; the owner's standing rules (AGENTS.md)>
```

## Open

| Brief | Task |
|---|---|
| [s0-scale-harness.md](s0-scale-harness.md) | S0: the sizes beyond 10^5 lines, profile B, change sets, the index against the truth |
| [b0-format-catch-up.md](b0-format-catch-up.md) | B0: the release, the Sub0Squad PR and the real-attempt re-capture (deleted by the cleanup) |
| [v0-worktree-snapshot.md](v0-worktree-snapshot.md) | V0: Markdown blocks, Markdown rendered in the viewer, the worktree's uncommitted state, merge titles |
| [platform-migration.md](platform-migration.md) | Move Sub0Squad's platform layer to Sub0Platform, with the `getenv` fix; Squad and Canvas consume it |
| [canvas-runtime-evidence.md](canvas-runtime-evidence.md) | Receive on-demand trace evidence from Sub0Squad on the exact canvas revision that ran |
| [canvas-test-coverage.md](canvas-test-coverage.md) | Later U8: per-test coverage in source and scoped assertion evidence |
| [canvas-prose-verification.md](canvas-prose-verification.md) | Verify prose against the code and tests that support it, with revision-bound evidence |
| [canvas-source-addressing.md](canvas-source-addressing.md) | Shared source addressing for edits and diagnostics; viewpoint clones |
