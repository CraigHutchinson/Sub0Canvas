#!/usr/bin/env node
// K0's judge: applies the budgets of docs/AUTHORING.md section 3.2 (perf/k0/budgets.json) to a samples record, deterministically, and prints one verdict
// line plus one line per breach (SUB0PERF_OUTPUT=brief style: the details are in the report file). Pure functions are exported for test/k0.test.js.
// Usage: node tools/k0/judge.mjs <samples.json> [--budgets FILE] [--baseline FILE] [--report FILE] [--strict]
// Exit: 0 judged (breaches of a budget are reported, and fail only with --strict); 1 a regression against the baseline (or a breach with --strict);
// 2 unusable input.
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
export const SAMPLES_FORMAT = 'k0-samples-1';

/** Nearest-rank percentile of `values` (0 < p <= 100): the ceil(p/100*n)-th smallest, so 20 samples at p95 give the 19th. NaN for no samples. */
export function percentile(values, p) {
    if (!values.length) return NaN;
    const s = [...values].sort((a, b) => a - b);
    return s[Math.min(s.length - 1, Math.max(0, Math.ceil((p / 100) * s.length) - 1))];
}

/** The median (mean of the two middle values for an even count). NaN for no samples. */
export function median(values) {
    if (!values.length) return NaN;
    const s = [...values].sort((a, b) => a - b), m = s.length >> 1;
    return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

export const round = (x, d = 1) => (Number.isFinite(x) ? Math.round(x * 10 ** d) / 10 ** d : x);

/** Row key: `<scope>/<interaction>`. */
export const rowKey = (scope, interaction) => `${scope}/${interaction}`;

/**
 * Judge a samples record against the budgets (and a baseline, if given). Returns
 * `{ rows, counts, status, verdict, lines, exit }`; a row is `{ key, scope, interaction, title, n, p50, p95, budgetMs, status, baselineP95, drift, regressed }`
 * with status `ok`, `breach`, `n/a` (not yet possible, no samples) or `missing` (samples expected and absent); `regressed` is set when p95 is worse than
 * the baseline's by more than the tolerance (and by at least 2 ms, below which a frame is noise).
 */
export function judge(record, budgets, baseline = null, { strict = false } = {}) {
    const rows = [];
    const pct = budgets.percentile ?? 95;
    const tol = budgets.driftTolerance ?? 0.25;
    for (const scope of Object.keys(record.scopes ?? {})) {
        for (const [interaction, spec] of Object.entries(budgets.interactions)) {
            const key = rowKey(scope, interaction);
            const got = record.benchmarks?.[key];
            const samples = got?.samples ?? [];
            const row = { key, scope, interaction, title: spec.title, budgetMs: spec.budgetMs, n: samples.length, p50: NaN, p95: NaN, status: 'ok', regressed: false };
            if (spec.notYetPossible && !samples.length) { row.status = 'n/a'; row.note = spec.notYetPossible; }
            else if (!samples.length) { row.status = 'missing'; row.note = got?.error ?? 'no samples'; }
            else {
                row.p50 = median(samples);
                row.p95 = percentile(samples, pct);
                if (row.p95 > spec.budgetMs) row.status = 'breach';
                const b = baseline?.benchmarks?.[key];
                if (b && Number.isFinite(b.p95)) {
                    row.baselineP95 = b.p95;
                    row.drift = row.p95 / b.p95 - 1;
                    row.regressed = row.drift > tol && row.p95 - b.p95 >= 2;
                }
            }
            rows.push(row);
        }
    }
    const count = (f) => rows.filter(f).length;
    const counts = { judged: count((r) => r.status === 'ok' || r.status === 'breach'), breach: count((r) => r.status === 'breach'), ok: count((r) => r.status === 'ok'),
        na: count((r) => r.status === 'n/a'), missing: count((r) => r.status === 'missing'), regressed: count((r) => r.regressed), baselined: count((r) => r.baselineP95 !== undefined) };
    const bad = counts.regressed > 0 || counts.missing > 0 || (strict && counts.breach > 0);
    const status = bad ? 'FAIL' : counts.breach > 0 ? 'OVER-BUDGET' : 'PASS';
    const lines = [`k0: ${status} ${counts.breach} of ${counts.judged} over budget (p${pct}), ${counts.regressed} regressed vs baseline${baseline ? '' : ' (no baseline)'}, `
        + `${counts.na} not yet possible${counts.missing ? `, ${counts.missing} MISSING` : ''}`];
    for (const r of rows) {
        if (r.status === 'breach') lines.push(`BREACH ${r.key}: p${pct} ${round(r.p95)} ms > ${r.budgetMs} ms (p50 ${round(r.p50)}, x${round(r.p95 / r.budgetMs)})`);
        else if (r.status === 'missing') lines.push(`MISSING ${r.key}: ${r.note}`);
        if (r.regressed) lines.push(`REGRESSED ${r.key}: p${pct} ${round(r.p95)} ms vs baseline ${round(r.baselineP95)} ms (${r.drift >= 0 ? '+' : ''}${round(r.drift * 100)}%, tol ${round(tol * 100, 0)}%)`);
    }
    return { rows, counts, status, verdict: lines[0], lines, exit: bad ? 1 : 0 };
}

/** The report (markdown): the full table with every row, the machine and the scope sizes. */
export function report(record, result) {
    const m = record.machine ?? {};
    const out = ['# K0 report', '',
        `host ${record.host}, ${m.system ?? ''} ${m.cpu ?? ''}; power ${record.power?.state ?? '?'}${record.power?.forced ? ' (forced)' : ''}; browser ${record.browser?.name ?? '?'} ${record.browser?.version ?? ''}; `
        + `node ${record.node}; commit ${record.commit ?? '?'}; ${record.repetitions} repetitions after ${record.warmup} warm-up; recorded ${record.recorded}`, ''];
    for (const [scope, info] of Object.entries(record.scopes ?? {})) out.push(`scope ${scope}: ${info.files} files, ${info.blocks} blocks, ${info.turns} turns, ${info.bundleBytes} bytes, ${info.domNodes ?? "?"} DOM nodes, idle frame ${info.frameFloorMs ?? "?"} ms`);
    out.push('', '| scope | interaction | n | p50 ms | p95 ms | budget ms | status | baseline p95 | drift |', '|---|---|---|---|---|---|---|---|---|');
    for (const r of result.rows)
        out.push(`| ${r.scope} | ${r.title} | ${r.n} | ${round(r.p50)} | ${round(r.p95)} | ${r.budgetMs} | ${r.status}${r.regressed ? ' REGRESSED' : ''}${r.note ? ` (${r.note})` : ''} | `
            + `${r.baselineP95 === undefined ? '' : round(r.baselineP95)} | ${r.drift === undefined ? '' : `${round(r.drift * 100)}%`} |`);
    out.push('', result.verdict, '');
    return out.join('\n');
}

/** A baseline object (perf/k0/<host>.json) from a samples record: p50 and p95 per row, with the machine and power state. */
export function baselineFrom(record, budgets) {
    const benchmarks = {};
    for (const [key, b] of Object.entries(record.benchmarks)) {
        if (!b.samples?.length) continue;
        benchmarks[key] = { p50: round(median(b.samples), 2), p95: round(percentile(b.samples, budgets.percentile ?? 95), 2), n: b.samples.length };
    }
    return { format: 'k0-baseline-1', host: record.host, machine: record.machine, power: record.power, browser: record.browser, node: record.node, commit: record.commit,
        recorded: record.recorded, scopes: record.scopes, benchmarks, accepted: [] };
}

export const readJson = (file) => JSON.parse(readFileSync(file, 'utf8'));

export const loadBudgets = (file = join(root, 'perf', 'k0', 'budgets.json')) => readJson(file);

function main(argv) {
    const args = { strict: false };
    const rest = [];
    for (let i = 0; i < argv.length; i++) {
        if (['--budgets', '--baseline', '--report'].includes(argv[i])) args[argv[i].slice(2)] = argv[++i];
        else if (argv[i] === '--strict') args.strict = true;
        else rest.push(argv[i]);
    }
    if (!rest[0] || !existsSync(rest[0])) { console.error('usage: judge.mjs <samples.json> [--budgets F] [--baseline F] [--report F] [--strict]'); return 2; }
    let record, budgets, baseline = null;
    try {
        record = readJson(rest[0]);
        if (record.format !== SAMPLES_FORMAT) throw new Error(`format is ${record.format}, expected ${SAMPLES_FORMAT}`);
        budgets = loadBudgets(args.budgets);
        if (args.baseline && existsSync(args.baseline)) baseline = readJson(args.baseline);
    } catch (e) { console.error(`k0 judge: unusable input: ${e.message}`); return 2; }
    const result = judge(record, budgets, baseline, { strict: args.strict });
    if (args.report) writeFileSync(args.report, report(record, result));
    console.log(result.lines.join('\n'));
    return result.exit;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) process.exit(main(process.argv.slice(2)));
