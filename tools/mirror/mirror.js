// The synthetic mirror: a seeded generator of a small, invented project ("Fernhall", a desktop photo catalogue) whose repository has the state of a
// work-dominated monorepo (SCALE.md profile B, scaled down) and a topic of ~25 changes with the shape of a real one (CHANGE_VIEWS.md section 1):
// an umbrella spike, carve-outs (some rewritten), supersessions, an integration change, a stack, a feature-branch merge, a port, test-only changes,
// an unrelated refactor sharing files, background trunk churn with drift and rewrites that make spike hunks obsolete.
//
// Pure: no I/O, no clock, no environment. node:crypto and node:zlib only compute bytes (git object ids, PNG chunks), so every output is a function of
// the seed. Commit ids are real git SHA-1s, so `local` mode's repository must reproduce them exactly (tested).
//
// Outputs (docs/MIRROR.md): the project (commits with trees), `mirror-state-1` (forge and tracker state), `mirror-truth-1`
// (what every derivation of CHANGE_VIEWS.md must find), and the DocumentSource / ItemSource shape of BACKBONE.md section 5 over them.
'use strict';

const crypto = require('node:crypto');
const zlib = require('node:zlib');

const STATE_SCHEMA = 'mirror-state-1';
const TRUTH_SCHEMA = 'mirror-truth-1';
const DAY = 86400;
const EPOCH = 1704067200; // 2024-01-01T00:00:00Z: invented history starts here
const WINDOW_DAY = 700;   // the topic's seven weeks start on this day
const WINDOW_DAYS = 50;

// ---- deterministic randomness -------------------------------------------------------------------------------------------------------------------

function makeRng(seed) {
    let a = seed >>> 0;
    const next = () => {                     // mulberry32
        a = (a + 0x6d2b79f5) | 0;
        let t = Math.imul(a ^ (a >>> 15), 1 | a);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    return {
        float: next,
        int: (lo, hi) => lo + Math.floor(next() * (hi - lo + 1)),
        pick: (xs) => xs[Math.floor(next() * xs.length)],
        chance: (p) => next() < p,
        shuffle(xs) {
            const a2 = xs.slice();
            for (let i = a2.length - 1; i > 0; i--) { const j = Math.floor(next() * (i + 1)); [a2[i], a2[j]] = [a2[j], a2[i]]; }
            return a2;
        },
        sample(xs, n) { return this.shuffle(xs).slice(0, n); },
    };
}

// ---- vocabulary: an invented product, unrelated to any real codebase ------------------------------------------------------------------------------

const PRODUCT = { name: 'Fernhall', description: 'An invented desktop photo catalogue: scans folders, indexes photos, shows thumbnails, exports albums.' };
const NS = 'fernhall';
const MODULES = ['library', 'scanner', 'index', 'selection', 'thumbs', 'bridge', 'details', 'storage', 'metadata', 'albums', 'export', 'search'];
const PARTS = ['cache', 'io', 'query', 'jobs'];
const VERBS = ['load', 'scan', 'find', 'count', 'build', 'refresh', 'resolve', 'open', 'apply', 'read', 'queue', 'merge', 'sort', 'filter', 'prepare', 'flush', 'track', 'collect'];
const NOUNS = ['Folder', 'Entry', 'Photo', 'Album', 'Batch', 'Range', 'Cache', 'Slot', 'Thumb', 'Tag', 'Rating', 'Path', 'Record', 'Query', 'Page', 'Job', 'Preview', 'Header'];
const VARS = ['entry', 'record', 'photo', 'batch', 'folder', 'count', 'total', 'cursor', 'page', 'item', 'slot', 'header'];
const OBJS = ['store', 'cache', 'catalogue', 'queue', 'view', 'walker', 'tracker', 'pool'];
const METHODS = ['find', 'load', 'count', 'open', 'read', 'push', 'flush', 'resolve', 'lookup', 'decode', 'fetch', 'attach'];
const FIELDS = ['size', 'id', 'kind', 'flags', 'width', 'height', 'stamp', 'depth'];
const RETURNS = { bool: 'false', int: '0', 'std::size_t': '0', Record: 'Record{}', Page: 'Page{}', void: null };
const PLUGINS = ['raw_import', 'face_hints', 'geo_tags', 'dupe_finder', 'slideshow', 'print_layout', 'web_gallery', 'color_labels', 'batch_rename', 'watermark',
    'calendar_view', 'map_view', 'backup', 'cloud_stub', 'keyword_sets', 'smart_albums', 'histogram', 'lens_stats', 'timeline', 'compare_view'];
const DOCS = ['architecture', 'scanning', 'indexing', 'selection', 'thumbnails', 'storage', 'plugins', 'exporting', 'performance', 'releasing'];
const ICONS = ['album', 'star', 'flag', 'trash', 'folder', 'camera', 'tag', 'search', 'grid', 'list', 'map', 'clock', 'lock', 'cloud', 'export', 'print',
    'rotate', 'crop', 'heart', 'eye', 'pin', 'share', 'filter', 'sort', 'zoom'];
const PRESETS = ['export_web', 'export_print', 'export_archive', 'thumb_small', 'thumb_large', 'scan_fast', 'scan_full', 'grid_dense', 'grid_loose', 'theme_dark',
    'theme_light', 'keys_default', 'keys_alt', 'raw_default', 'jpeg_default', 'sort_date', 'sort_name', 'backup_daily', 'backup_weekly', 'labels_default'];
const LAYOUTS = ['main_window', 'details_panel', 'import_dialog', 'export_dialog', 'compare_view', 'map_view', 'slideshow', 'preferences', 'album_editor', 'onboarding'];
const PEOPLE = [
    ['Ada Wren', 'ada.wren'], ['Bo Lindqvist', 'bo.lindqvist'], ['Cyra Mendez', 'cyra.mendez'], ['Dov Achterberg', 'dov.achterberg'],
    ['Esi Boateng', 'esi.boateng'], ['Faro Ito', 'faro.ito'], ['Gil Navarro', 'gil.navarro'], ['Hana Kowal', 'hana.kowal'],
    ['Iver Sato', 'iver.sato'], ['Juno Park', 'juno.park'],
].map(([name, user]) => ({ name, user, email: `${user}@example.com` }));
const TOPIC_AUTHOR = { name: 'Rin Okafor', user: 'rin.okafor', email: 'rin.okafor@example.com' };
const ORIGIN_AUTHOR = PEOPLE[5];
const MERGER = { name: 'Fernhall Merge Bot', user: 'merge-bot', email: 'merge-bot@example.com' };

const pascal = (s) => s.split('_').map((w) => w[0].toUpperCase() + w.slice(1)).join('');
const snake = (s) => s.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase();

// ---- files and rendering ------------------------------------------------------------------------------------------------------------------------
// A code file is {kind:'code', lang:'cpp'|'h'|'py', path, summary, include, blocks:[{id, name, sig, body:[line], tail}]}; text files hold lines;
// binaries hold bytes. Files and blocks are never mutated: an edit clones what it changes, so every commit's tree can share the rest.

const renderCache = new WeakMap();

function render(f) {
    let r = renderCache.get(f);
    if (r) return r;
    const spans = {};
    let bytes;
    if (f.kind === 'binary') bytes = f.bytes;
    else if (f.kind === 'text') bytes = Buffer.from(f.lines.join('\n') + '\n', 'utf8');
    else {
        // Edge cases live here on purpose: preambles that shift every line, CRLF, a BOM, tab indentation, #if-guarded blocks.
        const L = [...(f.preamble ?? [])];
        const ind = f.indent ?? '    ';
        if (f.lang === 'py') {
            L.push(`"""${f.summary}"""`, '', 'import logging', '', 'log = logging.getLogger(__name__)', '');
            for (const b of f.blocks) {
                L.push('');
                const first = L.length + 1;
                L.push(b.sig, ...b.body.map((l) => ind + l));
                if (b.tail) L.push(ind + b.tail);
                spans[b.id] = [first, L.length];
            }
        } else {
            L.push(`// ${f.path}: ${f.summary}`);
            if (f.lang === 'h') L.push('#pragma once', '', '#include "fernhall/types.h"');
            else L.push(`#include "${f.include}"`);
            L.push('', `namespace ${NS} {`, '');
            for (const b of f.blocks) {
                if (b.guard) L.push(`#if ${b.guard}`);
                const first = L.length + 1;
                L.push(b.sig, '{', ...b.body.map((l) => ind + l));
                if (b.tail) L.push(ind + b.tail);
                L.push('}');
                spans[b.id] = [first, L.length];
                if (b.guard) L.push(`#endif // ${b.guard}`);
                L.push('');
            }
            L.push(`} // namespace ${NS}`);
        }
        const eol = f.eol === 'crlf' ? '\r\n' : '\n';
        bytes = Buffer.from((f.bom ? '\uFEFF' : '') + L.join(eol) + eol, 'utf8');
    }
    r = { bytes, spans, blob: gitHash('blob', bytes) };
    renderCache.set(f, r);
    return r;
}

function gitHash(type, bytes) {
    return crypto.createHash('sha1').update(`${type} ${bytes.length}\0`).update(bytes).digest('hex');
}

/** The git tree id of a whole tree (Map path -> file), with nested directories, sorted as git sorts (a directory compares as "name/"). */
function treeId(tree) {
    const root = new Map();
    for (const [path, f] of tree) {
        const parts = path.split('/');
        let dir = root;
        for (const p of parts.slice(0, -1)) { if (!dir.has(p + '/')) dir.set(p + '/', new Map()); dir = dir.get(p + '/'); }
        dir.set(parts.at(-1), render(f).blob);
    }
    const write = (dir) => {
        const entries = [...dir.entries()].map(([k, v]) => (k.endsWith('/') ? { sortKey: k, name: k.slice(0, -1), mode: '40000', id: write(v) } : { sortKey: k, name: k, mode: '100644', id: v }));
        entries.sort((a, b) => (a.sortKey < b.sortKey ? -1 : a.sortKey > b.sortKey ? 1 : 0));
        const buf = Buffer.concat(entries.map((e) => Buffer.concat([Buffer.from(`${e.mode} ${e.name}\0`, 'utf8'), Buffer.from(e.id, 'hex')])));
        return gitHash('tree', buf);
    };
    return write(root);
}

function commitText(c, parentShas, tree) {
    const sig = (p, t) => `${p.name} <${p.email}> ${t} +0000`;
    return `tree ${tree}\n${parentShas.map((p) => `parent ${p}\n`).join('')}author ${sig(c.author, c.time)}\ncommitter ${sig(c.committer, c.time)}\n\n${c.message}`;
}

function png(rng) {
    const crc = (buf) => {
        let c = ~0;
        for (const b of buf) { c ^= b; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); }
        return ~c >>> 0;
    };
    const chunk = (type, data) => {
        const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
        const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
        const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
        return Buffer.concat([len, td, c]);
    };
    const size = rng.pick([8, 16]);
    const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4); ihdr[8] = 8; ihdr[9] = 2;
    const rgb = [rng.int(0, 255), rng.int(0, 255), rng.int(0, 255)];
    const raw = Buffer.alloc(size * (1 + size * 3));
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) for (let k = 0; k < 3; k++) raw[y * (1 + size * 3) + 1 + x * 3 + k] = (rgb[k] + x * 7 + y * 3) & 255;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ---- edits (hunks) --------------------------------------------------------------------------------------------------------------------------------
// One shape for background churn and for the topic's hunks: append (before the block's tail), replace (a line by its exact text: every generated
// line is unique, so a missing line means the block was rewritten), add (a new block), rename, resign, rewrite, delete, put/remove (whole files).

