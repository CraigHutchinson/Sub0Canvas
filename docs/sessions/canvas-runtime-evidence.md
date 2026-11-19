# Follow-up session for runtime evidence on canvas

Status is open as of 2026-11-19. [Backlog](../BACKLOG.md#any-time) · [Layers](../LAYERED_DOCUMENT.md) · [B0 format work](b0-format-catch-up.md)

## Goal

Let a Sub0Squad worker navigate from a failing test or crash to its recorded call stack, and from a source block back to the tests or runs that observed it. The issue shows a short indication that evidence is available. A request for detail returns the relevant stack frames and observed values or a bounded comparison of related checks. The first consumer is the C++ arithmetic repair. Sub0Squad's canvas reasoning workflow brief and local trace-spike run hold the experiment evidence.

## What is known

Sub0Squad already captures a real stack trace, cursor changes, selected values, and a wider arithmetic matrix for the same candidate. The gate-only repair passed on the tested seed. Sending the failing trace by default led to a regression, and supplying a passing trace preserved right associativity without fixing the original failure. This supports testing selective retrieval while keeping the gate as the ordinary first view. The watch also needed a lifetime bound to avoid a hit after the watched object was destroyed.

The canvas has stable block IDs and layer summaries and details. Detail resolves an anchor against the current block. Runtime evidence must also retain the source revision or candidate snapshot that actually ran. A block ID helps find related code after edits, while the original revision supplies the source lines for the recorded event. See [the layer item and anchor model](../LAYERED_DOCUMENT.md).

## Navigate in both directions

From the test, assertion or crash, show the recorded stack in frame order. A selected frame opens its file at the revision and location that ran, with any observed values. Keep repeated recursive frames distinct. Show unresolved frames, capture limits and missing symbols so the reader can see where the evidence stops.

From a code block or source range, list the recorded tests and runs that reached it. Expand an entry to the matching frames and surrounding callers. Keep one result per run and witness in the summary, even when recursion gives it several matching frames. Link the assertion or fault location as a separate point in the same execution. A frame records participation. Show a confirmed cause or a suspected explanation only when supporting evidence or an explicitly attributed diagnosis exists.

After an edit or rename, the block identity can still lead to related historical runs. Opening one of those observations shows the original source location. A patched block needs a new run to establish its current behavior. Scope reverse lookup by revision, run and outcome, and fetch matching frames on demand.

## Review of related uses

| Use | From the result to the code | From the code to the result |
|---|---|---|
| Crash, exception, or timeout | Show the captured fault or throw site and the relevant thread stacks. For a timeout, identify each snapshot by capture time. For async work, connect hops only when the runtime records the relationship. | Find incidents and runs that recorded the block, with capture type, thread, and occurrence. Preserve partial stacks when symbols or source are missing. |
| Passing tests and coverage | Show which blocks an individual test reached when coverage is collected per test. Keep aggregate coverage at suite or run scope. | List tests observed to exercise a block. Use this evidence to help select regression checks and show when the observations predate the change. |
| Profiling and performance regressions | Drill from a benchmark change or sampled hotspot into the measured caller paths, with the build, workload and sampling metadata. | Find workloads and measurements that included the block. Label sampled counts and keep comparisons tied to the runs that produced them. |
| Compiler, sanitizer and CI diagnostics | Open the reported ranges and any recorded related locations. Preserve distinct stack roles when a sanitizer reports several stacks for one finding. | Find checks that cited the block and show whether each relation is a source location, a runtime observation or a diagnosis. |

The common need is revision-bound evidence readable in either direction. Use the C++ test case to prove navigation and bounded detail first. Coverage, profiling, multi-thread and async cases each need their own receiving fixture before they shape a shared interface.

## First receiving slice

1. In Sub0Squad, turn the existing run into a local fixture with one failing issue, one failing witness, one passing witness and one wider check result. Supply a summary reference through the existing layer path and keep the raw artifacts in the run. Keep debugger commands, evaluation of watches, capture limits and execution in the harness.
2. Give each observation enough provenance to retrieve its original source and reproduce its inputs. Use the run or candidate identity, the test or input, the source revision or content identity and the producer. Relate multiple frames to blocks where resolution is exact, preserving their order and original locations. If a file or frame cannot be resolved, keep the evidence accessible at the issue or run level.
3. Exercise summary-only and explicit detail reads with one real Sub0Squad repair. From the failing arithmetic test, walk the observed frames for parse_unary, parse_power and parse_term. From each recorded block, find the same failing test and run, with separate frame occurrences where applicable. Verify that a passing witness linked to the same block is distinguishable. Edit the file, then follow both directions and confirm that opening the historical observation still shows its old source. Verify that missing or truncated detail is visible and that raw payloads stay outside the initial prompt and turn bundle.
4. Compare the existing layer and run-artifact path with the experience above. If the consumer needs an interchange field beyond what exists, bring the smallest observed requirement to B0 with a producer and reader fixture. V1a can receive the same lookup in the viewer when it adds scoped reads. Keep language-specific details in the corresponding harness and evaluate another backend when it has a concrete consumer.

## Done means

A worker can locate the failing execution from the test issue and from a participating code block, compare the passing witness, and tell which revision each observation belongs to. An edited canvas still reads the historical evidence at the original location. Artifact bytes are fetched on demand within a recorded budget, and an unavailable artifact has a clear result. Record the end-to-end test, prompt cost and gate verdict, then decide whether a library or format change is warranted.
