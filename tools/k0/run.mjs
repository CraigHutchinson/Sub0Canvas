#!/usr/bin/env node
// K0: times every viewer interaction of docs/AUTHORING.md section 3.2 in a headless Edge or Chrome (Chrome DevTools Protocol, Node's built-in WebSocket),
// on the synthetic mirror and on a large generated subtree, and judges the 95th percentiles against perf/k0/budgets.json. A local-tier timing check: mains
// power and a quiet machine, never in CI (docs/K0.md). Output is one verdict line plus one line per breach; the table is in out/k0/last.report.md.
//
// Usage: node tools/k0/run.mjs [--scope mirror|large|all] [--reps 20] [--warmup 2] [--force] [--browser PATH] [--record] [--out DIR] [--strict]
//   --force    measure on battery or a busy machine anyway (the record says so; never to be recorded as a baseline)
//   --record   write the result as this host's baseline, perf/k0/<host>.json (a full run on mains power only)
import { spawn, execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync, existsSync, statSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { launchBrowser } from './cdp.mjs';
import { findBrowser, readPower, sampleBusy, decide, hostAlias, machineInfo } from './env.mjs';
import { median, judge, report, baselineFrom, loadBudgets, readJson, rowKey, SAMPLES_FORMAT } from './judge.mjs';
import { largeEntries } from './gen-large.mjs';
import { SCENARIOS, PAGE_SCRIPT, FIRST_VIEW_SCRIPT } from './scenarios.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const require = createRequire(import.meta.url);

export function parseArgs(argv) {
    const a = { scope: 'all', force: false, record: false, strict: false, out: join(root, 'out', 'k0') };
    for (let i = 0; i < argv.length; i++) {
        const k = argv[i];
        if (k === '--scope') a.scope = argv[++i];
        else if (k === '--reps') a.reps = Number(argv[++i]);
        else if (k === '--warmup') a.warmup = Number(argv[++i]);
        else if (k === '--browser') a.browser = argv[++i];
        else if (k === '--out') a.out = argv[++i];
        else if (k === '--force') a.force = true;
        else if (k === '--record') a.record = true;
        else if (k === '--strict') a.strict = true;
        else throw new Error(`unknown argument ${k}`);
    }
    if (!['mirror', 'large', 'all'].includes(a.scope)) throw new Error(`--scope is mirror, large or all, not ${a.scope}`);
    return a;
}

/** The bundles by scope, written under `out` (served from the repository root, so the page fetches them by a root-relative URL). */
function writeBundles(scopes, out) {
    mkdirSync(out, { recursive: true });
    const files = {};
    if (scopes.includes('mirror')) {
        const { generateMirror } = require('../mirror/mirror.js');
        const { canvasEntries } = require('../mirror/outputs.js');
        const entries = canvasEntries(generateMirror({ seed: 1 }));
        files.mirror = join(out, 'mirror.bundle.json');
        writeFileSync(files.mirror, JSON.stringify(entries));
    }
    if (scopes.includes('large')) {
        files.large = join(out, 'large.bundle.json');
        writeFileSync(files.large, JSON.stringify(largeEntries().entries));
    }
    return files;
}

function startServer() {
    const child = spawn(process.execPath, [join(root, 'tools', 'serve.mjs'), '0'], { env: { ...process.env, PORT: '0' }, stdio: ['ignore', 'pipe', 'ignore'], windowsHide: true });
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('tools/serve.mjs printed no port in 20 s')), 20000);
        child.stdout.on('data', (d) => { const m = String(d).match(/:(\d+)\//); if (m) { clearTimeout(timer); resolve({ port: m[1], stop: () => child.kill() }); } });
        child.on('exit', (c) => reject(new Error(`tools/serve.mjs exited (${c})`)));
    });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitFor(tab, expr, ms = 90000) {
    const end = Date.now() + ms;
    for (;;) {
        if (await tab.eval(expr).catch(() => false)) return;
        if (Date.now() > end) throw new Error(`timed out waiting for ${expr}`);
        await sleep(100);
    }
}

async function measureScope(cdp, scope, url, args, benchmarks, scopes, log) {
    const total = args.reps + args.warmup;
    const tab = await cdp.openTab(url);
    try {
        await waitFor(tab, 'window.S0View && !!window.S0View.state.doc && !!document.querySelector(".code .line")');
        await tab.eval(PAGE_SCRIPT);
        scopes[scope] = { ...(await tab.eval('window.__k0.sizes()')), bundleBytes: scopes[scope]?.bundleBytes };
        // The idle frame: what timing an action that does nothing reads, the floor under every number here (vsync and the task hop).
        const idle = (await tab.eval('window.__k0.run("floor", 20)')).floor ?? [];
        scopes[scope].frameFloorMs = Math.round(median(idle) * 10) / 10;
        for (const id of SCENARIOS) {
            const r = await tab.eval(`window.__k0.run(${JSON.stringify(id)}, ${total})`);
            if (r.error) {
                log(`${scope}/${id}: ${r.error}`);
                for (const row of id === 'centre' ? ['centre-layer', 'centre-code'] : [id]) benchmarks[rowKey(scope, row)] = { unit: 'ms', samples: [], error: r.error };
                continue;
            }
            for (const [row, v] of Object.entries(r)) if (Array.isArray(v)) benchmarks[rowKey(scope, row)] = { unit: 'ms', samples: v.slice(args.warmup).map((x) => Math.round(x * 100) / 100) };
            if (r.below) scopes[scope].folderToggleFilesBelow = r.below;
        }
    } finally { await tab.close(); }
    // First view: a new tab per repetition, navigation to the tree and the first file painted.
    const tree = [], file = [];
    for (let i = 0; i < total; i++) {
        const t = await cdp.openTab('about:blank');
        try {
            await t.send('Page.addScriptToEvaluateOnNewDocument', { source: FIRST_VIEW_SCRIPT });
            await t.send('Page.navigate', { url });
            await waitFor(t, 'window.__k0first && window.__k0first.tree !== null && window.__k0first.file !== null');
            const r = await t.eval('window.__k0first');
            if (i >= args.warmup) { tree.push(Math.round(r.tree * 100) / 100); file.push(Math.round(r.file * 100) / 100); }
        } finally { await t.close(); }
    }
    benchmarks[rowKey(scope, 'first-tree')] = { unit: 'ms', samples: tree };
    benchmarks[rowKey(scope, 'first-file')] = { unit: 'ms', samples: file };
}

export async function main(argv) {
    const args = parseArgs(argv);
    const budgets = loadBudgets();
    args.reps ??= budgets.repetitions;
    args.warmup ??= budgets.warmup;
    const log = (m) => console.log(m);

    const browser = findBrowser(args.browser);
    if (!browser) { log('SKIP timing: no Chromium-family browser found (Edge or Chrome); pass --browser PATH or set BROWSER'); return 0; }
    const power = readPower();
    const busy = await sampleBusy();
    const verdict = decide({ power, busy, force: args.force });
    if (!verdict.ok) { log(`SKIP timing: ${verdict.reason}`); return 0; }
    if (args.record && (verdict.forced || args.reps < budgets.repetitions)) { log('k0: --record needs a full steady run (no --force, all repetitions)'); return 2; }

    const scopes = args.scope === 'all' ? ['mirror', 'large'] : [args.scope];
    const files = writeBundles(scopes, args.out);
    const server = await startServer();
    let browserRun;
    const benchmarks = {}, scopeInfo = {};
    try {
        browserRun = await launchBrowser(browser);
        for (const scope of scopes) {
            scopeInfo[scope] = { bundleBytes: statSync(files[scope]).size };
            const rel = relative(root, files[scope]).replace(/\\/g, '/');
            await measureScope(browserRun.cdp, scope, `http://127.0.0.1:${server.port}/index.html?bundle=${encodeURIComponent('/' + rel)}`, args, benchmarks, scopeInfo, log);
        }
    } finally { browserRun?.stop(); server.stop(); }

    const commit = (() => { try { return execFileSync('git', ['-C', root, 'rev-parse', '--short', 'HEAD'], { encoding: 'utf8' }).trim(); } catch { return null; } })();
    const record = { format: SAMPLES_FORMAT, tool: 'k0', unit: 'ms', host: hostAlias(), machine: machineInfo(), power: { ...power, busyCpu: Math.round(busy * 100) / 100, forced: !!verdict.forced },
        browser: { name: browser.replace(/\\/g, '/').split('/').pop().replace(/\.exe$/, ''), version: browserRun?.version }, node: process.version, commit,
        repetitions: args.reps, warmup: args.warmup, percentile: budgets.percentile, recorded: new Date().toISOString(), scopes: scopeInfo, benchmarks };

    const baselineFile = join(root, 'perf', 'k0', `${record.host}.json`);
    const baseline = existsSync(baselineFile) ? readJson(baselineFile) : null;
    const result = judge(record, budgets, baseline, { strict: args.strict });
    mkdirSync(args.out, { recursive: true });
    writeFileSync(join(args.out, 'last.samples.json'), JSON.stringify(record, null, 1) + '\n');
    writeFileSync(join(args.out, 'last.report.md'), report(record, result));
    if (args.record) {
        mkdirSync(dirname(baselineFile), { recursive: true });
        writeFileSync(baselineFile, JSON.stringify(baselineFrom(record, budgets), null, 1) + '\n');
        log(`k0: baseline written to ${relative(root, baselineFile).replace(/\\/g, '/')}`);
    }
    log(result.lines.join('\n'));
    log(`report: ${relative(root, join(args.out, 'last.report.md')).replace(/\\/g, '/')}`);
    return result.exit;
}

import { pathToFileURL } from 'node:url';
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main(process.argv.slice(2)).then((c) => process.exit(c), (e) => { console.error(`k0: ${e.message}`); process.exit(2); });
}
