// Tests of the model against the demo attempt (fixtures/demo/attempt-1), read from disk the way the folder picker reads it.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const M = require('../src/model.js');

const root = path.join(__dirname, '..', 'fixtures', 'demo');

function readTree(dir, prefix = '') {
    const out = [];
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = prefix ? `${prefix}/${e.name}` : e.name;
        if (e.isDirectory()) out.push(...readTree(path.join(dir, e.name), rel));
        else out.push({ path: rel, text: fs.readFileSync(path.join(dir, e.name), 'utf8') });
    }
    return out;
}

const entries = readTree(root);
const { docs, problems } = M.loadEntries(entries);
const doc = docs[0];
const turn = (n) => M.turnOf(doc, n);
const item = (n, layer, id) => turn(n).layers[layer].find((it) => it.id === id);

test('the demo loads as one attempt of four turns without problems', () => {
    assert.deepEqual(problems, []);
    assert.equal(docs.length, 1);
    assert.equal(doc.name, 'attempt-1');
    assert.deepEqual(doc.turnNumbers, [1, 2, 3, 4]);
    assert.deepEqual(doc.paths, ['src/main.cpp', 'src/parser.cpp', 'tests/test_parser.cpp']);
    for (const t of doc.turns) assert.deepEqual(M.validateTurn(t), [], `turn ${t.turn}`);
});

test('layers are whatever the files hold, in first-seen order', () => {
    assert.deepEqual(M.layerNames(doc), ['issues', 'prose', 'review', 'decisions']);
    assert.equal(turn(1).layers.decisions, undefined);
});

test('every revision of every file is available, mapped back from flattened names', () => {
    assert.deepEqual(Object.keys(doc.revisions['src/parser.cpp']).map(Number), [1, 2, 3, 4, 5, 6]);
    assert.deepEqual(Object.keys(doc.revisions['tests/test_parser.cpp']).map(Number), [1, 2]);
    assert.equal(doc.revisions['src/parser.cpp'][5], turn(3).files['src/parser.cpp'].text);
});

test('block lines in the fixtures match the file text (signature line to closing brace)', () => {
    for (const t of doc.turns)
        for (const b of t.blocks) {
            const lines = M.fileLines(doc, t.turn, b.path);
            assert.equal(lines[b.first - 1].trim(), b.signature, `turn ${t.turn} ${b.id} first`);
            assert.equal(lines[b.last - 1], '}', `turn ${t.turn} ${b.id} last`);
        }
});

test('SYMBOL edit: term keeps its id and its anchors land on its new lines', () => {
    const h = M.blockHistory(doc, 'b4');
    assert.deepEqual(h.timeline.map((t) => t.status), ['live', 'live', 'live', 'live']);
    assert.equal(h.timeline[1].events[0].carry, 'kept');
    assert.equal(h.timeline[1].changed, true);
    assert.deepEqual(h.timeline[1].revisions.map((r) => r.revision), [2, 3, 4]);
    const before = M.resolveId(doc, 1, 'b4').block, after = M.resolveId(doc, 3, 'b4').block;
    assert.notEqual(before.first, after.first, 'the whole-file rewrite moved term');
    const prose = M.fanOut(doc, 3, item(3, 'prose', 'prose-term'));
    assert.equal(prose.anchors[0].resolved.first, after.first);
});

test('rename: number becomes parseNumber under the same id', () => {
    const h = M.blockHistory(doc, 'b2');
    assert.deepEqual(h.names, ['Parser::number', 'Parser::parseNumber']);
    const ev = h.timeline[1].events.find((e) => e.id === 'b2');
    assert.equal(ev.carry, 'renamed');
    assert.equal(ev.detail, 'Parser::number');
    assert.equal(M.findBlocks(doc, 'number')[0].id, 'b2', 'the old name still finds the block');
});

