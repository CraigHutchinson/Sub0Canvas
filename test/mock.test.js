// The viewer against the mock squad's attempts (fixtures/mock, docs/MOCK_SQUAD.md section 4, MS1 = B0): every attempt the reference loop persists opens in the
// model with no problems, an item is followed across turns by its `id` while its title changes, per-turn and cumulative events read the same, texts sent once by
// content id read as inline ones, and an attempt of eight turns costs a few times one turn, not eight.
//
// The attempts are generated (consumer/mock_squad, `mock_squad --update`) and checked there against the scenarios' hand-written expectations; this file checks what
// only the viewer's side can: that the files it is given are the format it reads.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const M = require('../src/model.js');
const { readEntries } = require('./support.js');
const { blobId } = require('../tools/format/layered_doc.js');

const root = path.join(__dirname, '..', 'fixtures', 'mock');

const names = fs.readdirSync(root, { withFileTypes: true }).filter((e) => e.isDirectory()).map((e) => e.name).sort();
const entries = (name) => readEntries(path.join(root, name), name);
const load = (name, es = entries(name)) => {
    const { docs, problems } = M.loadEntries(es);
    return { doc: docs[0], docs, problems };
};
const rawTurns = (name) => fs.readdirSync(path.join(root, name, 'layers')).filter((f) => /^turn-\d+\.json$/.test(f))
    .sort((a, b) => parseInt(a.slice(5), 10) - parseInt(b.slice(5), 10))
    .map((f) => ({ file: f, text: fs.readFileSync(path.join(root, name, 'layers', f), 'utf8') }));

test('the mock attempts are present', () => {
    for (const n of ['g1-symbol-fix', 'g3-two-files-one-reply', 'g4-rename-resign-move', 'g5-fix-away-from-diagnostic', 'g7-eight-turns-texts-by-id', 'b10-search-not-found-or-ambiguous', 'b11-write-to-disallowed-path'])
        assert.ok(names.includes(n), n);
    assert.ok(names.length >= 15);
});