/** The file holding block `id`: at `path` first, else wherever it moved (a hunk names the block; its path is a hint, as for the core's resolver). */
function locate(tree, path, id) {
    const f = tree.get(path);
    if (f?.kind === 'code' && f.blocks.some((b) => b.id === id)) return path;
    for (const [p, g] of tree) if (g.kind === 'code' && g.blocks.some((b) => b.id === id)) return p;
    return null;
}

function applyOp(tree, op) {
    if (op.op === 'put') { const t = new Map(tree); t.set(op.path, op.file); return { ok: true, tree: t }; }
    if (op.op === 'remove') { if (!tree.has(op.path)) return { ok: false, why: 'no file' }; const t = new Map(tree); t.delete(op.path); return { ok: true, tree: t }; }
    if (op.op === 'movefile') {
        const g = tree.get(op.path);
        if (!g || tree.has(op.to)) return { ok: false, why: 'cannot move file' };
        const t = new Map(); for (const [p, x] of tree) t.set(p === op.path ? op.to : p, p === op.path ? { ...g, path: op.to } : x);
        return { ok: true, tree: t };
    }
    if (op.op === 'header') {
        const t = new Map(tree);
        for (const [p, g] of tree) if (g.kind === 'code') t.set(p, { ...g, preamble: g.lang === 'py' ? op.lines.map((l) => `# ${l}`) : op.lines.map((l) => `// ${l}`) });
        return { ok: true, tree: t };
    }
    const path = op.block && op.op !== 'add' ? locate(tree, op.path, op.block) : op.path;
    if (!path) return { ok: false, why: `no block ${op.block}` };
    const f = tree.get(path);
    if (!f) return { ok: false, why: `no file ${op.path}` };
    if (f.kind === 'text' && op.op === 'lines') { const t = new Map(tree); t.set(path, { ...f, lines: op.lines(f.lines) }); return { ok: true, tree: t }; }
    if (f.kind !== 'code') return { ok: false, why: 'not code' };
    if (op.op === 'move') {
        const b = f.blocks.find((x) => x.id === op.block), dest = tree.get(op.to);
        if (!dest || dest.kind !== 'code' || op.to === path) return { ok: false, why: 'cannot move block' };
        const t = new Map(tree);
        t.set(path, { ...f, blocks: f.blocks.filter((x) => x.id !== op.block) });
        t.set(op.to, { ...dest, blocks: [...dest.blocks, { ...b, name: b.name.replace(/^\w+::/, `${op.cls}::`), sig: b.sig.replace(/ \w+::/, ` ${op.cls}::`) }] });
        return { ok: true, tree: t };
    }
    if (op.op === 'regen') { const t = new Map(tree); t.set(path, { ...f, blocks: f.blocks.map(op.block2) }); return { ok: true, tree: t }; }
    const i = f.blocks.findIndex((b) => b.id === op.block);
    if (op.op === 'add') {
        if (locate(tree, op.path, op.block)) return { ok: false, why: `block ${op.block} exists` };
        const blocks = f.blocks.slice();
        blocks.push({ id: op.block, name: op.name, sig: op.sig, body: op.body.slice(), tail: op.tail, ...(op.guard ? { guard: op.guard } : {}) });
        const t = new Map(tree); t.set(path, { ...f, blocks }); return { ok: true, tree: t };
    }
    if (i < 0) return { ok: false, why: `no block ${op.block}` };
    const b = f.blocks[i];
    let nb;
    switch (op.op) {
        case 'append': nb = { ...b, body: [...b.body, ...op.lines] }; break;
        case 'replace': {
            const k = b.body.indexOf(op.old);
            if (k < 0) return { ok: false, why: 'line gone' };
            nb = { ...b, body: [...b.body.slice(0, k), op.new, ...b.body.slice(k + 1)] }; break;
        }
        case 'removeLine': {
            const k = b.body.indexOf(op.line);
            if (k < 0) return { ok: false, why: 'line gone' };
            nb = { ...b, body: [...b.body.slice(0, k), ...b.body.slice(k + 1)] }; break;
        }
        case 'rename': nb = { ...b, name: op.name, sig: op.sig }; break;
        case 'resign': nb = { ...b, sig: op.sig }; break;
        case 'rewrite': nb = { ...b, body: op.body.slice() }; break;
        case 'delete': { const blocks = f.blocks.filter((x) => x.id !== op.block); const t = new Map(tree); t.set(path, { ...f, blocks }); return { ok: true, tree: t }; }
        default: return { ok: false, why: `unknown op ${op.op}` };
    }
    const blocks = f.blocks.slice(); blocks[i] = nb;
    const t = new Map(tree); t.set(path, { ...f, blocks });
    return { ok: true, tree: t };
}

function applyAll(tree, ops, what) {
    let t = tree;
    for (const op of ops) {
        const r = applyOp(t, op);
        if (!r.ok) throw new Error(`mirror: ${what}: ${op.op} ${op.path} ${op.block ?? ''}: ${r.why}`);
        t = r.tree;
    }
    return t;
}

// ---- the generator ------------------------------------------------------------------------------------------------------------------------------

/**
 * Generate the mirror for `seed`. Returns {seed, product, commits, branches, state, truth, treeAt(sha), source, items}.
 * Deterministic: the same seed gives byte-identical outputs and the same commit ids on every machine.
 */
