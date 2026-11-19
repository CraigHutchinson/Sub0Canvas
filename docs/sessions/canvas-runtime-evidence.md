# Follow-up session for runtime evidence on canvas

Status is open as of 2026-11-19. [Backlog](../BACKLOG.md#any-time) · [Layers](../LAYERED_DOCUMENT.md) · [B0 format work](b0-format-catch-up.md)

## Goal

From a failing issue, let a Sub0Squad worker inspect the execution that produced it. The issue shows a short indication that evidence is available. A request for detail returns the relevant stack frames and observed values or a bounded comparison of related checks. The first consumer is the C++ arithmetic repair. Sub0Squad's canvas reasoning workflow brief and local trace-spike run hold the experiment evidence.

## What is known

Sub0Squad already captures a real stack trace, cursor changes, selected values, and a wider arithmetic matrix for the same candidate. The gate-only repair passed on the tested seed. Sending the failing trace by default led to a regression, and supplying a passing trace preserved right associativity without fixing the original failure. This supports testing selective retrieval while keeping the gate as the ordinary first view. The watch also needed a lifetime bound to avoid a hit after the watched object was destroyed.

The canvas has stable block IDs and layer summaries and details. Detail resolves an anchor against the current block. Runtime evidence must also retain the source revision or candidate snapshot that actually ran. A block ID helps find related code after edits, while the original revision supplies the source lines for the recorded event. See [the layer item and anchor model](../LAYERED_DOCUMENT.md).

## First receiving slice

1. In Sub0Squad, turn the existing run into a local fixture with one failing issue, one failing witness, one passing witness and one wider check result. Supply a summary reference through the existing layer path and keep the raw artifacts in the run. Keep debugger commands, evaluation of watches, capture limits and execution in the harness.
2. Give each observation enough provenance to retrieve its original source and reproduce its inputs. Use the run or candidate identity, the test or input, the source revision or content identity and the producer. Relate multiple frames to blocks where resolution is exact. If a file or frame cannot be resolved, keep the evidence accessible at the issue or run level.
3. Exercise summary-only and explicit detail reads with one real Sub0Squad repair. Show the recorded frame and watch values at their source revision. Edit the file, then read the same evidence and show that it still identifies the old source. Verify that missing or truncated detail is visible and that raw payloads stay outside the initial prompt and turn bundle.
4. Compare the existing layer and run-artifact path with the experience above. If the consumer needs an interchange field beyond what exists, bring the smallest observed requirement to B0 with a producer and reader fixture. V1a can receive the same lookup in the viewer when it adds scoped reads. Keep language-specific details in the corresponding harness and evaluate another backend when it has a concrete consumer.

## Done means

A worker can discover and retrieve the failing execution from the real test issue, compare the passing witness, and tell which revision each observation belongs to. An edited canvas still reads the historical evidence at the original location. Artifact bytes are fetched on demand within a recorded budget, and an unavailable artifact has a clear result. Record the end-to-end test, prompt cost and gate verdict, then decide whether a library or format change is warranted.
