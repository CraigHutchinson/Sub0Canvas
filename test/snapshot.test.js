// `sub0canvas snapshot` (app/) over this repository's own history: the bundle loads in the viewer with no problems, block identities come from the
// library, and the commits layer anchors commits to the blocks they changed. Skipped when the executable is not built (ctest passes its path in
// SUB0CANVAS_EXE; locally it is looked for in build/default).
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const M = require('../src/model.js');

const root = path.join(__dirname, '..');
const exe = [process.env.SUB0CANVAS_EXE, path.join(root, 'build', 'default', 'sub0canvas.exe'), path.join(root, 'build', 'default', 'sub0canvas')]
    .find((p) => p && fs.existsSync(p));

test('a snapshot of this repository loads in the viewer', { skip: !exe && 'sub0canvas is not built' }, () => {
    const r = spawnSync(exe, ['snapshot', '--repo', root, '--history', '4', '--', 'lib/src'], { encoding: 'utf8', maxBuffer: 1 << 28 });
    assert.equal(r.status, 0, r.stderr);
    const entries = JSON.parse(r.stdout);
    assert.ok(entries.every(({ path }) => path === "canvas.json" || /^layers\/turn-\d+\.json$/.test(path)));
    const { docs, problems } = M.loadEntries(entries);
    assert.deepEqual(problems, []);
    const last = docs[0].turns.at(-1);
    assert.ok(Object.keys(last.files).every((p) => p.startsWith('lib/src/')), 'only the pathspec');
    assert.ok(last.blocks.length > 20, 'the library found blocks');
    assert.equal(last.layers.commits.length, docs[0].turns.length, 'one commits item per turn');
});

test('a snapshot refuses what it must not pass to git, and an unscoped request', { skip: !exe && 'sub0canvas is not built' }, () => {
    for (const args of [['snapshot', '--repo', root, 'lib/src;calc'], ['snapshot', '--repo', root], ['snapshot', '--repo', root, '--rev', 'HEAD&x', 'lib']]) {
        const r = spawnSync(exe, args, { encoding: 'utf8' });
        assert.equal(r.status, 2, args.join(' '));
    }
});

test('a missing turn parameter opens the latest turn, not turn 0', () => {
    assert.equal(M.pickTurn([0, 1, 2, 3], null), 3);
    assert.equal(M.pickTurn([0, 1, 2, 3], ''), 3);
    assert.equal(M.pickTurn([0, 1, 2, 3], '0'), 0);
    assert.equal(M.pickTurn([0, 1, 2, 3], '9'), 3);
    assert.equal(M.pickTurn([1, 2, 3, 4], null), 4);
});

test('the scale gate rejects a bundle too small to say anything and accepts a representative one', { skip: !exe && 'sub0canvas is not built' }, async () => {
    const { checkScale, MINIMUMS } = await import('../tools/scale-check.mjs');
    const snap = (history) => JSON.parse(spawnSync(exe, ['snapshot', '--repo', root, '--history', String(history), '--', 'lib/src'], { encoding: 'utf8', maxBuffer: 1 << 28 }).stdout);
    const small = checkScale(snap(3));
    assert.ok(small.some((f) => f.startsWith('turns:')), small.join('; '));
    assert.ok(small.some((f) => f.startsWith('files:')), small.join('; '));
    const own = { turns: 1, files: 5, blocks: 50 };   // a hosted runner's checkout may hold a single commit, so only the shape of the gate is pinned here
    assert.deepEqual(checkScale(snap(12), own), []);
    assert.equal(MINIMUMS.turns, 20);
});

test('edited events carry block-relative hunk ranges and the turn reconciles with the diff', { skip: (!exe && 'sub0canvas is not built') || (spawnSync('git', ['--version']).status !== 0 && 'git is not installed') }, async () => {
    const os = require('node:os');
    const { pathToFileURL } = require('node:url');
    const { buildRepository } = await import(pathToFileURL(path.join(root, 'tools', 'git-fixture.mjs')).href);
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sub0canvas-hunks-'));
    const repo = path.join(dir, 'tinycalc.git');
    try {
        buildRepository(repo);
        const git = (...args) => spawnSync('git', ['-C', repo, ...args], { encoding: 'utf8', maxBuffer: 1 << 28 });
        const r = spawnSync(exe, ['snapshot', '--repo', repo, '--rev', 'main', '--history', '40', '--', 'src/'], { encoding: 'utf8', maxBuffer: 1 << 28 });
        assert.equal(r.status, 0, r.stderr);
        const { docs, problems } = M.loadEntries(JSON.parse(r.stdout));
        assert.deepEqual(problems, []);
        const turns = docs[0].turns;
        const shas = git('rev-list', '--first-parent', '--max-count=40', 'main', '--', 'src/').stdout.trim().split('\n').reverse();
        assert.equal(shas.length, turns.length);
        let placed = 0, partial = 0;
        for (let i = 1; i < turns.length; ++i) {
            // (c) the independent count: every `@@` of the -U0 diff of the code files the view holds.
            const diff = git('diff', '-U0', '--no-renames', shas[i - 1], shas[i], '--', 'src/').stdout;
            const hunks = diff.split('\n').filter((l) => l.startsWith('@@ ')).length;
            const t = turns[i];
            assert.equal(t.diffHunks.total, hunks, `turn ${i}`);
            assert.ok(t.diffHunks.outside <= hunks);
            const spans = t.events.flatMap((e) => e.hunks || []);
            assert.ok(spans.length >= hunks - t.diffHunks.outside, `turn ${i}: every placed hunk has a span`);
            if (hunks - t.diffHunks.outside > 0) ++placed;
            for (const e of t.events) for (const h of e.hunks || []) {
                const b = t.blocks.find((x) => x.id === e.id);
                assert.ok(b, 'a span names a live block');
                const len = b.last - b.first + 1;
                assert.ok(h[2] >= 0 && h[2] + h[3] - 1 <= len, `new span within the block: ${h}`);
            }
            // (a, b) through the viewer: a block edited by one small hunk is partial, and the span is short of the block.
            for (const b of t.blocks) {
                const hist = M.blockHistory(docs[0], b.id).timeline.find((x) => x.turn === t.turn);
                if (hist?.edit === 'partial') { ++partial; assert.ok(hist.hunks.every((h) => h[3] <= b.last - b.first + 1)); }
            }
        }
        assert.ok(placed > 0, 'some commit edited a block');
        assert.ok(partial > 0, 'the viewer tells a partial edit from a rewrite');
        assert.deepEqual(M.validateTurn(turns[1]), []);
    } finally {
        fs.rmSync(dir, { recursive: true, force: true });
    }
});

test('the snapshot discovers Markdown through the same registry as Git history', { skip: !exe && 'sub0canvas is not built' }, () => {
    const snapshot = spawnSync(exe, ['snapshot', '--repo', root, '--history', '1', '--', 'docs/BACKBONE.md'], { encoding: 'utf8' });
    const history = spawnSync(exe, ['history', '--repo', root, '--max', '1', '--blocks', '--', 'docs/BACKBONE.md'], { encoding: 'utf8' });
    assert.equal(snapshot.status, 0, snapshot.stderr);
    assert.equal(history.status, 0, history.stderr);
    const { docs, problems } = M.loadEntries(JSON.parse(snapshot.stdout));
    assert.deepEqual(problems, []);
    const names = docs[0].turns[0].blocks.map((block) => block.name);
    assert.ok(names.length > 10);
    assert.deepEqual(names, JSON.parse(history.stdout).blocks.map((block) => block.name));
});
