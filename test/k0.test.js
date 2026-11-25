// K0's units (tools/k0, docs/K0.md): the budget judge, the machine rules, the large bundle and the DevTools client's plumbing. Headless-free: no browser
// is started, so `npm test` stays fast and green without one. The timing run itself is `node tools/k0/run.mjs` (label `timing`, local only).
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const M = require('../src/model.js');

const tool = (name) => import(`../tools/k0/${name}.mjs`);
const budgets = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'perf', 'k0', 'budgets.json'), 'utf8'));

/** A samples record with `n` samples of `ms` for every interaction of the budgets that can be sampled. */
function record(ms, { scope = 'large', n = 20, only } = {}) {
    const benchmarks = {};
    for (const [k, spec] of Object.entries(budgets.interactions)) {
        if (spec.notYetPossible || (only && !only.includes(k))) continue;
        benchmarks[`${scope}/${k}`] = { unit: 'ms', samples: Array.from({ length: n }, () => (typeof ms === 'function' ? ms(k) : ms)) };
    }
    return { format: 'k0-samples-1', host: 'test-host', scopes: { [scope]: { files: 1, blocks: 1, turns: 1, bundleBytes: 1 } }, benchmarks, power: { state: 'ac' }, machine: {}, browser: {} };
}

test('the budgets are the rows of AUTHORING.md section 3.2', () => {
    const authoring = fs.readFileSync(path.join(__dirname, '..', 'docs', 'AUTHORING.md'), 'utf8');
    assert.equal(budgets.percentile, 95);
    assert.equal(budgets.repetitions, 20);
    const expected = { 'folder-toggle': 50, 'open-file': 100, 'open-file-fetched': 300, 'select-block': 50, 'switch-turn': 200, 'centre-layer': 100, 'centre-code': 100,
        'filter-key': 30, 'first-tree': 1000, 'first-file': 2000, 'actor-event': 50 };
    for (const [k, ms] of Object.entries(expected)) assert.equal(budgets.interactions[k].budgetMs, ms, k);
    assert.match(authoring, /Interactivity budgets/);
    assert.match(authoring, /\| expand or collapse a folder[^|]*\| 50 ms \|/);
});

test('percentile is nearest-rank: 20 samples at p95 give the 19th', async () => {
    const { percentile, median } = await tool('judge');
    const s = Array.from({ length: 20 }, (_, i) => i + 1);
    assert.equal(percentile(s, 95), 19);
    assert.equal(percentile(s, 100), 20);
    assert.equal(percentile([...s].reverse(), 95), 19, 'order does not matter');
    assert.equal(percentile([7], 95), 7);
    assert.ok(Number.isNaN(percentile([], 95)));
    assert.equal(median([1, 2, 3, 4]), 2.5);
    assert.equal(median([3, 1, 2]), 2);
});

test('a record inside every budget passes; one slow sample in twenty is the 20th and does not breach p95', async () => {
    const { judge } = await tool('judge');
    const ok = judge(record(5), budgets);
    assert.equal(ok.status, 'PASS');
    assert.equal(ok.counts.breach, 0);
    assert.equal(ok.counts.na, 2, 'the two interactions that are not yet possible are reported, not failed');
    assert.equal(ok.exit, 0);
    const spike = judge(record(5), budgets);
    const r = record(5);
    r.benchmarks['large/switch-turn'].samples[3] = 5000;
    assert.equal(judge(r, budgets).counts.breach, 0, 'p95 of 20 ignores the single worst');
    r.benchmarks['large/switch-turn'].samples[4] = 5000;
    const two = judge(r, budgets);
    assert.equal(two.counts.breach, 1, 'two slow samples in 20 are above the 95th percentile');
    assert.ok(spike);
});

test('a breach is one line, the verdict one line, and only --strict makes it fail', async () => {
    const { judge } = await tool('judge');
    const r = judge(record((k) => (k === 'folder-toggle' ? 1250 : 5)), budgets);
    assert.equal(r.status, 'OVER-BUDGET');
    assert.equal(r.exit, 0, 'the baseline gates; a budget is a target the viewer is not yet within');
    assert.equal(r.lines.length, 2);
    assert.match(r.lines[0], /^k0: OVER-BUDGET 1 of 10 over budget \(p95\), 0 regressed vs baseline \(no baseline\), 2 not yet possible$/);
    assert.match(r.lines[1], /^BREACH large\/folder-toggle: p95 1250 ms > 50 ms \(p50 1250, x25\)$/);
    assert.equal(judge(record((k) => (k === 'folder-toggle' ? 1250 : 5)), budgets, null, { strict: true }).exit, 1);
});

