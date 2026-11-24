// `sub0canvas history` (app/git_source.cpp, the live git DocumentSource) against the same hand-checked truth the C++ test reads from captured output
// (fixtures/scenarios/g1-tinycalc/truth.json, R-SRC-3): the repository is rebuilt from tools/git-fixture.mjs's plan into a temporary directory (local, no network), a
// shallow clone and a partial clone of it are made from it, and each scenario's events come from running git. Also what the adapter asked git (R-SCALE-9, A21) and
// what it does when objects are missing (R-SCALE-7, A20). Skipped when the executable is not built or git is not installed.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { pathToFileURL } = require('node:url');

const root = path.join(__dirname, '..');
const exe = [process.env.SUB0CANVAS_EXE, path.join(root, 'build', 'default', 'sub0canvas.exe'), path.join(root, 'build', 'default', 'sub0canvas')]
    .find((p) => p && fs.existsSync(p));
const hasGit = spawnSync('git', ['--version']).status === 0;
const skip = (!exe && 'sub0canvas is not built') || (!hasGit && 'git is not installed');
const truth = JSON.parse(fs.readFileSync(path.join(root, 'fixtures', 'scenarios', 'g1-tinycalc', 'truth.json'), 'utf8'));

const git = (repo, args) => spawnSync('git', ['-C', repo, ...args], { encoding: 'utf8' });
const idOfLabel = (repo, label) => git(repo, ['log', '--format=%H', `--grep=^${label}:`, 'main']).stdout.trim().split('\n')[0];

/** The repository once, for every test in this file. */
let repos = null;
async function repositories() {
    if (repos) return repos;
    const { buildRepository } = await import(pathToFileURL(path.join(root, 'tools', 'git-fixture.mjs')).href);
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sub0canvas-history-'));
    const full = path.join(dir, 'tinycalc.git');
    buildRepository(full);
    git(full, ['config', 'uploadpack.allowFilter', 'true']);
    git(full, ['config', 'uploadpack.allowAnySHA1InWant', 'true']);
    const url = pathToFileURL(full).href;
    const shallow = path.join(dir, 'shallow');
    assert.equal(spawnSync('git', ['clone', '-q', '--depth', '25', '--no-local', '--branch', 'main', url, shallow]).status, 0);
    const partial = path.join(dir, 'partial');
    const clone = spawnSync('git', ['clone', '-q', '--filter=blob:none', '--no-local', '--branch', 'main', url, partial]);   // a checkout: HEAD's blobs are local, history's are not
    repos = { dir, full, shallow, partial: clone.status === 0 ? partial : null };
    return repos;
}
test.after(() => { if (repos) fs.rmSync(repos.dir, { recursive: true, force: true }); });

function run(repo, rev, scope, names, extra = []) {
    const args = ['history', '--repo', repo, '--rev', rev, ...extra, ...names.flatMap((n) => ['--name', n]), '--', ...scope];
    const r = spawnSync(exe, args, { encoding: 'utf8', maxBuffer: 1 << 28 });
    assert.equal(r.status, 0, r.stderr);
    return JSON.parse(r.stdout);
}

/** An event as the truth writes it: label, carry, path, name, detail (a merged block's survivor as "->name"). */
const line = (e) => `${e.message.split(':')[0]}|${e.carry}|${e.path}|${e.name}|${e.carry === 'merged' ? '->' + e.mergedInto : e.detail}`;
const wanted = (ev) => `${ev[0]}|${ev[1]}|${ev[2]}|${ev[3]}|${ev[5] ? '->' + ev[5] : ev[4] ?? ''}`;

for (const sc of truth.scenarios) {
    test(`git history: ${sc.name}`, { skip }, async () => {
        const r = await repositories();
        const repo = sc.source === 'shallow' ? r.shallow : r.full;
        const rev = sc.revision === 'c200' ? 'main' : idOfLabel(r.full, sc.revision);
        const out = run(repo, rev, sc.scope, sc.functions.map((f) => f.name), sc.maxRevisions ? ['--max', String(sc.maxRevisions)] : []);
        assert.equal(out.historyCut, !!sc.historyCut, 'history cut');
        for (const f of sc.functions) {
            const found = out.functions.find((x) => x.name === f.name);
            assert.equal(found.blocks.length, 1, `${f.name} is one block at ${sc.revision}`);
            assert.deepEqual(found.blocks[0].events.map(line), f.events.map(wanted), `${sc.name} / ${f.name}`);
        }
    });
}

test('git history asks git for the scope, never the tree (R-SCALE-9, A21)', { skip }, async () => {
    const r = await repositories();
    const out = run(r.full, 'main', ['src/numeric.cpp'], ['clamp']);
    const log = out.commands.filter((c) => c.startsWith('log '));
    assert.equal(log.length, 2, 'the log of the path, then of the old name after the rename chase (and the one-commit probe is cached)');
    assert.ok(log.every((c) => c.includes(' -- "src/numeric.cpp"') || c.includes('"src/mathx.cpp"')), 'every log is limited to the scope');
    assert.ok(!out.commands.some((c) => c.startsWith('rev-list') || c.startsWith('ls-tree')), 'no whole-history or whole-tree walk');
    assert.equal(out.stats.commitsReplayed, 5, 'five of the 200 revisions touched the path or its old name');
    assert.equal(out.stats.filesRead, 5);
    assert.equal(out.commands.filter((c) => c.startsWith('cat-file')).length, 1, 'texts come in one batch');
    assert.equal(out.commands.filter((c) => c.startsWith('diff-tree')).length, out.stats.changesCalls, 'a whole diff only where a file was added');
    const dir = run(r.full, 'main', ['src/'], ['clamp'], ['--checkpoint', '50']);
    assert.equal(dir.stats.updates, 84, 'updates are exactly the changes under src/ (a rename is two)');
    assert.ok(dir.stats.commitsReplayed < 200 && dir.stats.commitsReplayed === 77);
});

test('git history of a partial clone: missing texts are unknown, the view renders (R-SCALE-7, A20)', { skip }, async () => {
    const r = await repositories();
    if (!r.partial) return;   // the local server did not offer a blob filter
    const out = run(r.partial, 'main', ['src/'], ['normalize', 'clamp'], ['--blocks']);
    assert.ok(out.unknown.length > 0, 'the historical blobs are not there, and no process fetched them');
    assert.ok(out.blocks.length > 0 && out.functions.every((f) => f.blocks.length >= 1), 'what is known is still shown');
    assert.ok(!out.commands.some((c) => c.startsWith('fetch')), 'nothing was fetched');
});

test('git history refuses what it must not pass to git, and an unscoped request', { skip }, async () => {
    const r = await repositories();
    for (const args of [['history', '--repo', r.full], ['history', '--repo', r.full, 'src;x'], ['history', '--repo', r.full, '--rev', 'HEAD&x', 'src'], ['history', '--repo', r.full, '--bogus', 'src']]) {
        const res = spawnSync(exe, args, { encoding: 'utf8' });
        assert.notEqual(res.status, 0, args.join(' '));
    }
});