test('whole-file rewrite: a signature change is carried by name (resigned)', () => {
    const h = M.blockHistory(doc, 'b3');
    const ev = h.timeline[2].events.find((e) => e.id === 'b3');
    assert.equal(ev.carry, 'resigned');
    assert.equal(h.timeline[2].block.signature, 'long Parser::factor()');
    assert.deepEqual(h.timeline[2].revisions.map((r) => r.by), ['coder (whole-file rewrite)']);
});

test('merge: items anchored to isDigit follow it into parseNumber', () => {
    const r = M.resolveId(doc, 3, 'b1');
    assert.equal(r.status, 'merged');
    assert.equal(r.block.id, 'b2');
    assert.deepEqual(r.via, ['b1']);
    const hits = M.attachedTo(doc, 3, 'b2');
    const isDigit = hits.find((hit) => hit.item.id === 'prose-isdigit');
    assert.ok(isDigit, 'the isDigit prose is attached to b2');
    assert.equal(isDigit.resolved.status, 'merged');
    assert.deepEqual(M.blockHistory(doc, 'b2').mergedFrom, ['b1']);
    assert.equal(M.blockHistory(doc, 'b1').timeline[2].status, 'merged');
});

test('orphan: printResult retired without a successor leaves its prose orphaned, and revival reattaches it', () => {
    assert.equal(M.resolveId(doc, 3, 'b6').status, 'orphan');
    assert.deepEqual(M.orphans(doc, 3).map((hit) => hit.item.id), ['prose-print']);
    assert.deepEqual(M.orphans(doc, 4), []);
    const h = M.blockHistory(doc, 'b6');
    assert.deepEqual(h.timeline.map((t) => t.status), ['live', 'live', 'retired', 'live']);
    assert.equal(h.timeline[3].events[0].carry, 'revived');
    assert.deepEqual(Object.keys(h.timeline[2].layers), ['prose'], 'orphaned items still show in the history');
    assert.deepEqual(Object.keys(h.timeline[3].layers).sort(), ['decisions', 'prose']);
});

test('a block-less anchor is a path and line', () => {
    const hits = M.lineAnchored(doc, 2);
    assert.equal(hits.length, 1);
    assert.equal(hits[0].resolved.status, 'line');
    assert.equal(hits[0].resolved.line, 2);
    assert.equal(hits[0].resolved.within, null);
});

test('code -> layers: everything anchored to a block, across layers', () => {
    const layers = new Set(M.attachedTo(doc, 1, 'b4').map((hit) => hit.layer));
    assert.deepEqual([...layers].sort(), ['issues', 'prose', 'review']);
    const counts = M.attachmentCounts(doc, 1).get('b4');
    assert.equal(counts.issues, 4, 'three peek sites and the suspect test failure');
});

test('1-N: one issue fans out to eight anchors in five blocks and two files', () => {
    const fan = M.fanOut(doc, 1, item(1, 'issues', 'issue-peek'));
    assert.equal(fan.anchors.length, 8);
    assert.equal(fan.blockCount, 5);
    assert.equal(fan.fileCount, 2);
    assert.deepEqual(fan.files.map((f) => [f.path, f.anchors.length]), [['src/parser.cpp', 6], ['tests/test_parser.cpp', 2]]);
    // Stepping order: by file, then line, then column.
    assert.deepEqual(fan.files[0].anchors.map((a) => `${a.resolved.line}:${a.anchor.column}`), ['18:9', '30:12', '30:29', '30:46', '43:12', '43:29']);
});

test('1-N batch fix: the regex preview changes each distinct line once, and the next turn is that text', () => {
    const it = item(1, 'issues', 'issue-peek');
    const changes = M.previewFix(doc, 1, it);
    assert.equal(changes.length, 5, 'eight anchors on five lines');
    assert.ok(changes.every((c) => c.applies));
    assert.ok(changes.every((c) => !c.after.includes('peek()') && c.after.includes('peekChar()')));
    // The batch-fix revision of each file equals turn 1's text with the fix applied at every anchor.
    for (const p of ['src/parser.cpp', 'tests/test_parser.cpp']) {
        const lines = M.splitLines(doc.revisions[p][1]);
        for (const c of changes.filter((c) => c.path === p)) lines[c.line - 1] = c.after;
        assert.equal(lines.join('\n') + '\n', doc.revisions[p][2], p);
    }
});