test('the judge is deterministic: the same record gives the same lines', async () => {
    const { judge, report } = await tool('judge');
    const rec = record((k) => k.length * 11);
    const a = judge(rec, budgets), b = judge(JSON.parse(JSON.stringify(rec)), budgets);
    assert.deepEqual(a.lines, b.lines);
    assert.equal(report(rec, a), report(rec, b));
});

test('a baseline: drift beyond the tolerance regresses, drift inside it or below 2 ms does not', async () => {
    const { judge, baselineFrom } = await tool('judge');
    const base = baselineFrom(record(10), budgets);
    assert.equal(base.benchmarks['large/switch-turn'].p95, 10);
    assert.equal(judge(record(11), budgets, base).counts.regressed, 0, '+10% is inside 25%');
    assert.equal(judge(record(10.5), budgets, base).counts.regressed, 0);
    const slow = judge(record((k) => (k === 'switch-turn' ? 20 : 10)), budgets, base);
    assert.equal(slow.counts.regressed, 1);
    assert.equal(slow.status, 'FAIL');
    assert.equal(slow.exit, 1);
    assert.match(slow.lines[1], /^REGRESSED large\/switch-turn: p95 20 ms vs baseline 10 ms \(\+100%, tol 25%\)$/);
    const tiny = baselineFrom(record(1), budgets);
    assert.equal(judge(record(2.5), budgets, tiny).counts.regressed, 0, '+150% but only 1.5 ms: below a frame, noise');
});

test('a row with no samples that should have them is MISSING and fails; the error text is carried', async () => {
    const { judge } = await tool('judge');
    const rec = record(5);
    rec.benchmarks['large/open-file'] = { unit: 'ms', samples: [], error: 'no file row' };
    const r = judge(rec, budgets);
    assert.equal(r.status, 'FAIL');
    assert.ok(r.lines.some((l) => l === 'MISSING large/open-file: no file row'));
});

test('scopes are judged separately', async () => {
    const { judge } = await tool('judge');
    const a = record(5, { scope: 'mirror' }), b = record(500, { scope: 'large' });
    const rec = { ...a, scopes: { ...a.scopes, ...b.scopes }, benchmarks: { ...a.benchmarks, ...b.benchmarks } };
    const r = judge(rec, budgets);
    assert.ok(r.rows.filter((x) => x.scope === 'mirror').every((x) => x.status === 'ok' || x.status === 'n/a'));
    assert.ok(r.rows.filter((x) => x.scope === 'large' && x.status !== 'n/a').every((x) => x.status === 'breach' || x.budgetMs >= 500));
});

test('the machine rules: battery skips, a busy machine skips, force measures and says so', async () => {
    const { decide, decidePower, windowsBatteryProbe, busyFraction, hostAlias, findBrowser, browserCandidates } = await tool('env');
    assert.deepEqual(decide({ power: decidePower({ present: true, discharging: true }), busy: 0 }), { ok: false, reason: 'on battery' });
    assert.equal(decide({ power: decidePower({ present: false }), busy: 0.05 }).ok, true, 'a desktop with no battery is on mains');
    assert.equal(decide({ power: decidePower({ present: true, discharging: false }), busy: 0.05 }).ok, true);
    const busy = decide({ power: { onBattery: false }, busy: 0.5 });
    assert.equal(busy.ok, false);
    assert.match(busy.reason, /not quiet/);
    assert.deepEqual(decide({ power: { onBattery: true }, busy: 1, force: true }), { ok: true, forced: true, reason: 'forced' });
    assert.equal(windowsBatteryProbe('').present, false);
    assert.equal(windowsBatteryProbe('{"BatteryStatus":2}').discharging, false, 'status 2 is on mains');
    assert.equal(windowsBatteryProbe('[{"BatteryStatus":1}]').discharging, true, 'status 1 is discharging');
    const t = (i) => ({ times: { user: i, sys: 0, idle: 0 } });
    assert.equal(busyFraction([{ times: { user: 0, sys: 0, idle: 0 } }], [{ times: { user: 25, sys: 0, idle: 75 } }]), 0.25);
    assert.equal(hostAlias({ SUB0PERF_HOST: 'Some-Host' }, 'ANYTHING'), 'some-host');
    assert.equal(hostAlias({}, 'My-PC'), 'my-pc');
    assert.equal(findBrowser(undefined, {}, () => false), null, 'no browser: the runner skips, it does not fail');
    assert.equal(findBrowser('/x/browser', {}, (p) => p === '/x/browser'), '/x/browser');
    assert.equal(browserCandidates('/a', { BROWSER: '/b' })[1], '/b');
    assert.ok(t);
});