function generateMirror({ seed = 1 } = {}) {
    const rng = makeRng(seed);
    let lit = 100;                                   // every generated statement carries a fresh literal, so every line is unique
    let blockSeq = 0;
    const newBlockId = () => `b${++blockSeq}`;

    // -- statements and blocks --
    const cppLine = () => {
        const n = lit++, v = rng.pick(VARS), o = rng.pick(OBJS), m = rng.pick(METHODS), fl = rng.pick(FIELDS);
        return rng.pick([
            `auto ${v} = ${o}.${m}(id, ${n});`,
            `if (!${o}.${m}(${v}, ${n})) return {};`,
            `for (const auto& it : ${v}s) total += it.${fl}() * ${n};`,
            `${o}.${m}(${v}, ${n});`,
            `const int ${v}${n} = ${n} * ${o}.${fl}();`,
        ]);
    };
    const pyLine = () => {
        const n = lit++, v = rng.pick(VARS), o = rng.pick(OBJS), m = rng.pick(METHODS), fl = rng.pick(FIELDS);
        return rng.pick([
            `${v} = ${o}.${m}(item_id, ${n})`,
            `total += ${v}.${fl} * ${n}`,
            `${o}.${m}(${v}, ${n})`,
            `log.debug("${m} %s", ${n})`,
        ]);
    };
    const cppBlock = (cls, inline) => {
        const ret = rng.pick(Object.keys(RETURNS));
        const name = rng.pick(VERBS) + rng.pick(NOUNS);
        const params = rng.pick(['', 'RecordId id', 'const Batch& batch', 'RecordId id, int flags', 'const Path& path']);
        const sig = inline ? `inline ${ret} ${name}(${params})` : `${ret} ${cls}::${name}(${params})`;
        const body = Array.from({ length: rng.int(3, 9) }, cppLine);
        return { id: newBlockId(), name: inline ? name : `${cls}::${name}`, sig, body, tail: RETURNS[ret] === null ? null : `return ${RETURNS[ret]};` };
    };
    const pyBlock = () => {
        const name = snake(rng.pick(VERBS) + rng.pick(NOUNS));
        const sig = `def ${name}(${rng.pick(['', 'item_id', 'batch', 'item_id, flags=0', 'path'])}):`;
        return { id: newBlockId(), name, sig, body: Array.from({ length: rng.int(2, 7) }, pyLine), tail: rng.pick(['return total', 'return None', 'return True']) };
    };
    const uniqueNames = (blocks) => { const seen = new Set(); return blocks.filter((b) => (seen.has(b.name) ? false : (seen.add(b.name), true))); };
    const sidecar = (path) => ({ kind: 'text', path: path + '.sidecar', lines: [`sidecar: 1`, `guid: ${crypto.createHash('sha1').update(`${seed}:${path}`).digest('hex').slice(0, 32)}`, `importer: ${path.split('.').pop()}`, `labels: []`] });

    // -- the initial tree --
    const tree0 = new Map();
    const put = (f) => tree0.set(f.path, f);
    for (const mod of MODULES) {
        const cls = pascal(mod);
        put({ kind: 'code', lang: 'h', path: `core/${mod}/${mod}.h`, summary: `${cls} declarations and inline helpers`, blocks: uniqueNames(Array.from({ length: rng.int(2, 4) }, () => cppBlock(cls, true))) });
        put({ kind: 'code', lang: 'cpp', path: `core/${mod}/${mod}.cpp`, summary: `${cls}`, include: `${mod}/${mod}.h`, blocks: uniqueNames(Array.from({ length: rng.int(4, 8) }, () => cppBlock(cls))) });
        for (const part of rng.sample(PARTS, rng.int(2, 3))) {
            put({ kind: 'code', lang: 'cpp', path: `core/${mod}/${mod}_${part}.cpp`, summary: `${cls}: ${part}`, include: `${mod}/${mod}.h`, blocks: uniqueNames(Array.from({ length: rng.int(3, 6) }, () => cppBlock(cls))) });
        }
        put({ kind: 'code', lang: 'py', path: `tests/test_${mod}.py`, summary: `Tests for ${mod}.`, blocks: uniqueNames(Array.from({ length: rng.int(3, 6) }, pyBlock)) });
    }
    put({ kind: 'code', lang: 'h', path: 'core/fernhall/types.h', summary: 'shared types', blocks: uniqueNames(Array.from({ length: 3 }, () => cppBlock('Types', true))) });
    for (const p of PLUGINS) put({ kind: 'code', lang: 'py', path: `plugins/${p}.py`, summary: `The ${p.replace(/_/g, ' ')} plugin.`, blocks: uniqueNames(Array.from({ length: rng.int(2, 5) }, pyBlock)) });
    for (const p of ['test_scan_speed', 'test_thumb_speed']) put({ kind: 'code', lang: 'py', path: `tests/perf/${p}.py`, summary: 'Performance test.', blocks: [pyBlock()] });
    for (const d of DOCS) put({ kind: 'text', path: `docs/${d}.md`, lines: [`# ${pascal(d)}`, '', `Notes on ${d} in ${PRODUCT.name}.`, ''] });
    put({ kind: 'text', path: 'README.md', lines: [`# ${PRODUCT.name}`, '', PRODUCT.description, '', 'Generated by Sub0Canvas tools/mirror: invented, for testing.'] });
    put({ kind: 'text', path: 'CMakeLists.txt', lines: ['cmake_minimum_required(VERSION 3.24)', `project(${NS} CXX)`, `file(GLOB_RECURSE SOURCES core/*.cpp)`, `add_library(${NS} \${SOURCES})`] });
    put({ kind: 'text', path: 'pyproject.toml', lines: ['[project]', `name = "${NS}-plugins"`, 'version = "2.0.0"'] });
    for (const i of ICONS) { const p = `assets/icons/${i}.png`; put({ kind: 'binary', path: p, bytes: png(rng) }); put(sidecar(p)); }
    for (const pr of PRESETS) { const p = `presets/${pr}.json`; put({ kind: 'text', path: p, lines: ['{', `  "name": "${pr}",`, `  "quality": ${rng.int(50, 100)},`, `  "size": ${rng.pick([256, 512, 1024, 2048])}`, '}'] }); put(sidecar(p)); }
    for (const l of LAYOUTS) { const p = `layouts/${l}.layout`; put({ kind: 'text', path: p, lines: [`layout: ${l}`, `panels: ${rng.int(1, 5)}`, `split: ${rng.int(20, 80)}`, `dock: ${rng.pick(['left', 'right', 'bottom'])}`] }); put(sidecar(p)); }

    // Repository edge cases present from the start (truth.edgeCases names each): a large generated file nobody edits by hand; CRLF with a BOM; tabs
    // and non-ASCII text; overloads (one name, two blocks); a block inside #if.
    const genRow = (k, salt) => ({ id: newBlockId(), name: `Schema::row${k}`, sig: `const SchemaRow& Schema::row${k}()`,
        body: [`static const SchemaRow r{${k}, ${(k * 7919 + salt) % 100003}, "${['photo', 'album', 'tag', 'face', 'place'][k % 5]}"};`], tail: 'return r;' });
    put({ kind: 'code', lang: 'cpp', path: 'core/generated/schema_tables.cpp', summary: 'GENERATED by schemagen from schema/catalogue.yaml: do not edit', include: 'generated/schema.h',
        generated: true, blocks: Array.from({ length: 400 }, (_, k) => genRow(k, 0)) });
    put({ kind: 'code', lang: 'py', path: 'plugins/legacy_import.py', summary: 'Legacy importer, kept with Windows line endings.', eol: 'crlf', bom: true, blocks: uniqueNames(Array.from({ length: 3 }, pyBlock)) });
    {
        const blocks = uniqueNames(Array.from({ length: 4 }, () => cppBlock('Albums')));
        blocks[0] = { ...blocks[0], body: ['// Gr\u00fc\u00dfe an die Kalibrierung: na\u00efve caf\u00e9 ordering, \u00bd steps', ...blocks[0].body] };
        put({ kind: 'code', lang: 'cpp', path: 'core/albums/albums_legacy.cpp', summary: 'Albums: legacy layout (tab-indented)', include: 'albums/albums.h', indent: '\t', blocks });
    }
    {
        const sel = tree0.get('core/selection/selection.cpp');
        // pinned: churn never renames, rewrites or deletes these, so every seed keeps the edge case
        const over = (params) => ({ id: newBlockId(), name: 'Selection::countEntries', sig: `std::size_t Selection::countEntries(${params})`, body: [cppLine(), cppLine()], tail: 'return 0;', pinned: true });
        const guarded = { id: newBlockId(), name: 'Selection::dumpProfile', sig: 'void Selection::dumpProfile()', body: [cppLine(), cppLine()], tail: null, guard: 'FERNHALL_PROFILING', pinned: true };
        put({ ...sel, blocks: [...sel.blocks, over('const Batch& batch'), over('RecordId id, int flags'), guarded] });
    }

    const initialBlocks = new Set();
    for (const f of tree0.values()) if (f.kind === 'code') for (const b of f.blocks) initialBlocks.add(b.id);

    // -- commits --
    const commits = [];                              // in creation order; each {sha, parents, branch, author, committer, time, message, tree, kind}
    const bySha = new Map();
    const branches = new Map();                      // name -> sha
    const makeCommit = ({ branch, parents, tree, author, committer, time, message, kind = 'trunk', change = null }) => {
        const tid = treeId(tree);
        const c = { branch, parents, author, committer: committer ?? author, time, message: message.endsWith('\n') ? message : message + '\n', tree, treeId: tid, kind, change };
        c.sha = gitHash('commit', Buffer.from(commitText(c, parents, tid), 'utf8'));
        commits.push(c); bySha.set(c.sha, c); branches.set(branch, c.sha);
        return c;
    };
    const tip = (b) => bySha.get(branches.get(b));
    const t = (day, hour = 0) => EPOCH + Math.round(day * DAY + hour * 3600);

    makeCommit({ branch: 'main', parents: [], tree: tree0, author: PEOPLE[0], time: t(0, 9), message: `Initial import of ${PRODUCT.name}` });

    // -- background churn --
    const protectedLines = new Set();                // `${block}\0${line}`: lines topic hunks replace; churn never touches them
    const protectedBlocks = new Set();               // blocks with protected lines: never rewritten or deleted by churn
    const codeBlocks = (tree, pred = () => true) => {
        const out = [];
        for (const f of tree.values()) if (f.kind === 'code' && pred(f)) for (const b of f.blocks) out.push({ f, b });
        return out;
    };
    const lineFor = (f) => (f.lang === 'py' ? pyLine() : cppLine());
    function churnOp(tree, bias) {
        const roll = rng.float();
        if (roll < 0.62) {
            const pool = codeBlocks(tree, (f) => !f.generated && (!bias || bias.some((m) => f.path.startsWith(`core/${m}/`))));
            const { f, b } = rng.pick(pool.length ? pool : codeBlocks(tree, (f) => !f.generated));
            const r2 = b.pinned ? 0 : rng.float();   // a pinned edge-case block only ever grows
            const area = f.path.startsWith('core/') ? f.path.split('/')[1] : f.path.split('/')[0];
            if (r2 < 0.5) return { op: { op: 'append', path: f.path, block: b.id, lines: [lineFor(f)] }, msg: `${pascal(area)}: ${rng.pick(VERBS)} ${rng.pick(NOUNS).toLowerCase()} after ${rng.pick(VERBS)}` };
            if (r2 < 0.72) {
                const free = b.body.filter((l) => !protectedLines.has(`${b.id}\0${l}`));
                if (free.length) { const old = rng.pick(free); return { op: { op: 'replace', path: f.path, block: b.id, old, new: lineFor(f) }, msg: `${pascal(area)}: fix ${rng.pick(NOUNS).toLowerCase()} ${rng.pick(['bounds', 'ordering', 'rounding', 'lifetime'])}` }; }
            }
            if (r2 < 0.82) {
                const nb = f.lang === 'py' ? pyBlock() : cppBlock(pascal(f.path.split('/')[1] ?? 'Mod'), f.lang === 'h');
                if (f.blocks.some((x) => x.name === nb.name)) return null;
                return { op: { op: 'add', path: f.path, block: nb.id, name: nb.name, sig: nb.sig, body: nb.body, tail: nb.tail }, msg: `${pascal(area)}: add ${nb.name.split('::').pop()}` };
            }
            if (r2 < 0.89 && f.lang !== 'h') {
                const nn = f.lang === 'py' ? snake(rng.pick(VERBS) + rng.pick(NOUNS)) : rng.pick(VERBS) + rng.pick(NOUNS);
                const short = b.name.split('::').pop();
                if (f.blocks.some((x) => x.name.split('::').pop() === nn)) return null;
                return { op: { op: 'rename', path: f.path, block: b.id, name: b.name.replace(short, nn), sig: b.sig.replace(short + '(', nn + '(') }, msg: `${pascal(area)}: rename ${short} to ${nn}` };
            }
            if (r2 < 0.94 && !protectedBlocks.has(b.id)) return { op: { op: 'rewrite', path: f.path, block: b.id, body: Array.from({ length: rng.int(3, 8) }, () => lineFor(f)) }, msg: `${pascal(area)}: rewrite ${b.name.split('::').pop()}` };
            if (!protectedBlocks.has(b.id) && f.blocks.length > 2 && rng.chance(0.5)) return { op: { op: 'delete', path: f.path, block: b.id }, msg: `${pascal(area)}: remove unused ${b.name.split('::').pop()}` };
            return { op: { op: 'append', path: f.path, block: b.id, lines: [lineFor(f)] }, msg: `${pascal(area)}: log ${rng.pick(NOUNS).toLowerCase()} timing` };
        }
        if (roll < 0.76) { const d = rng.pick(DOCS); return { op: { op: 'lines', path: `docs/${d}.md`, lines: (ls) => [...ls, `* ${rng.pick(VERBS)} ${rng.pick(NOUNS).toLowerCase()}s (note ${lit++}).`] }, msg: `Docs: ${d} notes` }; }
        if (roll < 0.86) { const pr = rng.pick(PRESETS); const q = rng.int(40, 100); return { op: { op: 'lines', path: `presets/${pr}.json`, lines: (ls) => ls.map((l) => (l.includes('"quality"') ? `  "quality": ${q},` : l)) }, msg: `Presets: tune ${pr}` }; }
        const name = `${rng.pick(ICONS)}_${lit++}`; const p = `assets/icons/${name}.png`;
        return { ops: [{ op: 'put', path: p, file: { kind: 'binary', path: p, bytes: png(rng) } }, { op: 'put', path: p + '.sidecar', file: sidecar(p) }], msg: `Assets: add ${name} icon` };
    }
    function churn(branch, time, bias) {
        for (let tries = 0; tries < 10; tries++) {
            const c = churnOp(tip(branch).tree, bias);
            if (!c) continue;
            const ops = c.ops ?? [c.op];
            let tr = tip(branch).tree, ok = true;
            for (const op of ops) { const r = applyOp(tr, op); if (!r.ok) { ok = false; break; } tr = r.tree; }
            if (!ok) continue;
            return makeCommit({ branch, parents: [branches.get(branch)], tree: tr, author: rng.pick(PEOPLE), time, message: c.msg });
        }
        throw new Error('mirror: churn found no applicable edit');
    }

    // Before the window: two years of trunk, two release branches with a few backports.
    const preTimes = Array.from({ length: 150 }, () => rng.float() * (WINDOW_DAY - 2) + 1).sort((a, b) => a - b);
    const relAt = { 'release/2.0': 60, 'release/2.1': 115 };
    const releaseTimes = [];
    preTimes.forEach((d, i) => {
        churn('main', t(d), null);
        for (const [rel, at] of Object.entries(relAt)) if (i === at) {
            branches.set(rel, branches.get('main'));
            for (let k = 0; k < 3; k++) releaseTimes.push({ rel, day: d + 1 + rng.float() * (WINDOW_DAY - d - 2) });
        }
    });
    for (const { rel, day } of releaseTimes.sort((a, b) => a.day - b.day)) {
        const { f, b } = rng.pick(codeBlocks(tip(rel).tree, (ff) => ff.lang === 'cpp' && !ff.generated));
        makeCommit({ branch: rel, parents: [branches.get(rel)], tree: applyAll(tip(rel).tree, [{ op: 'append', path: f.path, block: b.id, lines: [cppLine()] }], 'backport'),
            author: rng.pick(PEOPLE), time: t(day), message: `[2.x] Backport: guard ${rng.pick(NOUNS).toLowerCase()} ${rng.pick(['overflow', 'reentry', 'null read'])}` });
    }
    // Releases were cut before the window; the window's backports are not modelled (the port PR stands for them).
    const windowStart = tip('main');

    // -- the topic plan, against trunk at the window's start --
    const W = windowStart.tree;
    const releaseTree = tip('release/2.1').tree;
    const topicFiles = (mods) => [...W.values()].filter((f) => f.kind === 'code' && f.lang === 'cpp' && mods.some((m) => f.path.startsWith(`core/${m}/`)));
    const takenBlocks = new Set();
    const variantNew = (old, isPy, variant) => {
        const swapped = old.replace(/\.(\w+)\(/, (_, m) => (isPy ? `.peek_${m}(` : `.peek${m[0].toUpperCase()}${m.slice(1)}(`));
        const base = swapped === old ? (isPy ? `${old}  # resident only` : `${old} // resident only`) : swapped;
        return variant ? (isPy ? `${base}  # no load` : `${base} // no load`) : base;
    };
    const appendLine = (isPy, n, variant) => (isPy ? `tracker.skip_unloaded(${n}${variant ? ', eager=False' : ''})` : `tracker.skipUnloaded(${n}${variant ? ', /*eager=*/false' : ''});`);
    const lineages = new Map();                      // lineage id -> {id, group, op template}
    let lin = 0;
    function makeGroup(group, mods, count, { kinds = ['replace', 'replace', 'append'], fromInitial = true, alsoIn = null } = {}) {
        const files = rng.shuffle(topicFiles(mods));
        const out = [];
        for (let k = 0; out.length < count && k < 400; k++) {
            const f = files[k % files.length];
            const relaxed = k >= 200;                // a small module ran out of untouched blocks: share a block, never a line
            // A ported hunk must also apply on the release branch: only lines that exist there too.
            const linesThere = (b) => {
                const rb = alsoIn ? alsoIn.get(f.path)?.blocks?.find((x) => x.id === b.id) : b;
                return rb ? b.body.filter((l) => rb.body.includes(l) && !protectedLines.has(`${b.id}\0${l}`)) : [];
            };
            const cands = f.blocks.filter((b) => (!fromInitial || initialBlocks.has(b.id)) && (relaxed || !takenBlocks.has(b.id)) && linesThere(b).length >= 2);
            if (!cands.length) continue;
            const b = rng.pick(cands);
            takenBlocks.add(b.id); protectedBlocks.add(b.id);
            const kind = rng.pick(kinds);
            const id = `L${String(++lin).padStart(2, '0')}`;
            let tmpl;
            if (kind === 'replace') {
                const old = rng.pick(linesThere(b));
                protectedLines.add(`${b.id}\0${old}`);
                tmpl = { op: 'replace', path: f.path, block: b.id, old, isPy: f.lang === 'py' };
            } else tmpl = { op: 'append', path: f.path, block: b.id, n: lit++, isPy: f.lang === 'py' };
            const L = { id, group, tmpl, name: b.name };
            lineages.set(id, L); out.push(L);
        }
        if (out.length < count) throw new Error(`mirror: group ${group} found ${out.length} of ${count} blocks`);
        return out;
    }
    const hunk = (L, variant = 0) => {
        const m = L.tmpl;
        if (m.op === 'replace') return { lineage: L.id, op: 'replace', path: m.path, block: m.block, old: m.old, new: variantNew(m.old, m.isPy, variant), variant };
        if (m.op === 'append') return { lineage: L.id, op: 'append', path: m.path, block: m.block, lines: [appendLine(m.isPy, m.n, variant)], variant };
        return { lineage: L.id, ...m, variant };
    };
    // A born block (an API added by the topic), one identity per lineage.
    const libFile = `core/library/library.cpp`;
    const apiId = newBlockId();
    const apiL = { id: `L${String(++lin).padStart(2, '0')}`, group: 'E', name: 'Library::peekRecord',
        tmpl: { op: 'add', path: libFile, block: apiId, name: 'Library::peekRecord', sig: 'bool Library::peekRecord(RecordId id)', body: [`const auto* slot = catalogue.slotOf(id, ${lit++});`, 'if (!slot) return false;'], tail: 'return slot->resident();' } };
    lineages.set(apiL.id, apiL);
    const residentId = newBlockId();
    const residentL = { id: `L${String(++lin).padStart(2, '0')}`, group: 'P', name: 'Storage::isResident',
        tmpl: { op: 'add', path: 'core/storage/storage.cpp', block: residentId, name: 'Storage::isResident', sig: 'bool Storage::isResident(RecordId id)', body: [`const auto* page = pool.pageOf(id, ${lit++});`], tail: 'return page && page->mapped();' } };
    lineages.set(residentL.id, residentL);

    const G = {
        A: makeGroup('A', ['scanner'], 3), B: makeGroup('B', ['selection'], 4), C: makeGroup('C', ['selection', 'details'], 4), D: makeGroup('D', ['thumbs'], 3),
        E: [apiL, ...makeGroup('E', ['library'], 2, { alsoIn: releaseTree })], F: makeGroup('F', ['bridge'], 3), K: makeGroup('K', ['library', 'selection', 'details'], 9),
        M: makeGroup('M', ['selection'], 3), H: makeGroup('H', ['index'], 5, { kinds: ['replace'] }), Gd: makeGroup('Gd', ['details'], 5), P: [residentL, ...makeGroup('P', ['storage'], 3)],
    };
    const ownGroup = (name, mods, count, alsoIn = null) => makeGroup(name, mods, count, { kinds: ['append', 'replace'], alsoIn });
    const own = { X1: ownGroup('X1', ['scanner'], 1), C4: ownGroup('C4', ['metadata', 'library'], 3), C5: ownGroup('C5', ['thumbs', 'library'], 2, releaseTree),
        C10: ownGroup('C10', ['scanner', 'storage'], 3), K1x: ownGroup('K1x', ['details'], 1), ORIG: ownGroup('ORIG', ['scanner'], 1) };
    // C5 depends on C3's API: its new lines call peekRecord, which C3 introduces.
    const c5Hunks = own.C5.map((L) => { const h = hunk(L); return h.op === 'replace' ? { ...h, new: `if (!library.peekRecord(id)) return {}; // ${L.tmpl.old.match(/\d+/)?.[0] ?? ''}` } : { ...h, lines: [`if (!library.peekRecord(id)) continue; // ${L.tmpl.n}`] }; });
    const H = (L, v) => L.map((x) => hunk(x, v));
    const [K1, K2, K3] = [G.K.slice(0, 3), G.K.slice(3, 5), G.K.slice(5)];

    // ---- edge cases: real-world situations the canvas may not support yet. The fixture exists to push those boundaries, so each is recorded in
    // truth.edgeCases with what it stresses and whether today's design covers it ('handled' built, 'designed' on paper, 'gap' neither).
    const edgeCases = [];
    const edge = (id, what, where, today, design) => edgeCases.push({ id, what, where, today, design });
    const mkL = (group, f, b) => {
        const id = `L${String(++lin).padStart(2, '0')}`;
        protectedBlocks.add(b.id); takenBlocks.add(b.id);
        const L = { id, group, name: b.name, tmpl: { op: 'append', path: f.path, block: b.id, n: lit++, isPy: false } };
        lineages.set(id, L); return L;
    };
    const selW = W.get('core/selection/selection.cpp');
    const overB = selW.blocks.filter((b) => b.name === 'Selection::countEntries')[1];
    const guardB = selW.blocks.find((b) => b.guard);
    const OV = mkL('OV', selW, overB), GU = mkL('GU', selW, guardB);
    // Two open changes replacing one line differently: a true conflict.
    const cfFile = rng.pick(topicFiles(['search']));
    const cfBlock = rng.pick(cfFile.blocks.filter((b) => initialBlocks.has(b.id) && !takenBlocks.has(b.id) && b.body.length >= 2));
    const cfOld = rng.pick(cfBlock.body);
    protectedLines.add(`${cfBlock.id}\0${cfOld}`); protectedBlocks.add(cfBlock.id); takenBlocks.add(cfBlock.id);
    const cfHunk = (tag, call) => ({ lineage: tag, op: 'replace', path: cfFile.path, block: cfBlock.id, old: cfOld, new: `const auto hits = index.${call}(ids, ${lit++});` });
    // An assets-only change: binaries and their sidecars, no code block at all.
    const iconPaths = rng.sample([...W.keys()].filter((p) => p.startsWith('assets/icons/') && p.endsWith('.png')), 3).sort();
    const asOps = iconPaths.flatMap((p) => [{ lineage: 'AS1', op: 'put', path: p, file: { kind: 'binary', path: p, bytes: png(rng) } },
        { lineage: 'AS1', op: 'lines', path: p + '.sidecar', lines: (ls) => ls.map((l) => (l.startsWith('labels:') ? 'labels: [selection]' : l)) }]);
    // A stale change opened 300 days before the window, still open: its base is far behind trunk.
    const staleHunk = { lineage: 'ST1', op: 'append', path: G.K[0].tmpl.path, block: G.K[0].tmpl.block, lines: [`tracker.legacyWarm(${lit++});`] };
    // An evil merge: the merge commit of C7 carries a line that is in neither parent.
    const evilTarget = rng.pick(codeBlocks(W, (f) => f.path.startsWith('core/selection/') && f.lang === 'cpp').filter(({ b }) => !protectedBlocks.has(b.id)));
    protectedBlocks.add(evilTarget.b.id);
    const evilOp = { op: 'append', path: evilTarget.f.path, block: evilTarget.b.id, lines: [`selection.flushPending(${lit++}); // resolved while merging`] };

    // Test-only and unrelated changes edit their own files and blocks.
    const perfFile = { kind: 'code', lang: 'py', path: 'tests/perf/test_select_many.py', summary: 'Performance test: select many photos.', blocks: [pyBlock(), pyBlock()] };
    const reproFile = { kind: 'code', lang: 'py', path: 'tests/test_preview_dock_repro.py', summary: 'Reproducer: docked preview outliving its parent.', blocks: [pyBlock()] };
    const t2Target = rng.pick(codeBlocks(W, (f) => f.path === 'tests/test_thumbs.py'));
    protectedBlocks.add(t2Target.b.id);
    const t2Hunk = { lineage: 'T2', op: 'append', path: t2Target.f.path, block: t2Target.b.id, lines: ['if not fixture.uses_atlas: return None'] };
    // The refactor: a new tags module, and appends in many existing files (four of them the spike's), never in the spike's blocks.
    const spikeFiles = new Set(Object.values(G).flat().map((L) => L.tmpl.path));
    const refOps = [];
    for (const part of ['tags', 'tags_store', 'tags_query']) {
        const p = `core/tags/${part}.cpp`;
        refOps.push({ lineage: 'REF', op: 'put', path: p, file: { kind: 'code', lang: 'cpp', path: p, summary: `Tags: ${part}`, include: 'tags/tags.h', blocks: Array.from({ length: 3 }, () => cppBlock('Tags')) } });
    }
    const refFiles = rng.shuffle([...W.values()].filter((f) => f.kind === 'code' && f.lang === 'cpp' && !f.generated && !spikeFiles.has(f.path))).slice(0, 20)
        .concat(rng.sample([...spikeFiles].filter((p) => W.get(p)), 4).map((p) => W.get(p)));
    for (const f of refFiles) {
        const free = f.blocks.filter((b) => !takenBlocks.has(b.id) && !protectedBlocks.has(b.id));
        if (!free.length) continue;
        const b = rng.pick(free);
        protectedBlocks.add(b.id);
        refOps.push({ lineage: 'REF', op: 'append', path: f.path, block: b.id, lines: [`tags::touch(${lit++});`] });
    }

    const KEY = 'CAT-1042', KEY2 = 'CAT-1107', KEY_REF = 'CAT-0990', KEY_PORT = 'CAT-1188';
    const C = (key, o) => ({ key, author: TOPIC_AUTHOR, topic: KEY, labels: [], draft: false, ...o });
    // day offsets within the window; commits: [[day, hunks]]
    const plan = [
        C('ORIG', { author: ORIGIN_AUTHOR, kind: 'origin', title: `Draft: faster scans of very large folders, with investigation notes`, open: 0.2, base: 'main', draft: true,
            commits: [[0.2, [...H(G.A.slice(0, 2)), ...H(G.B.slice(0, 2)), ...H(own.ORIG), { lineage: 'DOC', op: 'put', path: 'docs/investigation-large-folders.md', file: { kind: 'text', path: 'docs/investigation-large-folders.md', lines: ['# Large folder scans', '', 'Where the time goes, measured.', ''] } }]]], close: 9 }),
        C('S1', { kind: 'spike', title: `SPIKE-NO-MERGE [${KEY}] Library scan stalls on large folders: reproducer, spikes, background scan`, open: 1, base: 'main', draft: true, labels: ['spike'],
            commits: [[1, H([...G.A, ...G.B, ...G.C, ...G.D, OV, GU])], [5, H([...G.E, ...G.F])], [11, H([...G.K, ...G.M])], [19, H(G.H)]] }),
        C('C1', { kind: 'carve', title: `[${KEY}] Per-folder scan fixes from the investigation draft, updated to trunk`, open: 1.5, base: 'main', draft: true, commits: [[1.5, [...H(G.A), ...H(own.X1)]]], close: 26 }),
        C('C2', { kind: 'carve', title: `[${KEY}] Stop selection code from reading every selected photo`, open: 2, base: 'main', draft: true, commits: [[2, H([...G.B, ...G.C])], [6, H(G.D)]], close: 27 }),
        C('C3', { kind: 'carve', title: `[${KEY}] Add photo queries that do not open the file`, open: 7, base: 'main', commits: [[7, H(G.E)]], merge: 20 }),
        C('INT', { kind: 'integration', title: `[${KEY}] Integrate the remaining scan fixes`, open: 7.5, base: 'main', draft: true, commits: [[7.5, H(G.M)], [36.5, H(G.K)]] }),
        C('C4', { kind: 'carve', title: `[${KEY}] Answer type questions about photos without opening them`, open: 9, base: 'feature/scan-v2', commits: [[9, H(own.C4)]], merge: 12 }),
        C('C5', { kind: 'carve', title: `[${KEY}] Skip the decode for thumbnails the cached type rules out`, open: 25, base: 'main', commits: [[25, c5Hunks]], merge: 35 }),
        C('T1', { kind: 'test', title: `[${KEY}] Add a performance test for selecting many photos`, open: 30, base: 'main', commits: [[30, [{ lineage: 'T1', op: 'put', path: perfFile.path, file: perfFile }]]], merge: 33 }),
        C('C6', { kind: 'carve', title: `[${KEY}] Stop selection history reading selected photos`, open: 30.2, base: 'main', commits: [[30.2, H(G.B, 1)]], merge: 34 }),
        C('T2', { kind: 'test', title: `Limit atlas test setup to atlas tests`, open: 30.4, base: 'main', commits: [[30.4, [t2Hunk]]], merge: 31 }),
        C('C7', { kind: 'carve', title: `[${KEY}] Classify the global selection without reading photos`, open: 30.6, base: 'main', commits: [[30.6, H(G.C, 1)]], merge: 35, evil: evilOp }),
        C('C8', { kind: 'carve', title: `[${KEY}] Stop thumbnail tracking reading selected photos`, open: 30.8, base: 'main', commits: [[30.8, H(G.D)]], merge: 34 }),
        C('C9', { kind: 'carve', title: `[${KEY}] Stop panel overlays reading selected photos`, open: 31, base: 'main', commits: [[31, H(G.F)]], merge: 36 }),
        C('S2', { kind: 'spike', topic: KEY2, title: `SPIKE-NO-MERGE [${KEY2}] Let storage decide whether a record is resident`, open: 32, base: 'main', draft: true, labels: ['spike'], commits: [[32, H(G.P)]] }),
        C('S3', { kind: 'spike', title: `SPIKE-NO-MERGE [${KEY}] Keep the details panel responsive with thousands selected`, open: 32.3, base: 'main', draft: true, labels: ['spike'], commits: [[32.3, H(G.Gd)]] }),
        C('C10', { kind: 'carve', title: `[${KEY}] Let a scanner thread hand the store lock to the UI thread`, open: 32.6, base: 'main', draft: true, commits: [[32.6, H(own.C10)]], close: 40 }),
        C('C11', { kind: 'carve', title: `[${KEY}] Compute the details footer once per selection`, open: 33, base: 'main', commits: [[33, H(G.Gd.slice(0, 3))]], merge: 38 }),
        C('REF', { kind: 'refactor', topic: KEY_REF, author: PEOPLE[2], title: `[${KEY_REF}] Move tag storage into its own module (unblocks ${KEY2})`, open: 35, base: 'main', commits: [[35, refOps]], merge: 37 }),
        C('K1', { kind: 'carve', title: `[${KEY}] Build the shared tracker only when something reads it`, open: 36, base: 'main', commits: [[36, [...H(K1), ...H(own.K1x)]]] }),
        C('K2', { kind: 'carve', title: `[${KEY}] Let the panels skip unread photos without a details view`, open: 36.2, base: 'K1', draft: true, commits: [[36.2, H(K2)]] }),
        C('C12', { kind: 'carve', title: `[${KEY}] Count the selection without reading selected photos`, open: 36.4, base: 'main', commits: [[36.4, H(G.M)]] }),
        C('K3', { kind: 'carve', title: `[${KEY}] Keep selected photos unread when a rescan rebuilds the selection`, open: 36.6, base: 'K2', draft: true, commits: [[36.6, H(K3)]] }),
        C('C13', { kind: 'carve', topic: KEY, title: `[${KEY}] Add Storage::isResident to check a record exists without loading it`, open: 38, base: 'main', commits: [[38, H(G.P.slice(0, 2))]], merge: 39 }),
        C('PORT', { kind: 'port', title: `[${KEY_PORT}] Port: photo queries and thumbnail decode skip to 2.1`, open: 40, base: 'release/2.1', draft: true, commits: [[40, [...H(G.E, 1), ...c5Hunks]]] }),
        C('R1', { kind: 'spike', title: `SPIKE-NO-MERGE [${KEY}] Reproduce docked preview outliving its parent`, open: 45, base: 'main', draft: true, labels: ['spike'], commits: [[45, [{ lineage: 'R1', op: 'put', path: reproFile.path, file: reproFile }]]], close: 46 }),
        // edge-case changes
        C('ST1', { kind: 'carve', topic: 'CAT-0731', author: PEOPLE[4], title: `[CAT-0731] Warm the tracker cache on startup`, open: 400 - WINDOW_DAY, baseAt: 400, base: 'main', commits: [[400.5 - WINDOW_DAY, [staleHunk]]] }),
        C('CF1', { kind: 'carve', topic: KEY2, author: PEOPLE[1], title: `[${KEY2}] Batch the search index lookups`, open: 39, base: 'main', commits: [[39, [cfHunk('CF1', 'lookupBatch')]]] }),
        C('CF2', { kind: 'carve', title: `[${KEY}] Serve search lookups from the resident cache`, open: 39.5, base: 'main', commits: [[39.5, [cfHunk('CF2', 'lookupCached')]]] }),
        C('AS1', { kind: 'assets', topic: KEY, author: PEOPLE[8], title: `[${KEY}] Refresh the selection icons`, open: 41, base: 'main', commits: [[41, asOps]], merge: 41.5 }),
        C('C8R', { kind: 'carve', title: `[${KEY}] Reland: stop thumbnail tracking reading selected photos`, open: 42, base: 'main', commits: [[42, H(G.D, 1)]], merge: 45 }),
    ];
    const truthLinks = [
        ['C1', 'carvedFrom', 'S1'], ['C2', 'carvedFrom', 'S1'], ['C3', 'carvedFrom', 'S1'], ['C6', 'carvedFrom', 'S1'], ['C7', 'carvedFrom', 'S1'], ['C8', 'carvedFrom', 'S1'],
        ['C9', 'carvedFrom', 'S1'], ['K1', 'carvedFrom', 'S1'], ['K2', 'carvedFrom', 'S1'], ['K3', 'carvedFrom', 'S1'], ['C12', 'carvedFrom', 'S1'], ['C11', 'carvedFrom', 'S3'],
        ['C13', 'carvedFrom', 'S2'], ['C1', 'carvedFrom', 'ORIG'], ['S1', 'continues', 'ORIG'],
        ['C6', 'supersedes', 'C2'], ['C7', 'supersedes', 'C2'], ['C8', 'supersedes', 'C2'],
        ['INT', 'integrates', 'C12'], ['INT', 'integrates', 'K1'], ['INT', 'integrates', 'K2'], ['INT', 'integrates', 'K3'],
        ['INT', 'duplicates', 'C12'],
        ['K2', 'stackedOn', 'K1'], ['K3', 'stackedOn', 'K2'],
        ['C5', 'dependsOn', 'C3'],
        ['PORT', 'ports', 'C3'], ['PORT', 'ports', 'C5'],
        ['T1', 'tests', 'topic'], ['T2', 'tests', 'topic'], ['R1', 'tests', 'topic'],
        ['CF1', 'conflictsWith', 'CF2'], ['C8R', 'relands', 'C8'], ['AS1', 'partOf', 'topic'],
    ];

    // -- the window: churn, deliberate rewrites (obsolete hunks), drift, and the topic's changes, in time order --
    const events = [];
    const windowTimes = Array.from({ length: 100 }, () => rng.float() * WINDOW_DAYS);
    for (const d of windowTimes) events.push({ day: d, type: 'churn', bias: rng.chance(0.35) ? ['library', 'selection', 'details', 'index', 'thumbs'] : null });
    G.H.slice(0, 2).forEach((L, i) => events.push({ day: 22 + i * 3, type: 'rewrite', L }));
    events.push({ day: 8, type: 'feature-branch', name: 'feature/scan-v2' });
    const trunkEdit = (day, ops, message, author = PEOPLE[6]) => events.push({ day, type: 'trunk', ops, message, author });
    trunkEdit(15, [{ op: 'header', lines: ['Copyright (c) the Fernhall contributors.', 'SPDX-License-Identifier: MIT'] }], 'Add a licence header to every source file');
    trunkEdit(24, () => {
        const L = G.H[2], tr = tip('main').tree, from = locate(tr, L.tmpl.path, L.tmpl.block);
        const to = [...tr.keys()].filter((p) => p.startsWith('core/index/') && p.endsWith('.cpp') && p !== from).sort()[0];
        return [{ op: 'move', path: from, block: L.tmpl.block, to, cls: 'Index' }];
    }, 'Index: move page lookups next to the page table');
    trunkEdit(26, [{ op: 'regen', path: 'core/generated/schema_tables.cpp', block2: (b) => ({ ...b, body: [b.body[0].replace(/, (\d+), "/, (_, x) => `, ${(Number(x) * 31 + 7) % 100003}, "`)] }) }], 'Regenerate schema tables');
    trunkEdit(28, () => { const p = G.Gd[0].tmpl.path; return [{ op: 'movefile', path: p, to: p.replace('core/details/', 'core/details/panel/') }]; }, 'Details: move panel sources under panel/');
    trunkEdit(30, () => (tip('main').tree.has('docs/performance.md') ? [{ op: 'movefile', path: 'docs/performance.md', to: 'docs/Performance.md' }] : []), 'Docs: capitalise Performance.md');
    trunkEdit(38, () => {
        const tr = tip('main').tree, p = locate(tr, G.K[0].tmpl.path, G.K[0].tmpl.block);
        const b = tr.get(p).blocks.find((x) => x.id === G.K[0].tmpl.block), short = b.name.split('::').pop();
        return [{ op: 'rename', path: p, block: b.id, name: b.name.replace(short, `${short}Lazily`), sig: b.sig.replace(short + '(', `${short}Lazily(`) }];
    }, 'Library: name the lazy tracker path');
    events.push({ day: 40, type: 'revert', key: 'C8' });
    events.push({ day: 42, type: 'rebase', key: 'K1' });
    events.push({ day: 44, type: 'backmerge', key: 'C12' });
    for (const ch of plan) {
        events.push({ day: ch.open, type: 'open', ch });
        ch.commits.forEach(([day, hunks], i) => events.push({ day: day + 0.01, type: 'push', ch, hunks, first: i === 0 }));
        if (ch.merge !== undefined) events.push({ day: ch.merge, type: 'merge', ch });
        if (ch.close !== undefined) events.push({ day: ch.close, type: 'close', ch });
    }
    events.sort((a, b) => a.day - b.day || (a.type === 'open' ? -1 : 0) - (b.type === 'open' ? -1 : 0));

    const changeState = new Map();                   // key -> {branch, base, number, state, head, opened, merged, closed, pushes}
    let prNumber = 400;
    const branchOf = (ch) => `${ch.author.user.split('.')[0]}/${ch.topic.toLowerCase()}-${ch.key.toLowerCase()}-${ch.title.replace(/^.*\] /, '').replace(/^Draft: /, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').slice(0, 40).replace(/-$/, '')}`;
    for (const ev of events) {
        const time = t(WINDOW_DAY + ev.day);
        if (ev.type === 'churn') churn('main', time, ev.bias);
        else if (ev.type === 'rewrite') {
            const m = ev.L.tmpl;
            const f = tip('main').tree.get(m.path);
            const body = Array.from({ length: 4 }, () => lineFor(f));
            makeCommit({ branch: 'main', parents: [branches.get('main')], tree: applyAll(tip('main').tree, [{ op: 'rewrite', path: m.path, block: m.block, body }], 'rewrite'),
                author: PEOPLE[7], time, message: `Index: rewrite ${ev.L.name.split('::').pop()} around the new page table` });
        } else if (ev.type === 'feature-branch') branches.set(ev.name, branches.get('main'));
        else if (ev.type === 'trunk') {
            const ops = typeof ev.ops === 'function' ? ev.ops() : ev.ops;
            if (ops.length) makeCommit({ branch: 'main', parents: [branches.get('main')], tree: applyAll(tip('main').tree, ops, ev.message), author: ev.author, time, message: ev.message });
        } else if (ev.type === 'revert') {
            const s = changeState.get(ev.key), ch = plan.find((c) => c.key === ev.key);
            const inverse = ch.commits.flatMap(([, h]) => h).reverse().flatMap((h) => (h.op === 'replace' ? [{ ...h, old: h.new, new: h.old }] : h.lines.map((line) => ({ op: 'removeLine', path: h.path, block: h.block, line }))));
            makeCommit({ branch: 'main', parents: [branches.get('main')], tree: applyAll(tip('main').tree, inverse, `revert ${ev.key}`), author: PEOPLE[9], time,
                message: `Revert "${ch.title}"\n\nThis reverts the merge of pull request ${s.number}: thumbnails flicker on rescan.` });
            s.reverted = time; s.revertSha = branches.get('main');
        } else if (ev.type === 'rebase') {
            const s = changeState.get(ev.key), ch = plan.find((c) => c.key === ev.key);
            const c = makeCommit({ branch: s.branch, parents: [branches.get('main')], tree: applyAll(tip('main').tree, ch.commits.flatMap(([, h]) => h), `rebase ${ev.key}`),
                author: ch.author, time, kind: 'change', change: ch.key, message: ch.title });
            s.pushes.push(c.sha); s.head = c.sha; s.rebased = time; s.baseSha = c.parents[0];
        } else if (ev.type === 'backmerge') {
            const s = changeState.get(ev.key), ch = plan.find((c) => c.key === ev.key);
            const c = makeCommit({ branch: s.branch, parents: [s.head, branches.get('main')], tree: applyAll(tip('main').tree, ch.commits.flatMap(([, h]) => h), `back-merge ${ev.key}`),
                author: ch.author, time, kind: 'change', change: ch.key, message: `Merge branch 'main' into ${s.branch}` });
            s.pushes.push(c.sha); s.head = c.sha; s.backMerged = time;
        } else if (ev.type === 'open') {
            const ch = ev.ch;
            const baseRef = branches.has(ch.base) ? ch.base : changeState.get(ch.base).branch;
            const baseSha = ch.baseAt !== undefined ? commits.filter((c) => c.branch === 'main' && c.time <= t(ch.baseAt)).at(-1).sha : branches.get(baseRef);
            changeState.set(ch.key, { branch: branchOf(ch), baseBranch: baseRef, baseSha, number: prNumber++, state: 'open', opened: time, pushes: [] });
        } else if (ev.type === 'push') {
            const s = changeState.get(ev.ch.key);
            const parent = ev.first ? s.baseSha : branches.get(s.branch);
            const tree = applyAll(bySha.get(parent).tree, ev.hunks, `push ${ev.ch.key}`);
            const c = makeCommit({ branch: s.branch, parents: [parent], tree, author: ev.ch.author, time, kind: 'change', change: ev.ch.key,
                message: ev.first ? ev.ch.title.replace(/^SPIKE-NO-MERGE /, '') : `Address review: ${rng.pick(VERBS)} ${rng.pick(NOUNS).toLowerCase()}s` });
            s.pushes.push(c.sha); s.head = c.sha;
        } else if (ev.type === 'merge') {
            const s = changeState.get(ev.ch.key);
            const target = s.baseBranch;
            const hunks = ev.ch.commits.flatMap(([, h]) => h);
            const tree = applyAll(tip(target).tree, ev.ch.evil ? [...hunks, ev.ch.evil] : hunks, `merge ${ev.ch.key}`);
            // What landed is now someone's code: churn must not rewrite it out from under a later revert.
            for (const h of hunks) {
                if (h.op === 'replace') protectedLines.add(`${h.block}\0${h.new}`);
                if (h.op === 'append') for (const l of h.lines) protectedLines.add(`${h.block}\0${l}`);
                if (h.block) protectedBlocks.add(h.block);
            }
            makeCommit({ branch: target, parents: [branches.get(target), s.head], tree, author: ev.ch.author, committer: MERGER, time, kind: 'merge', change: ev.ch.key,
                message: `Merge pull request ${s.number} from ${s.branch}\n\n${ev.ch.title}` });
            s.state = 'merged'; s.merged = time; s.mergeSha = branches.get(target);
        } else if (ev.type === 'close') { const s = changeState.get(ev.ch.key); s.state = 'closed'; s.closed = time; }
    }

    // -- truth --
    const mainTree = tip('main').tree;
    const holders = new Map();                        // lineage -> [change keys] (non-spike)
    for (const ch of plan) {
        if (ch.kind === 'spike' || ch.kind === 'origin') continue;
        for (const [, hs] of ch.commits) for (const h of hs) if (h.lineage && lineages.has(h.lineage)) {
            if (!holders.has(h.lineage)) holders.set(h.lineage, new Set());
            holders.get(h.lineage).add(ch.key);
        }
    }
    const isMainMerge = (k) => changeState.get(k).state === 'merged' && changeState.get(k).baseBranch === 'main' && !changeState.get(k).reverted;
    const spikeCoverage = {};
    for (const sp of plan.filter((c) => c.kind === 'spike')) {
        spikeCoverage[sp.key] = sp.commits.flatMap(([, hs]) => hs).filter((h) => lineages.has(h.lineage)).map((h) => {
            const hs = [...(holders.get(h.lineage) ?? [])];
            let state;
            if (hs.some(isMainMerge)) state = 'landed';
            else if (hs.some((k) => changeState.get(k).state === 'open')) state = 'inFlight';
            else if (hs.length) state = 'dropped';
            else state = applyOp(mainTree, h).ok ? 'notCarved' : 'obsolete';
            const pathNow = h.block ? locate(mainTree, h.path, h.block) : h.path;
            return { lineage: h.lineage, block: h.block, path: h.path, ...(pathNow && pathNow !== h.path ? { pathNow } : {}), op: h.op, state, heldBy: hs.sort() };
        });
    }
    const footprint = (ch) => {
        const hs = ch.commits.flatMap(([, h]) => h);
        return { blocks: [...new Set(hs.filter((h) => h.block).map((h) => h.block))].sort(), files: [...new Set(hs.map((h) => h.path))].sort() };
    };
    const blockNames = {};
    for (const f of mainTree.values()) if (f.kind === 'code') for (const b of f.blocks) blockNames[b.id] = { path: f.path, name: b.name };
    for (const L of lineages.values()) if (!blockNames[L.tmpl.block]) blockNames[L.tmpl.block] = { path: L.tmpl.path, name: L.name, onMain: false };

    const changesTruth = {};
    for (const ch of plan) {
        const s = changeState.get(ch.key);
        const fp = footprint(ch);
        changesTruth[ch.key] = { number: s.number, kind: ch.kind, topic: ch.topic, state: s.state, base: s.baseBranch, footprint: fp.blocks, files: fp.files,
            lineages: [...new Set(ch.commits.flatMap(([, h]) => h).map((h) => h.lineage).filter((l) => lineages.has(l)))].sort(),
            rewritten: ch.commits.some(([, h]) => h.some((x) => x.variant)),
            ...(s.reverted ? { revertedBy: s.revertSha } : {}), ...(s.rebased ? { rebased: true } : {}), ...(s.backMerged ? { backMerged: true } : {}) };
    }
    const truth = {
        schema: TRUTH_SCHEMA, seed, topic: KEY, product: PRODUCT.name,
        notes: 'Identity is per lineage: a hunk equivalence class. Born blocks keep one id across the changes that add them. Hunk states take the first that holds of landed (a holder merged into main), inFlight (a holder open), dropped (only closed holders), obsolete (no holder and it no longer applies to main), notCarved.',
        changes: changesTruth,
        links: truthLinks.map(([from, kind, to]) => ({ from, kind, to })),
        spikeCoverage,
        blocks: blockNames,
        edgeCases,
        notModelled: ['symbolic links and submodules (git modes 120000, 160000)', 'large-file pointers (LFS)', 'two paths differing only by case in one tree',
            'committed conflict markers', 'a topic spanning several repositories (U10)', 'review comments replayed on a forge', 'hunks split across two carve-outs'],
    };
    const commitBy = (msg) => commits.find((c) => c.branch === 'main' && c.message.startsWith(msg))?.sha ?? null;
    const st = (k) => changeState.get(k);
    edge('overloads', 'two blocks share one name in one file; a spike hunk edits the second', { path: selW.path, blocks: [overB.id, ...selW.blocks.filter((b) => b.name === overB.name && b.id !== overB.id).map((b) => b.id)], lineage: OV.id },
        'designed', 'LAYERED_DOCUMENT.md identity rules (signature first); BLOCK_DISCOVERY.md 4.1');
    edge('guarded-block', 'a block inside #if/#endif; a spike hunk edits it', { path: selW.path, block: guardB.id, lineage: GU.id }, 'gap', 'BLOCK_DISCOVERY.md does not say whether the guard belongs to the span');
    edge('generated-file', 'a 400-block generated file, regenerated wholesale on trunk (every block changes at once)', { path: 'core/generated/schema_tables.cpp', commit: commitBy('Regenerate schema tables') },
        'designed', 'SCALE.md section 3, kinds before blocks (kinds@1); identity under wholesale regeneration is not designed');
    edge('line-endings-and-bom', 'a file with CRLF line endings and a UTF-8 BOM', { path: 'plugins/legacy_import.py' }, 'gap', 'line and column semantics for CRLF and BOM are unspecified');
    edge('tabs-and-non-ascii', 'tab indentation and non-ASCII text (columns differ from bytes and characters)', { path: 'core/albums/albums_legacy.cpp' }, 'gap', 'anchor column units are unspecified (BACKBONE.md 3.2)');
    edge('licence-header', 'one trunk commit prepends two lines to every source file: every line number moves, no block changes', { commit: commitBy('Add a licence header') },
        'designed', 'identity by signature, anchors re-resolved by quote (BACKBONE.md 5.1)');
    edge('block-moved-across-files', 'a block the spike edits moves to another file on trunk; the hunk still applies there', { block: G.H[2].tmpl.block, from: G.H[2].tmpl.path, commit: commitBy('Index: move page lookups') },
        'designed', 'SCALE.md section 4, cross-file moves through the LSH');
    edge('file-moved', 'a file the topic edits moves to a new directory before carve-outs based on it merge', { from: G.Gd[0].tmpl.path, commit: commitBy('Details: move panel sources') },
        'designed', 'git rename detection in per-path replay (SCALE.md section 4)');
    edge('case-only-rename', 'a file renamed by case only (fatal on case-insensitive file systems if both existed)', { from: 'docs/performance.md', to: 'docs/Performance.md', commit: commitBy('Docs: capitalise') },
        'gap', 'path identity on case-insensitive file systems is unspecified');
    edge('rename-under-open-change', 'trunk renames a block that open changes (a stack, the integration change) modify', { block: G.K[0].tmpl.block, commit: commitBy('Library: name the lazy tracker path') },
        'designed', 'identity carries through renames (R-ID); footprints carried to the viewed revision (R-CHG-1)');
    edge('true-conflict', 'two open changes, under different topics, replace the same line differently', { changes: ['CF1', 'CF2'], block: cfBlock.id }, 'designed', 'CHANGE_VIEWS.md section 3');
    edge('revert-and-reland', 'a merged carve-out is reverted on trunk, then relanded by a new change', { changes: ['C8', 'C8R'], revert: st('C8').revertSha },
        'gap', 'CHANGE_VIEWS.md landing coverage has no reverted state; a holder that merged is not enough');
    edge('force-pushed-rebase', 'the bottom of a stack is rebased (history rewritten); the changes above still sit on its old head', { changes: ['K1', 'K2', 'K3'], heads: st('K1').pushes },
        'gap', 'a stale stack and a non-linear change history are not designed');
    edge('back-merge', 'trunk is merged into an open change: its branch now contains unrelated trunk work', { change: 'C12', head: st('C12').head },
        'designed', 'footprints are relative to the merge base (CHANGE_VIEWS.md section 2), which the back-merge moves');
    edge('evil-merge', 'a merge commit carries a line that is in neither parent', { change: 'C7', merge: st('C7').mergeSha, block: evilOp.block, line: evilOp.lines[0] },
        'gap', 'content attributed to no change: blame and footprints miss it');
    edge('assets-only-change', 'a change of binaries and sidecars only: no code block in its footprint', { change: 'AS1', files: iconPaths }, 'gap', 'R-CHG-1 defines footprints as blocks only');
    edge('stale-change', 'a change opened 300 days before the window, still open, its base far behind trunk', { change: 'ST1', base: st('ST1').baseSha }, 'designed', 'drift (CHANGE_VIEWS.md section 3)');
    edge('topic-key-placement', 'a tracker key only in the branch name; a title naming two keys', { changes: ['T2', 'REF'] }, 'designed', 'the topic policy (CHANGE_VIEWS.md section 2)');
    edge('feature-branch-merge', 'a carve-out merged into a feature branch, not trunk', { change: 'C4', branch: 'feature/scan-v2' }, 'designed', 'landing is relative to a target branch');
    edge('port', 'a change of landed hunks onto a release branch, rewritten', { change: 'PORT', branch: 'release/2.1' }, 'designed', 'ports links (CHANGE_VIEWS.md section 2)');

    // -- forge and tracker state --
    const iso = (s) => new Date(s * 1000).toISOString().replace('.000Z', 'Z');
    const pulls = plan.map((ch) => {
        const s = changeState.get(ch.key);
        return { number: s.number, key: ch.key, title: ch.title, body: `${ch.kind === 'spike' ? 'Exploration only: not for merging.\n\n' : ''}Tracker: ${ch.topic}`, author: ch.author.user,
            state: s.state, draft: ch.draft, labels: ch.labels, base: s.baseBranch, head: s.branch, headSha: s.head, baseSha: s.baseSha,
            createdAt: iso(s.opened), mergedAt: s.merged ? iso(s.merged) : null, closedAt: s.closed ? iso(s.closed) : (s.merged ? iso(s.merged) : null), mergeSha: s.mergeSha ?? null,
            commits: s.pushes.length };
    });
    const reviews = [];
    for (const key of ['C6', 'K1', 'C12', 'C3']) {
        const s = changeState.get(key), ch = plan.find((c) => c.key === key);
        const h = ch.commits[0][1].find((x) => x.block);
        const headTree = bySha.get(s.head).tree, at = locate(headTree, h.path, h.block);
        const span = render(headTree.get(at)).spans[h.block];
        reviews.push({ pull: s.number, path: at, line: span[0], commit: s.head, blockId: h.block, author: PEOPLE[3].user,
            body: rng.pick(['Does this still read the photo when the cache is cold?', 'Can this skip the decode entirely?', 'Please add a test for the empty selection.', 'Is the order of these checks deliberate?']),
            state: s.state === 'merged' ? 'resolved' : 'open', at: iso(s.opened + 3600 * 20) });
    }
    const checks = pulls.flatMap((p) => ['build', 'tests', 'perf'].map((name) => ({ pull: p.number, sha: p.headSha, name,
        conclusion: p.state === 'merged' ? 'success' : (p.key.startsWith('S') || p.key === 'R1') && name !== 'build' ? 'failure' : rng.pick(['success', 'success', 'failure']) })));
    const issues = [
        { key: KEY, type: 'bug', title: 'Library scan stalls on folders with tens of thousands of photos', status: 'In Review', labels: ['theme:hang', 'branch:2.1'], links: [{ type: 'is implemented by', key: KEY_PORT }],
          origin: { pull: changeState.get('ORIG').number, document: 'docs/investigation-large-folders.md' } },
        { key: KEY_PORT, type: 'port', title: 'Port for 2.1: library scan stalls on folders with tens of thousands of photos', status: 'Open', labels: ['branch:2.1'], links: [{ type: 'implements', key: KEY }] },
        { key: KEY2, type: 'task', title: 'Let storage decide whether a record is resident', status: 'Open', labels: [], links: [{ type: 'relates to', key: KEY }] },
        { key: KEY_REF, type: 'task', title: 'Move tag storage into its own module', status: 'Done', labels: [], links: [] },
    ];
    const allCommits = commits.map((c) => ({ sha: c.sha, parents: c.parents, branch: c.branch, author: c.author.user, time: iso(c.time), message: c.message.split('\n')[0], kind: c.kind, ...(c.change ? { change: c.change } : {}) }));
    const state = {
        schema: STATE_SCHEMA, seed, product: PRODUCT,
        repository: { defaultBranch: 'main', branches: Object.fromEntries([...branches.entries()].sort()), window: { from: iso(t(WINDOW_DAY)), to: iso(t(WINDOW_DAY + WINDOW_DAYS)) } },
        commits: allCommits, pulls, reviews, checks, issues,
    };

    // -- the DocumentSource / ItemSource shape (BACKBONE.md section 5) --
    const ancestors = (sha) => { const seen = new Set(); const st = [sha]; while (st.length) { const s = st.pop(); if (!s || seen.has(s)) continue; seen.add(s); st.push(...bySha.get(s).parents); } return seen; };
    const textOf = (f) => (f.kind === 'binary' ? null : render(f).bytes.toString('utf8'));
    const source = {
        revisions(from, to) {
            const keep = ancestors(to); if (from) for (const s of ancestors(from)) keep.delete(s);
            return commits.filter((c) => keep.has(c.sha)).map((c) => ({ id: c.sha, parents: c.parents, author: c.author.name, time: iso(c.time), message: c.message }));
        },
        files(revision) { const c = bySha.get(revision); return c ? Object.fromEntries([...c.tree].map(([p, f]) => [p, textOf(f)])) : null; },
        changedPaths(a, b) {
            const ta = bySha.get(a).tree, tb = bySha.get(b).tree, out = [];
            for (const [p, f] of tb) if (!ta.has(p) || render(ta.get(p)).blob !== render(f).blob) out.push(p);
            for (const p of ta.keys()) if (!tb.has(p)) out.push(p);
            return out.sort();
        },
        blocks(revision, path) {
            const f = bySha.get(revision)?.tree.get(path);
            if (!f || f.kind !== 'code') return [];
            const sp = render(f).spans;
            return f.blocks.map((b) => ({ id: b.id, path, name: b.name, signature: b.sig, first: sp[b.id][0], last: sp[b.id][1] }));
        },
    };
    const items = { name: () => 'mirror/forge', fetch(cursor) { return cursor === 'end' ? { items: [], events: [], cursor: 'end' } : { items: pulls, events: [], cursor: 'end' }; } };

    return { seed, product: PRODUCT, commits, bySha, branches, state, truth, source, items, render, plan, changeState, windowStart: windowStart.sha };
}

/** Topic and repository statistics, to compare with the anonymised targets (docs/MIRROR.md). */
function shape(m) {
    const { truth, state } = m;
    const main = m.bySha.get(m.branches.get('main')).tree;
    const kinds = {};
    for (const p of main.keys()) {
        const k = p.endsWith('.sidecar') ? 'sidecar' : p.endsWith('.png') ? 'binary' : /\.(cpp|h|py)$/.test(p) ? 'code' : p.endsWith('.md') ? 'docs' : 'data';
        kinds[k] = (kinds[k] ?? 0) + 1;
    }
    const topic = Object.entries(truth.changes);
    const byState = {}, byKind = {};
    for (const [, c] of topic) { byState[c.state] = (byState[c.state] ?? 0) + 1; byKind[c.kind] = (byKind[c.kind] ?? 0) + 1; }
    const fileCount = new Map();
    for (const [, c] of topic) for (const f of c.files) fileCount.set(f, (fileCount.get(f) ?? 0) + 1);
    const s1 = new Set(truth.changes.S1.files);
    const sharing = topic.filter(([k, c]) => k !== 'S1' && c.files.some((f) => s1.has(f))).length;
    const coverage = {};
    for (const [sp, hs] of Object.entries(truth.spikeCoverage)) { coverage[sp] = {}; for (const h of hs) coverage[sp][h.state] = (coverage[sp][h.state] ?? 0) + 1; }
    return {
        files: main.size, kinds, commits: state.commits.length, trunkCommits: state.commits.filter((c) => c.branch === 'main').length, branches: Object.keys(state.repository.branches).length,
        changes: topic.length, byKind, byState, topicFiles: fileCount.size, hotspots: [...fileCount.values()].filter((n) => n >= 4).length,
        sharingFilesWithS1: sharing, refactorSharedWithS1: truth.changes.REF.files.filter((f) => s1.has(f)).length, stackDepth: 3, coverage,
    };
}

module.exports = { generateMirror, shape, applyOp, locate, treeId, render, STATE_SCHEMA, TRUTH_SCHEMA };
