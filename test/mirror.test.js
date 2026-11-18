// The synthetic mirror (tools/mirror, docs/MIRROR.md): deterministic, matching its committed fixture, loadable by the viewer, git-exact, and with
// truth that is internally consistent. What the mirror stresses is recorded in truth.edgeCases; these tests check the fixture, not the canvas's
// support for it (that is what change views, P0-P5, will be checked against).
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { generateMirror, shape } = require('../tools/mirror/mirror.js');
const { fastImportStream, canvasEntries, githubPlan } = require('../tools/mirror/outputs.js');
const M = require('../src/model.js');

const fixtureDir = path.join(__dirname, '..', 'fixtures', 'mirror', 'seed-1');
const m = generateMirror({ seed: 1 });
const roundTrip = (x) => JSON.parse(JSON.stringify(x));

test('generation is deterministic', () => {
    const again = generateMirror({ seed: 1 });
    assert.deepEqual(roundTrip(again.state), roundTrip(m.state));
    assert.deepEqual(roundTrip(again.truth), roundTrip(m.truth));
});

test('the committed fixture is what the generator writes (regenerate: npm run fixtures:mirror)', () => {
    for (const [name, value] of [['state', m.state], ['truth', m.truth], ['shape', shape(m)]]) {
        const committed = JSON.parse(fs.readFileSync(path.join(fixtureDir, `${name}.json`), 'utf8'));
        assert.deepEqual(roundTrip(value), committed, `${name}.json differs from the generator`);
    }
});

test('other seeds generate too, with the same topic shape', () => {
    for (let seed = 2; seed <= 8; seed++) {
        const s = shape(generateMirror({ seed }));
        assert.equal(s.changes, shape(m).changes, `seed ${seed}`);
        assert.ok(s.files > 250 && s.files < 400, `seed ${seed}: ${s.files} files`);
    }
});

test('the canvas output loads in the viewer with no problems', () => {
    const { docs, problems } = M.loadEntries(canvasEntries(m));
    assert.deepEqual(problems, []);
    assert.equal(docs.length, 1);
    assert.equal(docs[0].turns.length, 8);
    const last = docs[0].turns.at(-1);
    for (const layer of ['changes', 'topics', 'spikes', 'review']) assert.ok(last.layers[layer].length > 0, layer);
    assert.ok(last.layers.changes.some((c) => /state: reverted/.test(c.detail)), 'the reverted change shows as reverted');
    assert.equal(last.layers.changes.length, m.plan.length, 'the last turn, at the window\'s end, shows every change');
});

test('the source answers like a DocumentSource', () => {
    const head = m.branches.get('main');
    const files = m.source.files(head);
    assert.match(files['README.md'], /Fernhall/);
    assert.equal(files['assets/icons/album.png'], null, 'binaries have no text');
    const changed = m.source.changedPaths(m.windowStart, head);
    assert.ok(changed.includes('docs/Performance.md') && changed.includes('docs/performance.md'), 'the case-only rename is a change of two paths');
    const blocks = m.source.blocks(head, 'core/selection/selection.cpp');
    const lines = files['core/selection/selection.cpp'].split('\n');
    for (const b of blocks) assert.equal(lines[b.first - 1], b.signature, `${b.id} starts at its signature`);
    assert.equal(blocks.filter((b) => b.name === 'Selection::countEntries').length, 2, 'the overloads');
    assert.ok(m.source.revisions(m.windowStart, head).length > 100);
});

test('the truth is consistent', () => {
    const t = m.truth;
    const keys = new Set(Object.keys(t.changes));
    for (const l of t.links) {
        assert.ok(keys.has(l.from), `link from ${l.from}`);
        assert.ok(keys.has(l.to) || l.to === 'topic', `link to ${l.to}`);
    }
    const states = new Set(t.spikeCoverage.S1.map((h) => h.state));
    assert.deepEqual([...states].sort(), ['dropped', 'inFlight', 'landed', 'notCarved', 'obsolete'], 'every hunk state occurs');
    assert.ok(t.spikeCoverage.S1.some((h) => h.pathNow), 'a spike hunk whose block moved');
    const ids = t.edgeCases.map((e) => e.id);
    assert.equal(new Set(ids).size, ids.length);
    for (const e of t.edgeCases) {
        assert.ok(['handled', 'designed', 'gap'].includes(e.today), e.id);
        for (const k of [e.where.change, ...(e.where.changes ?? [])].filter(Boolean)) assert.ok(keys.has(k), `${e.id} names ${k}`);
        if ('commit' in e.where) assert.ok(m.bySha.has(e.where.commit), `${e.id} names a real commit`);
    }
});

test('the forge plan replays every pull request in time order', () => {
    const steps = githubPlan(m, { repo: 'sub0canvas_test1' });
    const creates = steps.filter((s) => s.argv[1] === 'pr' && s.argv[2] === 'create');
    assert.equal(creates.length, m.state.pulls.length);
    for (const p of m.state.pulls.filter((x) => x.state === 'merged'))
        assert.ok(steps.some((s) => s.argv[0] === 'git' && s.argv[3] === `${p.mergeSha}:refs/heads/${p.base}`), `merge of #${p.number} is a push`);
    assert.ok(steps.filter((s) => s.argv[0] === 'git' && s.argv[1] === 'push').some((s) => s.argv[3].startsWith('+')), 'change branches are force-pushed');
    assert.equal(steps.filter((s) => s.argv[2] === 'close').length, m.state.pulls.filter((p) => p.state === 'closed').length);
});

const hasGit = spawnSync('git', ['--version']).status === 0;
test('git builds the same commit ids', { skip: !hasGit && 'git is not installed' }, () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mirror-'));
    try {
        const repo = path.join(dir, 'repo');
        assert.equal(spawnSync('git', ['init', '-q', '--initial-branch=main', repo]).status, 0);
        const r = spawnSync('git', ['-C', repo, '-c', 'core.ignorecase=false', 'fast-import', '--quiet', '--done'], { input: fastImportStream(m) });
        assert.equal(r.status, 0, String(r.stderr));
        const refs = spawnSync('git', ['-C', repo, 'for-each-ref', '--format=%(refname:short) %(objectname)', 'refs/heads'], { encoding: 'utf8' }).stdout.trim().split('\n');
        assert.deepEqual(Object.fromEntries(refs.map((l) => l.split(' '))), Object.fromEntries(m.branches));
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});
