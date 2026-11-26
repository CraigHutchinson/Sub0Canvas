# Plan: a gold-standard scale dataset (local only) and an anonymised replicator (committable)

Expands [scale-extraction.md](scale-extraction.md) into ordered work. Status: plan, not started. Grounded in [SCALE.md](../SCALE.md) section 6, [MIRROR.md](../MIRROR.md), [BACKBONE.md](../BACKBONE.md) section 6, [CHANGE_VIEWS.md](../CHANGE_VIEWS.md) section 8, [K0.md](../K0.md) and [TESTING.md](../TESTING.md) ("Scale-run minimums").

AGENTS.md says nothing leaves the machine, so every agent stage below runs on a local model by default, and the pipeline must finish with no agent at all.

## 1. Goals

* **G-A**: one command extracts a named area and window of profile B into git-ignored `out/gold/<run>/`. Every stage writes a manifest (input hashes, output hashes, counts, tool versions, timings), so a run resumes and can be purged on demand.
* **G-B**: a separate program builds a synthetic git repository, forge JSON, tracker JSON and a viewer bundle from a committed profile and a seed. Its metrics match gold within the tolerances in section 6, and it scales or focuses by parameter.
* **G-C**: the library and viewer slices this work exposes, each shippable alone and judged on gold locally and on the replica in CI.

## 2. Architecture

```
profile B --(git, forge CLI read-only, tracker read-only)--> tools/gold/ --> out/gold/<run>/ (raw, joined, anchored, measured)
                                                                   | distil (deterministic) + leak check + owner review
                                                                   v
                                                fixtures/profile/b-<n>.json   (the only thing that leaves out/)
                                                                   |
tools/replica/ (seed, --scale, --focus) --> out/replica/<seed>/ repository + forge.json + tracker.json --> sub0canvas snapshot --> bundle
                                                                   |
                                        tools/shape/ (neutral measurer: bundle -> metrics.json), used by both sides
```

* **Separate programs.** `tools/gold/` and `tools/replica/` share only `tools/profile/schema.js` (the closed schema and its fixed bucket edges) and the neutral `tools/shape/` measurer, which reads bundles only. A quick-tier test scans imports and fails if either side imports the other. `tools/replica/` may reuse the mirror's lexicon and its fast-import writer.
* **Paths.** gold refuses an `--out` outside `out/gold/`. The area is an alias that `repos.json` resolves, so no private path reaches argv history or a committed script. The extractor is generic and names nothing private.
* **Same pipeline on both sides.** The replica goes through the real `sub0canvas snapshot` and the library, so block counts and event kinds measure the product, not the generator.

## 3. Profile schema (`replica-profile-1`)