for (const name of names) {
    test(`mock ${name} loads as one document with no problems`, () => {
        const { docs, problems } = load(name);
        assert.deepEqual(problems, []);
        assert.equal(docs.length, 1);
        assert.ok(docs[0].turns.length >= 1);
    });

    test(`mock ${name}: the turn files hold the B0 fields, and every block anchor resolves to a live block (R-VWR-7)`, () => {
        for (const { file, text } of rawTurns(name)) {
            const t = JSON.parse(text);
            assert.deepEqual(M.validateTurn(t), [], `${name}/${file}`);
            assert.ok(Number.isInteger(t.eventsFrom), `${file} says where its events start`);
            assert.equal(t.policies.loop, 'mock-loop@1', `${file} records the policies that made it`);
            assert.equal(t.layerMeta.issues.label, 'Issues');
            for (const [p, f] of Object.entries(t.files)) {
                assert.ok(Number.isInteger(f.revision) && f.revisionId === `r${f.revision}`, `${file} ${p}: a revision number and an opaque id`);
                assert.match(f.textId, /^[0-9a-f]{40}$/, `${file} ${p}: a content id`);
            }
            const live = new Set(t.blocks.map((b) => b.id));
            for (const it of t.layers.issues) {
                assert.match(it.id, /^issues\//, `${file} item ${it.number} has an id`);
                for (const a of it.anchors) if (a.blockId) assert.ok(live.has(a.blockId), `${file} anchor ${a.blockId} is live`);
            }
        }
    });

    test(`mock ${name}: every file's text at every turn is the text the canvas kept for that revision`, () => {
        const { doc } = load(name);
        for (const t of doc.turns)
            for (const [p, f] of Object.entries(t.files)) {
                assert.equal(f.text, doc.revisions[p][f.revision], `turn ${t.turn} ${p} r${f.revision}`);
                assert.equal(blobId(f.text), f.textId, `turn ${t.turn} ${p}: the content id is the text's git blob id`);
            }
    });
}

test('mock g4: an item is traced across turns by its id while its title changes (title matching would lose it)', () => {
    const { doc } = load('g4-rename-resign-move');
    assert.equal(doc.turns.length, 4);
    const first = doc.turns[0].layers.issues[0];
    const trace = M.itemTrace(doc, 'issues', first);
    assert.equal(trace.length, 4);
    assert.ok(trace.every((x) => x.item && x.item.id === first.id), 'found at every turn, by id');
    const titles = new Set(trace.map((x) => x.item.title));
    assert.ok(titles.size >= 3, `the title changed across the turns (${[...titles].join(' | ')})`);
    // The same trace by title, as a producer without ids leaves it: the item is lost where its title moved (the line number is in the title).
    const noId = { ...first, id: undefined };
    const byTitle = M.itemTrace(doc, 'issues', noId);
    assert.ok(byTitle.some((x) => !x.item), 'matching by title loses the item');
    // The anchor names the block by identity: one block through a re-signing and a move of its neighbours, at its own columns.
    assert.ok(trace.every((x) => x.fan.anchors[0].anchor.blockId === first.anchors[0].blockId));
    assert.ok(trace.every((x) => x.fan.anchors[0].anchor.column === 28 && x.fan.anchors[0].anchor.endColumn === 34), 'the range of the identifier survives');
    // The identity history of the neighbours: renamed, re-signed, retired and revived by the move to another file.
    const carries = doc.events.map((e) => `${e.carry}:${e.name}`);
    for (const c of ['renamed:safeDivide', 'resigned:area', 'retired:square', 'revived:square']) assert.ok(carries.includes(c), c);
    const renamed = doc.events.find((e) => e.carry === 'renamed');
    assert.equal(M.blockHistory(doc, renamed.id).timeline.at(-1).name, 'safeDivide');
});

test('mock g5: the fix lands away from the diagnostic; the block it named keeps its identity', () => {
    const { doc } = load('g5-fix-away-from-diagnostic');
    const [t0, t1] = doc.turns;
    const area = t0.layers.issues[0].anchors[0].blockId;
    assert.ok(t1.blocks.some((b) => b.id === area && b.name === 'area'));
    assert.ok(t1.blocks.some((b) => b.name === 'legacy'), 'the new function is a new block');
    assert.notEqual(t1.layers.issues[0].id, t0.layers.issues[0].id, 'the compile error is gone and a failing test is a different item');
});

test('mock: per-turn events are contiguous and read in order', () => {
    let attemptsWithEvents = 0;
    for (const name of names) {
        assert.ok(!fs.existsSync(path.join(root, name, 'events.ndjson')), `${name}: block events belong in turns`);
        const turns = rawTurns(name).map(({ text }) => JSON.parse(text));
        if (!turns.length) continue;
        const flat = [];
        for (const t of turns) {
            assert.equal(t.eventsFrom, flat.length, `${name} turn ${t.turn}: eventsFrom follows the previous turn`);
            for (const event of t.events) flat.push(event);
        }
        if (flat.length) ++attemptsWithEvents;
        assert.deepEqual(load(name).doc.events, flat, `${name}: the viewer preserves event order`);
    }
    assert.ok(attemptsWithEvents >= 15);
});

test('mock g7: texts by content id read as inline texts, are carried once, and eight turns cost a few times one', (t) => {
    const name = 'g7-eight-turns-texts-by-id';
    const raws = rawTurns(name);
    assert.equal(raws.length, 8, 'eight turn files');
    const parsed = raws.map((r) => JSON.parse(r.text));
    for (const turn of parsed) for (const f of Object.values(turn.files)) assert.equal(f.text, undefined, 'no inline text');
    assert.equal(Object.keys(parsed[0].texts).length, 8, 'the first turn carries each file once');
    for (const turn of parsed.slice(1)) assert.equal(Object.keys(turn.texts).length, 1, 'a later turn carries only the one text that changed');
    const { doc, problems } = load(name);
    assert.deepEqual(problems, []);
    assert.equal(doc.turns.length, 8);
    assert.equal(doc.turns[7].files['src/f1.cpp'].text, doc.revisions['src/f1.cpp'][2], 'a file fixed in turn 1 reads as its second revision at turn 7');

    const sizes = raws.map((r) => Buffer.byteLength(r.text));
    const byId = sizes.reduce((a, b) => a + b, 0);
    // The same attempt with every text inline, as the squad writes today: each turn's bytes plus the texts it would repeat, minus the table it would not have.
    const inline = parsed.map((turn, i) => {
        const bare = { ...turn, texts: undefined, files: Object.fromEntries(Object.entries(doc.turns[i].files).map(([p, f]) => [p, { ...turn.files[p], text: f.text }])) };
        return Buffer.byteLength(JSON.stringify(bare));
    }).reduce((a, b) => a + b, 0);
    t.diagnostic(`g7: one turn ${sizes[0]} bytes, eight turns by id ${byId} (${(byId / sizes[0]).toFixed(2)}x one turn), the same eight with every text inline ${inline} (${(inline / sizes[0]).toFixed(2)}x)`);
    assert.ok(byId < 4 * sizes[0], `eight turns by id are a few times one turn (${byId} against ${sizes[0]})`);
    assert.ok(inline > 6 * sizes[0], `inline, they are nearly eight times it (${inline})`);
    assert.ok(byId * 2 < inline, 'sending each text once more than halves the attempt');
});

test('the viewer reads a producer that gives only opaque revision ids and content ids', () => {
    const base = { schema: 'layered-doc-1', blocks: [], retired: [], events: [], layers: {} };
    const a = 'int a() { return 1; }\n', b = 'int a() { return 2; }\n';
    const turns = [
        { ...base, turn: 0, files: { 'x/y.cpp': { revisionId: 'c9f1', textId: blobId(a) } }, texts: { [blobId(a)]: a } },
        { ...base, turn: 1, files: { 'x/y.cpp': { revisionId: 'd02e', textId: blobId(b) } }, texts: { [blobId(b)]: b } },
        { ...base, turn: 2, files: { 'x/y.cpp': { revisionId: 'd02e', textId: blobId(b) } } },   // nothing new: refers to a text the turn before carried
    ];
    const es = [...turns.map((t) => ({ path: `att/layers/turn-${t.turn}.json`, text: JSON.stringify(t) })),
        { path: 'att/canvas/x%2Fy.cpp.r1', text: a }, { path: 'att/canvas/x%2Fy.cpp.r2', text: b }];
    const { docs, problems } = M.loadEntries(es);
    assert.deepEqual(problems, []);
    const doc = docs[0];
    assert.deepEqual(doc.turns.map((t) => t.files['x/y.cpp'].revision), [1, 2, 2], 'numbers follow the order the ids first appear in');
    assert.deepEqual(doc.turns.map((t) => t.files['x/y.cpp'].text), [a, b, b]);
    assert.deepEqual(doc.revisions['x/y.cpp'], { 1: a, 2: b }, 'a revision file named with %2F is found, as one named with _ is');
    // A text no turn carries is reported, not fatal.
    const lost = M.loadEntries([{ path: 'att/layers/turn-0.json', text: JSON.stringify({ ...base, turn: 0, files: { 'x.cpp': { revisionId: 'r', textId: 'f'.repeat(40) } } }) }]);
    assert.equal(lost.problems.length, 1);
    assert.match(lost.problems[0], /no turn carries the text/);
    assert.equal(lost.docs[0].turns[0].files['x.cpp'].text, '');
    // Validation says what is missing when a file has neither shape.
    assert.deepEqual(M.validateTurn({ ...base, turn: 0, files: { 'x.cpp': {} } }).length, 2);
});