test('an item is traced across turns by id: the fixed issue disappears', () => {
    const trace = M.itemTrace(doc, 'issues', item(1, 'issues', 'issue-peek'));
    assert.deepEqual(trace.map((t) => !!t.item), [true, false, false, false]);
    const narrow = M.itemTrace(doc, 'issues', item(3, 'issues', 'issue-narrow'));
    assert.deepEqual(narrow.map((t) => t.fan?.anchors.length ?? 0), [0, 0, 2, 3]);
});

test('duplicates are grouped and unify into one report with the union of anchors', () => {
    const groups = M.duplicateGroups(doc, 1, 'review');
    assert.deepEqual(groups.map((g) => g.map((it) => it.id)), [['review-div0-a', 'review-div0-b']]);
    const u = M.unify('review', groups[0]);
    assert.equal(u.anchors.length, 2, 'the shared anchor counts once');
    assert.deepEqual(u.mergedFrom, ['review#review-div0-a', 'review#review-div0-b']);
    assert.deepEqual(M.duplicateGroups(doc, 4, 'issues').map((g) => g.map((it) => it.id)), [['issue-narrow', 'issue-narrow-expr']]);
    assert.deepEqual(M.duplicateGroups(doc, 4, 'prose'), [], 'distinct prose is not a duplicate');
});

test('validation names what is wrong', () => {
    assert.deepEqual(M.validateTurn({ schema: 'layered-doc-1', turn: 1, files: {}, blocks: [], layers: {} }), []);
    const errs = M.validateTurn({ schema: 'x', turn: 'a', files: { a: { text: 'x', revision: 1 } }, blocks: [{ id: 'b1', path: 'a', first: 5, last: 2 }],
                                  events: [{ carry: 'moved', id: 'b1' }], layers: { issues: [{}] } });
    assert.equal(errs.length, 5, errs.join('\n'));
});

test('loose turn files without folders still load; cumulative events are reduced to per-turn', () => {
    const turns = entries.filter((e) => e.path.includes('/layers/')).map((e) => ({ path: path.basename(e.path), text: e.text }));
    const loose = M.loadEntries(turns);
    assert.equal(loose.docs.length, 1);
    assert.deepEqual(loose.docs[0].turnNumbers, [1, 2, 3, 4]);
    // A producer writing the whole event log each turn: turn 2 starts with all of turn 1's events.
    const t1 = JSON.parse(turns[0].text), t2 = JSON.parse(turns[1].text);
    t2.events = [...t1.events, ...t2.events];
    const cum = M.loadEntries([{ path: 'turn-1.json', text: JSON.stringify(t1) }, { path: 'turn-2.json', text: JSON.stringify(t2) }]);
    assert.equal(M.turnOf(cum.docs[0], 2).events.length, JSON.parse(turns[1].text).events.length);
});

test('several attempts in one folder become several documents', () => {
    const two = entries.flatMap((e) => [{ path: `w-1/items/x/${e.path}`, text: e.text }, { path: `w-1/items/x/${e.path.replace('attempt-1', 'attempt-2')}`, text: e.text }]);
    const loaded = M.loadEntries(two);
    assert.deepEqual(loaded.docs.map((d) => d.name), ['w-1/items/x/attempt-1', 'w-1/items/x/attempt-2']);
});

test('a malformed file is reported, not fatal', () => {
    const loaded = M.loadEntries([...entries, { path: 'attempt-1/layers/turn-9.json', text: '{nope' }]);
    assert.equal(loaded.docs.length, 1);
    assert.equal(loaded.problems.length, 1);
    assert.match(loaded.problems[0], /turn-9\.json/);
});
