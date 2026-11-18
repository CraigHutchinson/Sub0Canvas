// Real producer output (fixtures/golden: Sub0Squad's slice 2 A/B) loads in the viewer's model without problems, and keeps the properties the viewer
// relies on: every item has a stable id, and block anchors resolve to blocks that are live in that turn.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const M = require('../src/model.js');

const root = path.join(__dirname, '..', 'fixtures', 'golden');

function entriesOf(dir, prefix) {
    const out = [];
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = `${prefix}/${e.name}`;
        if (e.isDirectory()) out.push(...entriesOf(path.join(dir, e.name), rel));
        else out.push({ path: rel, text: fs.readFileSync(path.join(dir, e.name), 'utf8') });
    }
    return out;
}

const attempts = fs.readdirSync(root, { withFileTypes: true }).filter((e) => e.isDirectory()).map((e) => e.name).sort();

test('the golden set is present', () => {
    assert.deepEqual(attempts, ['adaptive6-bugs-10-run-1', 'adaptive6-fix-many-run-1', 'on-bugs-6-run-2']);
});

for (const name of attempts) {
    test(`golden ${name} loads as one document with no problems`, () => {
        const { docs, problems } = M.loadEntries(entriesOf(path.join(root, name), name));
        assert.deepEqual(problems, []);
        assert.equal(docs.length, 1);
        assert.ok(docs[0].turns.length >= 1);
    });

    test(`golden ${name}: items have ids and block anchors resolve to live blocks`, () => {
        const dir = path.join(root, name, 'layers');
        for (const f of fs.readdirSync(dir).filter((f) => f.endsWith('.json'))) {
            const t = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
            assert.deepEqual(M.validateTurn(t), [], `${name}/${f}`);
            const live = new Set(t.blocks.map((b) => b.id));
            const issues = t.layers.issues || [];
            assert.ok(issues.length > 0, `${name}/${f} has issues`);
            for (const it of issues) {
                assert.ok(typeof (it.id || it.key) === 'string' && (it.id || it.key).startsWith('issues/'), `${name}/${f} item ${it.number} has a stable id`);
                for (const a of it.anchors) if (a.blockId) assert.ok(live.has(a.blockId), `${name}/${f} anchor ${a.blockId} is live`);
            }
        }
    });
}

test('golden: the adaptive fix-many run passed its verdict through as the gate text', () => {
    const stats = JSON.parse(fs.readFileSync(path.join(root, 'adaptive6-fix-many-run-1', 'agent.json'), 'utf8'));
    assert.equal(stats.issueMode, 2);
    assert.equal(stats.layerShown, 0);
    assert.ok(stats.layerPassed >= 1);
});
