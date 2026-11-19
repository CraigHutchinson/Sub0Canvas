# Briefs for follow-up sessions

A brief is a self-contained description of one piece of work that a fresh session (or a person) picks up without the conversation that produced it.
Start a session with: "Read `docs/sessions/<file>.md` and do it." The project's state stays in [../RESUME.md](../RESUME.md) and
[../BACKLOG.md](../BACKLOG.md); a brief is for one task, and it links to the evidence rather than holding it.

## The workflow

1. **Write** one when work is found that belongs in its own session. Copy the template below, name the file after the task (`kebab-case.md`), add
   it to *Open* below, and point the backlog item at it.
2. **Pick up**: set `Status: in progress (<date>, <who>)` and work from it. Ask the owner where it is unclear rather than guessing.
3. **Close**: the result goes where it belongs (code, a design doc, RESUME, BACKLOG). The brief gets an `## Outcome` section (what was done, the
   commits and tag, what is left), moves to *Done* below, and is retired to git history in a later tidy-up.

## Template

```markdown
# Follow-up session: <task>

Status: open (written <date>). Backlog: <item>. Design: <BACKBONE.md section and slice>.

## Goal
<one paragraph: what to build or establish, and what not to do>

## What is known
<the facts the session starts from, briefly, with links>

## Steps
1. ...

## Done means
<the observable end state: which test passes, which doc says what, which measurement shows it>

## Environment and constraints
<toolchain, paths; the owner's standing rules (AGENTS.md)>
```

## Open

| Brief | Task | Written |
|---|---|---|
| [s0-scale-harness.md](s0-scale-harness.md) | S0: a synthetic corpus at any size and benchmarks of today's index against the scale budgets | 2026-11-18 |
| [canvas-runtime-evidence.md](canvas-runtime-evidence.md) | Receive on-demand trace evidence from Sub0Squad on the exact canvas revision that ran | 2026-11-19 |
| [b0-format-catch-up.md](b0-format-catch-up.md) | B0: the producer writes the viewer's optional fields; golden fixtures checked on both sides | 2026-11-18 |
| [g1-git-history.md](g1-git-history.md) | G1: a local git document adapter and per-path block history, against hand-checked truth | 2026-11-18 |
| [d0-block-discovery-seam.md](d0-block-discovery-seam.md) | D0: one outline result and a provider per language, today's heuristics behind it, the measured faults fixed | 2026-11-18 |
| [k0-ui-kpis.md](k0-ui-kpis.md) | K0: a scripted headless-browser benchmark of every viewer interaction against its budget, recorded with the machine | 2026-11-18 |
| [v0-worktree-snapshot.md](v0-worktree-snapshot.md) | V0: a `sub0canvas snapshot` of this repository's docs, Markdown blocks, Markdown rendered in the viewer | 2026-11-18 |

## Done

| Brief | Task | Closed |
|---|---|---|
| [m0-synthetic-mirror.md](m0-synthetic-mirror.md) | M0: the synthetic mirror, its fixture and edge-case register | 2026-11-18 |