test('the large bundle is the reference scale, deterministic, and loads in the viewer model', async () => {
    const { largeEntries, REFERENCE } = await tool('gen-large');
    const small = largeEntries({ files: 40, turns: 2 });
    const again = largeEntries({ files: 40, turns: 2 });
    assert.deepEqual(small.entries, again.entries);
    const { docs, problems } = M.loadEntries(small.entries);
    assert.deepEqual(problems, []);
    assert.equal(docs.length, 1);
    assert.deepEqual(docs[0].turnNumbers, [1, 2]);
    assert.equal(docs[0].turns[0].blocks.length, small.blocks);
    assert.equal(Object.keys(docs[0].turns[1].files).length, 40);
    assert.ok(docs[0].turns[1].files['core/album0/f0000.cpp'].text.endsWith('// turn 2\n'), 'every 20th file changes in a later turn');
    assert.equal(docs[0].turns[0].files['core/album0/f0000.cpp'].revision, 1);
    // Every block's lines are inside its file.
    const t = docs[0].turns[0];
    for (const b of t.blocks.slice(0, 200)) assert.ok(b.last <= M.splitLines(t.files[b.path].text).length);
    // The reference scale: 1,590 files and about 20,000 blocks (the average is 12.6 a file); counted, not built.
    assert.equal(REFERENCE.files, 1590);
    const blocksFor = (n) => Array.from({ length: n }, (_, i) => 12 + (i % 5 < 3 ? 1 : 0)).reduce((a, b) => a + b, 0);
    assert.equal(blocksFor(REFERENCE.files), 20034);
});

test('the DevTools client: ids pair responses, errors reject, close rejects what is pending', async () => {
    const { Cdp } = await tool('cdp');
    const listeners = {};
    const sent = [];
    const ws = { addEventListener: (k, f) => { listeners[k] = f; }, send: (s) => sent.push(JSON.parse(s)), close() {} };
    const cdp = new Cdp(ws);
    const a = cdp.send('Browser.getVersion');
    const b = cdp.send('Runtime.evaluate', { expression: '1' }, 'S1');
    assert.deepEqual(sent.map((m) => [m.id, m.method, m.sessionId]), [[1, 'Browser.getVersion', undefined], [2, 'Runtime.evaluate', 'S1']]);
    listeners.message({ data: JSON.stringify({ id: 2, result: { v: 2 } }) });
    assert.deepEqual(await b, { v: 2 });
    listeners.message({ data: JSON.stringify({ id: 1, error: { message: 'nope' } }) });
    await assert.rejects(a, /Browser.getVersion: nope/);
    const c = cdp.send('X.y');
    listeners.close();
    await assert.rejects(c, /closed/);
});

test('the runner: arguments, and a skip on battery is a line that begins SKIP (what ctest matches)', async () => {
    const { parseArgs } = await tool('run');
    assert.equal(parseArgs([]).scope, 'all');
    assert.deepEqual([parseArgs(['--scope', 'mirror', '--reps', '3', '--force']).scope, parseArgs(['--reps', '3']).reps, parseArgs(['--force']).force], ['mirror', 3, true]);
    assert.throws(() => parseArgs(['--scope', 'huge']), /--scope is/);
    assert.throws(() => parseArgs(['--nope']), /unknown argument/);
    const cmake = fs.readFileSync(path.join(__dirname, '..', 'CMakeLists.txt'), 'utf8');
    assert.match(cmake, /Sub0Canvas_K0_timing[\s\S]*SKIP_REGULAR_EXPRESSION "\^SKIP timing"/);
});

test('the page scripts are valid JavaScript and name every scenario', async () => {
    const { PAGE_SCRIPT, FIRST_VIEW_SCRIPT, SCENARIOS } = await tool('scenarios');
    new Function(PAGE_SCRIPT);
    new Function(FIRST_VIEW_SCRIPT);
    for (const id of SCENARIOS) assert.ok(PAGE_SCRIPT.includes(`'${id}'`) || PAGE_SCRIPT.includes(`async ${id}(`), id);
});
