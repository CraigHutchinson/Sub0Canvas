// The gold extraction pipeline (tools/gold/run.mjs) over the invented git fixture: census, one batched hunk walk, the join, its manifests, the batching
// of pathspecs and the refusal to write outside out/gold. Nothing here reads a real project.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const load = () => Promise.all([import('../tools/gold/run.mjs'), import('../tools/git-fixture.mjs')]);

test('chunkPaths keeps every path, in order, and each batch within the budget', async () => {
    const { chunkPaths } = await import('../tools/gold/run.mjs');
    const paths = Array.from({ length: 500 }, (_, i) => `dir/sub${i % 7}/file_${i}.cpp`);
    const parts = chunkPaths(paths, 2000);
    assert.deepEqual(parts.flat(), paths);
    assert.ok(parts.length > 1);
    for (const p of parts) assert.ok(p.join('   ').length <= 2000 + 40, 'one command line');
    assert.deepEqual(chunkPaths([], 100), []);
});

test('a run directory cannot leave out/gold', async () => {
    const { runDir } = await import('../tools/gold/run.mjs');
    const base = fs.mkdtempSync(path.join(os.tmpdir(), 'gold-'));
    assert.throws(() => runDir('../escape', base), /escapes/);
    assert.throws(() => runDir('..', base), /escapes/);
    assert.ok(fs.existsSync(runDir('ok-run', base)));
});

test('census, hunks and join over the fixture: batched git calls, manifests, and counts that reconcile with git', async () => {
    const [{ census, hunks, join }, { buildRepository }] = await load();
    const repo = fs.mkdtempSync(path.join(os.tmpdir(), 'gold-repo-'));
    buildRepository(repo);
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gold-run-'));

    const c = await census(repo, dir, { since: '1990-01-01', minTouches: 2 });
    assert.ok(c.commits > 20, 'the fixture has history');
    assert.ok(c.setFiles > 0 && c.setFiles <= c.codeFilesTouched);
    const h = await hunks(repo, dir, { since: '1990-01-01' });
    assert.equal(h.gitCalls, 1, 'the whole set fits one git call: one walk, not one process per file');
    const m = join(dir);
    assert.ok(m.hunks > 0 && m.namedFunctions > 0);
    assert.ok(m.functionsHitTwiceOrMore > 0, 'the fixture edits some functions more than once');

    const set = fs.readFileSync(path.join(dir, 'set.tsv'), 'utf8').split('\n').filter(Boolean).map((l) => l.split('\t')[0]);
    const direct = execFileSync('git', ['-C', repo, 'log', '--no-merges', '-M', '-p', '-U0', '--format=', '--', ...set], { encoding: 'utf8', maxBuffer: 1 << 28 })
        .split('\n').filter((l) => l.startsWith('@@')).length;
    assert.equal(m.hunks, direct, 'the join sees every hunk git reports');
    for (const stage of ['census', 'hunks', 'join']) assert.ok(fs.existsSync(path.join(dir, `${stage}.manifest.json`)), stage);
});