* **Closed.** Numbers, and strings drawn from enums the schema declares. No free strings, no hashes (salted ones too: equality across profiles and dictionary attacks on short identifiers leak), no per-file records. Bucket edges are fixed in the schema (log2 for sizes, steps of 0.05 for ratios), never derived from data.
* **k-anonymity.** A bin below k (default 5) merges into its neighbour. The top bin is open-ended (`>=4096`). Counts above 100 round to 2 significant figures. Time is relative days only.
* **Sections.**
  * `tree`: depth histogram, files per directory, kind mix (braces-code, indent-code, text, sidecar, binary).
  * `files`: lines, blocks per file.
  * `blocks`: span lines, nesting depth, children, signature token count.
  * `commits`: count, files, blocks and hunks per commit, inter-arrival, merge fraction.
  * `blockHistory`: commits per block, hunks per edit, partial-edit fraction (hunk inside the block, not the whole block), hunk span as a fraction of the block, Jaccard of consecutive versions, event-kind counts (born, edited, moved, renamed, merged, retired, revived), co-touched blocks per commit.
  * `prs`: commits per PR, threads per PR, comments per thread, thread-anchored fraction, PRs per block, body length.
  * `tickets`: frames per ticket, frame depth, template recurrence, tickets per block, anchor-kind counts (frame, symbol, ref), claim outcomes (confirmed, refuted, unchecked), ports.
  * `workPatterns`: the procedural model of how people work in the area, not what they wrote. Rework (a block edited again within 1, 7 and 30 days of its last edit; edit-then-revert pairs; a retire followed by a revive), overlap and conflict (blocks touched by two or more open changes at once; merge-time conflict fraction; rebase-forced re-edits), access (which fraction of blocks take which fraction of edits, a Zipf-like exponent per file kind; a hot-core versus long-tail split; a time-of-week activity shape), coupling (blocks co-edited with the same partner across commits; file pairs that change together), review rework (commits added after a review thread opened, threads that cause an edit to the thread's block), and defect feedback (a ticket followed by an edit to the blocks its frames name, with the lag in days). All are fitted distributions with fixed parameter ranges, not event lists.
  * `text`: token-length and line-length histograms, comment fraction.
  * `targets`: the gold metrics the replica must hit at scale 1.
* The replica is the stronger anonymisation path because it needs no event list: the work patterns above are fitted once from gold and the replica re-creates the rework, conflict and access behaviour by simulation (section 5). The exact ordinal skeleton (which commit hit which block, in order) is a fingerprint and stays in `out/`, driving local replay only.

## 4. Gold stages (`node tools/gold/run.mjs --area <alias> --since <date> [--stage X] [--resume]`)

| stage | kind | input | output | acceptance |
|---|---|---|---|---|
| G0 churn census | det | git log, 30 days, `--name-only` | churn.tsv (directory, commits, files) | run once and kept; an area with 100 or more commits and 300 to 3,000 files |
| G1 history | det | area, window | commits.ndjson (first parent, merges, parsed merge header: PR number, branch) | count equals `git rev-list --count`; every merge header parsed or listed |
| G2 replay + hunk join | det | `sub0canvas snapshot` blocks per turn; `git diff -U0` | block-events.ndjson with block-relative hunk spans | every hunk lands in a block or an explicit between-blocks bucket; totals reconcile with the diff's line counts |
| G3 forge fetch | det | PR numbers from G1 | raw/gh/*.json (pull, reviews, review comments with path, line, commit) | owner approves the first run; calls carry only repository and PR numbers; rate-limited; cached |
| G3a thread digest | agent (optional) | one thread's JSON | `{prRef, threadId, topic enum, summary <= 300 chars}` | schema-valid; used in the gold viewer only, never in the profile |
| G4 tracker fetch and parse | det | keys found in commits and PRs; area symbols and paths | tickets.ndjson; frames (path, symbol, line); templates (numbers, hex, paths, quotes masked) | every key in a commit or PR is fetched or listed as unreachable |
| G4a evidence consolidation | agent | a group of raw messages or traces the regex parse left ambiguous | `{groupId, template, memberIds[], frames[]}` | the template's regex matches every member; each frame's path exists at the named revision; otherwise rejected and the deterministic grouping stands |
| G5 anchoring | det | frames, symbols, refs, `RevisionIndex` | anchors.ndjson `{ticket, block, via, revision}`, ambiguous.ndjson | a frame resolves through the source-anchor resolver; a symbol only on an exact substring of the block's text |
| G5a ambiguity and claims | agent | ticket text plus candidate block ids and texts | `{ticket, choice: candidate id or none, claims: [{kind enum, block, quote}]}` | the choice is one of the candidates; each quote is a substring of the ticket; a claim's outcome is set later by deterministic checks, never by the agent |
| G6 bundle and measure | det | G2 to G5 | gold.bundle.json, metrics.json (counts, histograms), K0 samples | scale-check passes the new minimums; the viewer opens on the latest turn |
| G7 distil and leak check | det | metrics, histograms | profile candidate | every check in section 7 passes; the owner reviews the diff before it is copied to `fixtures/` |

**Agent contract.** Prompt templates are versioned files in `tools/gold/prompts/` and hold no private text; private text enters only as the record at run time. Cache key: sha256 of stage, template hash, record hash, model id and parameters, stored in `out/gold/cache/agent/`, so a rerun is byte-identical; a miss with `--no-agent` uses the deterministic fallback. Agent output reaches the profile only as counts (group sizes, anchor kinds, claim outcomes).

## 5. Replicator (`node tools/replica/cli.js --profile <f> --seed N [--scale s] [--focus area|change:<shape>|window:<days>|ticket:<shape>]`)

* **R1 tree and blocks.** Brace-language word soup in which only an allowlist of language keywords survives, so the braces provider finds the intended blocks. Spans and nesting come from the histograms.
* **R2 history, as a simulation of work.** Synthetic authors pick blocks by the fitted access distribution, open changes that overlap in time, rework their own blocks at the fitted lags, revert, rebase and conflict at the fitted rates, and fix ticketed blocks after the fitted delay. Rework, overlap, conflict and co-edit counts therefore arise from the model and are measured against gold, instead of being copied from it. Each commit is planned as (blocks, hunks, block-relative spans). An edit is built to reach a target Jaccard (swap k of n tokens). Merge commits carry synthetic PR headers.
* **R3 forge and tracker JSON.** The raw shapes of G3 and G4. Threads anchor to block lines at the commit; tickets carry frames that resolve to synthetic blocks, templates with recurrence counts, generated keys, and claims with planted refutations.
* **R4 subsetting.** `--scale` multiplies files and commits. `--focus` keeps the neighbourhood: the blocks a change, ticket or window touched, plus their history back to the start of the window. Profile targets scale linearly (`scaleTargets()` in the schema module).

## 6. Tolerances (replica at scale 1 against gold locally, against `targets` in CI)

* Totals (files, blocks, commits, PRs, tickets): within 10%.
* Event-kind shares: within 3 percentage points.
* Histograms (commits per block, hunks per block, fan-out, frames per ticket): total variation distance at most 0.10, and p50 and p90 within 15%.
* The top bucket of commits per block is occupied.
* Viewer: DOM nodes within 15% and bundle bytes within 20% (deterministic); parse time and click latency within 30% (local only, same host, foreground window).

## 7. Anonymisation enforcement

1. **Local private-terms guard** on commit, commit message and push (already wired). G7 writes the area's distinctive identifiers (tokens of 5 or more characters absent from a public English and keyword list, the most frequent N) to a local-only directory the guard also reads.
2. **Deliberately excessive masking.** The anonymiser does not try to keep text plausible. Every identifier, string, comment, path segment, commit subject, ticket title and author is replaced by a seeded random word from the replica lexicon (one consistent map per run, so renames and moves still line up); language keywords that are not needed to find blocks are replaced as well, so the code's intent is not recoverable from its vocabulary. Only the structure the library reads survives: braces and indentation, line counts, spans, nesting, token counts and the similarity between versions. The map is discarded after the run.
2b. **Leak scanner** (`tools/leak/scan.js`) over every artifact bound outside `out/`: (a) schema-closed validation, any non-enum string fails; (b) the guard's text mode; (c) every token of 4 or more characters comes from the replica lexicon or the keyword allowlist, and any hit against the gold token set fails; (d) no bin below k; (e) a re-identification probe: the profile's joint (lines bin, blocks bin, depth) matches at least k gold files, and no 8-gram of gold line lengths appears in replica files; (f) no number above 100 with more than 2 significant figures.
3. **One gate out of `out/`.** `gold export-profile` is the only command that writes outside it; it runs the scanner and prints a diff for the owner. In CI the gold token set is absent, so only (a), (c) against the lexicon, and (d) apply.
4. **Program separation**: the import test of section 2, and a CI check that `tools/replica` never reads `out/gold`.

## 8. Library and viewer slices (S up to 1 day, M 2 to 4 days, L 1 to 2 weeks)

| slice | what | size | depends on |
|---|---|---|---|
| X0 | merge `scale-gate-turn-default` (latest-turn fix, minimum gate) | S | push |
| X1 hunk ranges | `edited` events carry block-relative `[oldStart, oldLen, newStart, newLen]`; a `layered-doc-1` field; the viewer reads it | M | the one-format cleanup (backlog item 1) |
| X2 compact turns | snapshot writes per-turn `eventsFrom` and texts by content id end to end; the viewer folds runs of turns that did not touch the selection | M | none |
| X3 block-to-commit trace | the inspector lists a block's commits in order with changed sub-ranges | S-M | X1 |
| X4 scope growth | `--since`/`--until`, `--history` above 200, `snapshot --extend <bundle> --older N` appends older turns without re-reading | L | X2, the replay-root id problem (section 11) |
| X5a PR layer, local | merge header to a PR item (number, branch, subject) anchored to footprint blocks | S | none |
| X5b PR layer, forge | review threads anchored to blocks at their commit through the resolver | M | X5a, G3 raw shape |
| X6 tracker layer | frames and templates, three anchor kinds, claim against checked outcome | L | G4/G5 schema, resolver |
| X7 foreground latency | K0 over a bundle with a foreground-window check; numbers by scope size only | S | X0 |
| X8 scale-check | minimums from the profile (multi-commit blocks, partial-edit fraction, PRs, threads, tickets per block); `--profile <f> --scale s` | S | profile v0 |

## 9. Order and parallel sessions

* **Day 0 to 1** (owner's machine, private access): X0, G0, G1, G2 with the deterministic hunk join; no library change needed.
* **In parallel**: schema and leak scanner (starting from a profile hand-filled with the mirror's numbers); replica R1 to R2 against that draft; X2 and X5a, independent slices.
* **Week 1**: G6 and G7 produce profile v0; the replica tunes to tolerance on tree, blocks and history; X1 then X3; X8.
* **Week 2**: G3, then X5b and R3 forge; G4 and G5, then X6 and R3 tracker; the agent stages G3a, G4a, G5a land last and stay optional.
* **Week 2 to 3**: X4 (riskiest, its own session); X7 at gold scale.
* Gold stages run only in the owner's session on the owner's machine. Every other session works from the committed profile and the replica and never sees private data.

## 10. CI gating

* **Quick tier** (every push, under 60 s): schema and leak test over `fixtures/profile`; the import-separation test; the replica at `--scale 0.05`, seed 1 (about 60 files, about 30 turns); `sub0canvas snapshot`; `scale-check --profile --scale 0.05`; `loadEntries` and the latest-turn default; byte-identical regeneration of a small pinned replica fixture.
* **Full tier** (release tags): the replica at scale 1 and one `--focus ticket` subset; gate the deterministic metrics against `targets` within the tolerances. Timing is never gated in CI.
* **Local release check** (owner): gold and replica side by side through K0 in a foreground window; numbers by scope size only go to SCALE.md 6.1.

## 11. Risks

* **Back-extension changes block ids.** Ids are birth hashes local to a replay root, so extending a scope back re-roots it and renames every block. X4 needs a fixed root plus a prepended segment with an id-carry map, or a reroot with a remap table; decide inside X4 and test on the git fixture first.
* **A month may still be flat.** If commits per block stays mostly 1, the stress is absent. Measure first; widen to 90 days or a hotter area.
* **Word soup may fold differently from real code.** Keep keywords and brace structure; compare block counts per file kind, not only in total.
* **Jaccard targeting on tiny blocks** has coarse steps; accept bucket-level agreement.
* **Local model quality and throughput** on long threads and traces: agents are optional and cached; measure hit and reject rates.
* **Windows git process cost** (about 0.1 s each): use one long-lived `cat-file --batch` and batched `diff -U0`.
* **Disk**: whole bundles grow as files times turns (213 MB at 8 turns, 1,590 files). Do X2 before a month of turns, or cap turns in G6 until then.
* **Residual identification by numbers** ([SCALE.md](../SCALE.md) section 8): rounding, k-merging and the probe reduce it and do not prove it away; hence D2.

## 12. Decisions (taken by judgement; the owner may overturn any)

* **D1.** Agent stages run on a local model only; no hosted assistant reads `out/gold`. Everything also runs with no agent at all.
* **D2.** Commit the fitted profile only (histograms at k=5 and the work-pattern parameters). The ordinal skeleton never leaves `out/`, because the replica regenerates the patterns by simulation.
* **D3.** First area: the most active directory by 30-day churn that has 100 or more commits and 300 to 3,000 files, named by alias in the untracked `repos.json`. The first forge and tracker fetch is still the owner's approval, asked at the point it runs.
* **D4.** The private-terms extension writes to the local guard automatically, and the owner sees the term count and the guard's first hits on the next commit.

## 13. First three steps (each yields numbers within a day)

1. **Gate, census, area.** Merge X0; run G0 and G1; pick the area. Record commits, files, merges with PR headers and their share.
2. **Hunk-join prototype (G2).** Join `git diff -U0` hunks to block spans from snapshot turns over the week already measured, then over 30 days. Report the commits-per-block histogram, multi-commit blocks, partial-edit fraction and hunks per edit. If multi-commit blocks stay below about 100, change the area or window before building anything else.
3. **Profile schema v0, leak scanner v0, replica R1 to R2.** Include one work-pattern measure (rework within 7 days) in the first profile so the simulation is tested from the start. Run them on those histograms; compare block counts and commits-per-block distance between gold and replica at scale 1 and 0.05; run the leak scan on the profile.
