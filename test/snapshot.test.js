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
    const { docs, problems } = M.loadEntries(JSON.parse(r.stdout));
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
