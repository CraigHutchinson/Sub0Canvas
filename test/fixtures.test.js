// The committed fixtures are exactly what the generator writes, and the viewer scripts parse.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');

const root = path.join(__dirname, '..');

function tree(dir, prefix = '') {
    const out = {};
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = prefix ? `${prefix}/${e.name}` : e.name;
        if (e.isDirectory()) Object.assign(out, tree(path.join(dir, e.name), rel));
        else out[rel] = fs.readFileSync(path.join(dir, e.name), 'utf8');
    }
    return out;
}

test('fixtures are up to date with tools/gen-fixtures.mjs', () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'sub0canvas-'));
    try {
        // The generator also rewrites fixtures/demo.js; compare it before and after so a stale bundle fails here too.
        const bundle = fs.readFileSync(path.join(root, 'fixtures', 'demo.js'), 'utf8');
        execFileSync(process.execPath, [path.join(root, 'tools', 'gen-fixtures.mjs'), tmp]);
        assert.deepEqual(tree(tmp), tree(path.join(root, 'fixtures', 'demo', 'attempt-1')), 'run: npm run fixtures:demo');
        assert.equal(fs.readFileSync(path.join(root, 'fixtures', 'demo.js'), 'utf8'), bundle, 'run: npm run fixtures:demo');
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
});

test('the viewer scripts parse as classic scripts (they must run from file://)', () => {
    for (const f of ['src/model.js', 'src/view.js', 'fixtures/demo.js']) new vm.Script(fs.readFileSync(path.join(root, f), 'utf8'), { filename: f });
});

test('the bundled demo holds the same files as fixtures/demo', () => {
    const sandbox = { window: {} };
    vm.runInNewContext(fs.readFileSync(path.join(root, 'fixtures', 'demo.js'), 'utf8'), sandbox);
    const bundled = Object.fromEntries(sandbox.window.SUB0CANVAS_DEMO.map((e) => [e.path, e.text]));
    assert.deepEqual(bundled, tree(path.join(root, 'fixtures', 'demo')));
});
