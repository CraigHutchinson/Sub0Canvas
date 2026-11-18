// Sub0Canvas view: renders a layered document and lets you interrogate it. Plain DOM, no framework; every state change re-renders from `state`,
// and the state lives in the URL hash so a view can be linked, reloaded and screenshotted.
//
//   centre   which layer the main pane shows: "code", or any key of `layers` (never a fixed list)
//   block    the selected block id: the inspector shows what is attached to it and its history
//   item     the open layer item (`layer#id`): its callout shows the code at anchor `a` and steps through the fan-out
(function () {
    'use strict';
    const M = window.S0Model;
    const $ = (sel) => document.querySelector(sel);

    const state = { docs: [], doc: null, docIdx: 0, turn: 0, centre: 'code', file: '', block: '', item: '', a: 0, fix: '', trace: '', unified: '', open: new Set() };

    // ---- tiny DOM helper --------------------------------------------------------------------------------------------------------------------------

    function h(tag, attrs, ...kids) {
        const el = document.createElement(tag);
        for (const [k, v] of Object.entries(attrs || {})) {
            if (v === undefined || v === null || v === false) continue;
            if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
            else if (k === 'class') el.className = v;
            else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
            else el.setAttribute(k, v === true ? '' : v);
        }
        for (const kid of kids.flat(Infinity)) if (kid !== null && kid !== undefined && kid !== false) el.append(kid.nodeType ? kid : String(kid));
        return el;
    }

    /** Replace an element's children with `kids`, flattening arrays and dropping null, undefined and false. */
    function put(el, ...kids) {
        el.replaceChildren(...kids.flat(Infinity).filter((k) => k !== null && k !== undefined && k !== false));
    }

    // ---- colours: one per layer, by its first-seen position in the document -------------------------------------------------------------------------

    const PALETTE = 8;
    const layerClass = (layer) => {
        if (layer === 'code') return 'lc-code';
        const i = state.doc ? M.layerNames(state.doc).indexOf(layer) : 0;
        return `lc-${((i % PALETTE) + PALETTE) % PALETTE}`;
    };
    const chip = (layer, text, attrs = {}) => h('span', { class: `chip ${layerClass(layer)} ${attrs.class || ''}`, ...attrs, }, text ?? layer);

    // ---- state <-> hash ---------------------------------------------------------------------------------------------------------------------------

    function writeHash() {
        const p = new URLSearchParams();
        if (state.docIdx) p.set('doc', state.docIdx);
        p.set('turn', state.turn);
        p.set('centre', state.centre);
        if (state.file) p.set('file', state.file);
        if (state.block) p.set('block', state.block);
        if (state.item) p.set('item', state.item);
        if (state.item && state.a) p.set('a', state.a);
        if (state.fix) p.set('fix', state.fix);
        if (state.trace) p.set('trace', state.trace);
        if (state.unified) p.set('unified', state.unified);
        const next = '#' + p.toString();
        if (location.hash !== next) history.replaceState(null, '', next);
    }

    function readHash() {
        const p = new URLSearchParams(location.hash.slice(1));
        state.docIdx = Math.min(Number(p.get('doc') || 0), Math.max(0, state.docs.length - 1));
        state.doc = state.docs[state.docIdx] || null;
        if (!state.doc) return;
        const turns = state.doc.turnNumbers;
        const t = Number(p.get('turn'));
        state.turn = turns.includes(t) ? t : turns[turns.length - 1];
        state.centre = p.get('centre') || 'code';
        state.file = p.get('file') || '';
        state.block = p.get('block') || '';
        state.item = p.get('item') || '';
        state.a = Number(p.get('a') || 0);
        state.fix = p.get('fix') || '';
        state.trace = p.get('trace') || '';
        state.unified = p.get('unified') || '';
    }

    function set(patch) {
        Object.assign(state, patch);
        if ('docIdx' in patch) state.doc = state.docs[state.docIdx];
        render();
    }

    // ---- loading ----------------------------------------------------------------------------------------------------------------------------------

    function load(entries, source) {
        const { docs, problems } = M.loadEntries(entries);
        const box = $('#problems');
        if (!docs.length) problems.unshift(`No layers/turn-N.json found in ${source} (${entries.length} files read).`);
        box.hidden = !problems.length;
        put(box, ...problems.slice(0, 20).map((p) => h('div', null, p)), problems.length > 20 ? h('div', null, `… and ${problems.length - 20} more`) : null);
        if (!docs.length) return;
        state.docs = docs;
        if (source !== 'hash') history.replaceState(null, '', location.pathname + location.search + ((source === 'demo' || source === 'bundle') && location.hash ? location.hash : ''));
        readHash();
        render();
    }

    const readFile = (file, path) => file.text().then((text) => ({ path, text }));
    const wanted = (path) => /(turn-\d+\.json|canvas\.json|\.r\d+)$/.test(path);

    $('#pick-dir').addEventListener('change', async (e) => {
        const files = [...e.target.files].filter((f) => wanted(f.webkitRelativePath || f.name));
        load(await Promise.all(files.map((f) => readFile(f, f.webkitRelativePath || f.name))), 'the folder');
    });
    $('#pick-files').addEventListener('change', async (e) => {
        load(await Promise.all([...e.target.files].map((f) => readFile(f, guessPath(f.name)))), 'the files');
    });
    // Loose files lose their folders: put them back where the format expects them.
    const guessPath = (name) => (/^turn-\d+\.json$/.test(name) ? `layers/${name}` : /\.r\d+$/.test(name) ? `canvas/${name}` : name);
    $('#load-demo').addEventListener('click', () => loadDemo(false));

    function loadDemo(keepHash) {
        if (!window.SUB0CANVAS_DEMO) { $('#problems').hidden = false; $('#problems').textContent = 'fixtures/demo.js is missing: run npm run fixtures:demo.'; return; }
        load(window.SUB0CANVAS_DEMO, keepHash ? 'demo' : 'demo-fresh');
    }

    // Drag and drop of folders (via webkitGetAsEntry) or files.
    let dragDepth = 0;
    document.addEventListener('dragenter', (e) => { e.preventDefault(); ++dragDepth; $('#drop').hidden = false; });
    document.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; $('#drop').hidden = true; } });
    document.addEventListener('dragover', (e) => e.preventDefault());
    document.addEventListener('drop', async (e) => {
        e.preventDefault();
        dragDepth = 0;
        $('#drop').hidden = true;
        const entries = [];
        const walk = async (entry, prefix) => {
            if (entry.isFile) {
                const path = prefix + entry.name;
                if (wanted(path)) entries.push(await new Promise((res, rej) => entry.file((f) => readFile(f, path).then(res, rej), rej)));
            } else if (entry.isDirectory) {
                const reader = entry.createReader();
                for (;;) {
                    const batch = await new Promise((res, rej) => reader.readEntries(res, rej));
                    if (!batch.length) break;
                    for (const child of batch) await walk(child, `${prefix}${entry.name}/`);
                }
            }
        };
        const items = [...(e.dataTransfer.items || [])].map((i) => i.webkitGetAsEntry?.()).filter(Boolean);
        if (items.length) for (const it of items) await walk(it, '');
        else for (const f of e.dataTransfer.files) entries.push(await readFile(f, guessPath(f.name)));
        const loose = entries.every((x) => !x.path.includes('/'));
        load(loose ? entries.map((x) => ({ ...x, path: guessPath(x.path) })) : entries, 'the drop');
    });

    // ---- helpers over the current turn ------------------------------------------------------------------------------------------------------------

    const turn = () => M.turnOf(state.doc, state.turn);
    const layerItems = (layer) => turn().layers[layer] || [];

    function findItem(key) {
        if (!key) return null;
        const layer = key.slice(0, key.search(/[#~]/));
        if (key.startsWith(`${layer}#unified:`) || state.unified === key) {
            const group = unifiedGroup(layer);
            if (group) return { layer, item: M.unify(layer, group), virtual: true };
        }
        const item = layerItems(layer).find((it) => M.itemKey(layer, it) === key);
        return item ? { layer, item } : null;
    }

    function unifiedGroup(layer) {
        if (!state.unified) return null;
        const groups = M.duplicateGroups(state.doc, state.turn, layer);
        return groups.find((g) => unifiedKey(layer, g) === state.unified) || null;
    }
    const unifiedKey = (layer, group) => `${layer}#unified:${group.map((it) => it.id || it.number).join('+')}`;

    const blockLabel = (b) => (b ? `${b.name}` : '');
    const linesLabel = (r) => (r.first === r.last ? `line ${r.first}` : `lines ${r.first}-${r.last}`);

    function statusNote(r, anchor) {
        if (r.status === 'merged') return h('span', { class: 'note merged' }, `${anchor.blockId} merged into ${r.block.id}`);
        if (r.status === 'orphan') return h('span', { class: 'note orphan' }, `orphan: ${r.orphanId} retired without a successor`);
        if (r.status === 'unknown') return h('span', { class: 'note orphan' }, `unknown block ${anchor.blockId}`);
        if (r.status === 'line') return h('span', { class: 'note line' }, r.within ? `outside the block index (line in ${r.within.name}?)` : 'outside every block');
        return null;
    }

    // ---- render -----------------------------------------------------------------------------------------------------------------------------------

    function render() {
        const doc = state.doc;
        $('#app').classList.toggle('empty', !doc);
        if (!doc) {
            put($('#nav'), );
            put($('#inspector'), );
            put($('#turns'), );
            put($('#centre'), h('div', { class: 'welcome' },
                h('h1', null, 'One document, many layers'),
                h('p', null, 'Open a Sub0Squad attempt directory (it holds layers/turn-N.json, canvas.json and canvas/), drop one here, or load the demo.'),
                h('button', { class: 'btn primary', onclick: () => loadDemo(false) }, 'Load the demo')));
            return;
        }
        if (state.centre !== 'code' && !M.layerNames(doc).includes(state.centre)) state.centre = 'code';
        renderDocSelect();
        renderTurns();
        renderNav();
        if (state.centre === 'code') renderCode();
        else renderLayer(state.centre);
        renderInspector();
        writeHash();
        const focus = document.querySelector('.focus-anchor');
        if (focus) focus.scrollIntoView({ block: 'nearest' });
    }

    function renderDocSelect() {
        const sel = $('#doc-select');
        sel.hidden = state.docs.length < 2;
        put(sel, ...state.docs.map((d, i) => h('option', { value: i, selected: i === state.docIdx }, d.name)));
        sel.onchange = () => set({ docIdx: Number(sel.value), turn: 0, block: '', item: '' });
    }

    function renderTurns() {
        const turns = state.doc.turnNumbers;
        const i = turns.indexOf(state.turn);
        put($('#turns'), 
            h('button', { class: 'btn icon', disabled: i <= 0, title: 'Previous turn', onclick: () => set({ turn: turns[i - 1] }) }, '◀'),
            ...turns.map((t) => {
                const tt = M.turnOf(state.doc, t);
                const changes = tt.events.filter((e) => e.carry !== 'kept').length;
                return h('button', { class: `turn ${t === state.turn ? 'on' : ''}`, title: `${changes} identity changes`, onclick: () => set({ turn: t }) },
                    `turn ${t}`);
            }),
            h('button', { class: 'btn icon', disabled: i >= turns.length - 1, title: 'Next turn', onclick: () => set({ turn: turns[i + 1] }) }, '▶'));
    }

    // Left: centre on any layer; the block index for history queries.
    function renderNav() {
        const doc = state.doc;
        const t = turn();
        const entries = [['code', `${t.blocks.length} blocks`]].concat(M.layerNames(doc).map((l) => [l, `${(t.layers[l] || []).length} items`]));
        const search = h('input', { type: 'search', placeholder: 'block id or name…', value: state.query || '', 'aria-label': 'Find a block' });
        const list = h('div', { class: 'blocklist' });
        const fill = () => {
            state.query = search.value;
            put(list, ...M.findBlocks(doc, search.value).map((b) => {
                const r = M.resolveId(doc, state.turn, b.id);
                return h('button', { class: `blockrow ${b.id === state.block ? 'on' : ''} st-${r.status}`, onclick: () => set({ block: b.id }) },
                    h('span', { class: 'bid' }, b.id), h('span', { class: 'bname' }, b.names[b.names.length - 1] || ''),
                    r.status === 'live' ? null : h('span', { class: `tag st-${r.status}` }, r.status === 'merged' ? `→ ${r.block.id}` : r.status === 'unknown' ? 'absent' : r.status));
            }));
        };
        search.addEventListener('input', fill);
        fill();
        put($('#nav'), 
            h('h2', null, 'Centre on'),
            h('div', { class: 'layers' }, entries.map(([l, sub]) =>
                h('button', { class: `layerbtn ${state.centre === l ? 'on' : ''}`, onclick: () => set({ centre: l, item: state.centre === l ? state.item : '' }) },
                    h('span', { class: `swatch ${layerClass(l)}` }), h('span', null, l), h('span', { class: 'muted' }, sub)))),
            h('h2', null, 'Blocks'),
            search, list);
    }

    // ---- centre: code -------------------------------------------------------------------------------------------------------------------------------

    function renderCode() {
        const doc = state.doc;
        const t = turn();
        const paths = Object.keys(t.files).sort();
        const sel = M.resolveId(doc, state.turn, state.block || '_');
        if (sel.block && !state.file) state.file = sel.block.path;
        if (!paths.includes(state.file)) state.file = sel.block ? sel.block.path : paths[0];
        const path = state.file;
        const lines = M.fileLines(doc, state.turn, path);
        const blocks = M.blocksOf(doc, state.turn, path);
        const counts = M.attachmentCounts(doc, state.turn);
        // Lines flagged by the open item's anchors (so the code view shows where a layer item lands too).
        const open = findItem(state.item);
        const marks = new Map();
        if (open) for (const a of M.fanOut(doc, state.turn, open.item).anchors) if (a.resolved.path === path) marks.set(a.resolved.line, open.layer);
        const lineItems = M.lineAnchored(doc, state.turn).filter((hit) => hit.anchor.path === path);
        for (const hit of lineItems) if (!marks.has(hit.anchor.line)) marks.set(hit.anchor.line, hit.layer);

        const starts = new Map(blocks.map((b) => [b.first, b]));
        const rows = [];
        lines.forEach((text, i) => {
            const n = i + 1;
            const holder = M.blockAt(doc, state.turn, path, n);
            const b = starts.get(n);
            if (b) {
                const c = counts.get(b.id) || {};
                rows.push(h('div', { class: `blockhead ${b.id === sel.block?.id ? 'on' : ''}`, onclick: () => set({ block: b.id }) },
                    h('span', { class: 'bid' }, b.id), h('span', { class: 'bname' }, b.name),
                    h('span', { class: 'muted' }, `${b.first}-${b.last}`),
                    h('span', { class: 'badges' }, Object.entries(c).map(([l, k]) => chip(l, `${l} ${k}`, { title: `${k} ${l} anchors` })))));
            }
            const inSel = holder && sel.block && holder.id === sel.block.id;
            const mark = marks.get(n);
            rows.push(h('div', { class: `line ${holder ? 'inblock' : ''} ${inSel ? 'sel' : ''} ${mark ? 'marked ' + layerClass(mark) : ''}`, 'data-line': n,
                                 onclick: holder ? () => set({ block: holder.id }) : undefined },
                h('span', { class: 'ln' }, n), h('span', { class: 'gut' }, mark ? '●' : ''), h('code', null, text || ' ')));
        });
        put($('#centre'), 
            h('div', { class: 'tabs' }, paths.map((p) => h('button', { class: `tab ${p === path ? 'on' : ''}`, onclick: () => set({ file: p }) },
                p, h('span', { class: 'muted' }, ` r${t.files[p].revision}`)))),
            h('div', { class: 'code' }, rows));
    }

    // ---- centre: a layer --------------------------------------------------------------------------------------------------------------------------

    function renderLayer(layer) {
        const items = layerItems(layer);
        const doc = state.doc;
        const groups = M.duplicateGroups(doc, state.turn, layer);
        const banner = groups.length ? h('div', { class: 'banner' },
            h('strong', null, 'Possible duplicates. '),
            groups.map((g) => {
                const key = unifiedKey(layer, g);
                return h('span', { class: 'dup' }, g.map((it) => `#${it.number}`).join(' + '), ' ',
                    h('button', { class: 'btn small', onclick: () => set({ unified: state.unified === key ? '' : key, item: state.unified === key ? '' : key, a: 0 }) },
                        state.unified === key ? 'Hide unified report' : 'Preview unified report'));
            })) : null;
        const cards = [];
        const ug = unifiedGroup(layer);
        if (ug) cards.push(card(layer, M.unify(layer, ug), state.unified));
        for (const it of items) cards.push(card(layer, it, M.itemKey(layer, it)));
        put($('#centre'), 
            h('div', { class: 'layerhead' }, chip(layer), h('span', null, `${items.length} items at turn ${state.turn}`)),
            banner,
            items.length ? h('div', { class: 'cards' }, cards) : h('p', { class: 'muted pad' }, `No ${layer} items at this turn.`));
    }

    function card(layer, item, key) {
        const doc = state.doc;
        const fan = M.fanOut(doc, state.turn, item);
        const open = state.item === key;
        const unified = key === state.unified;
        const orphanCount = fan.anchors.filter((a) => a.resolved.status === 'orphan' || a.resolved.status === 'unknown').length;
        const mergedCount = fan.anchors.filter((a) => a.resolved.status === 'merged').length;
        const summary = fan.anchors.length === 1
            ? anchorTitle(fan.anchors[0])
            : `${fan.anchors.length} anchors · ${fan.blockCount} blocks · ${fan.fileCount} file${fan.fileCount === 1 ? '' : 's'}`;
        const el = h('article', { class: `card ${open ? 'open' : ''} ${unified ? 'unified' : ''}`, id: `card-${cssId(key)}` },
            h('header', null,
                h('span', { class: 'num' }, unified ? 'U' : `#${item.number}`),
                item.kind ? h('span', { class: `kind k-${item.kind}` }, item.kind) : null,
                h('span', { class: 'title' }, item.title),
                fan.anchors.length > 1 ? h('span', { class: 'fan', title: 'one item, many anchors' }, `1→${fan.anchors.length}`) : null),
            h('div', { class: 'meta' },
                h('span', null, summary),
                orphanCount ? h('span', { class: 'note orphan' }, `${orphanCount} orphan`) : null,
                mergedCount ? h('span', { class: 'note merged' }, `${mergedCount} via merge`) : null,
                item.fix ? h('span', { class: 'note fix' }, `fix: ${item.fix.summary || item.fix.kind}`) : null),
            item.detail ? h('pre', { class: 'detail' }, item.detail) : null,
            h('div', { class: 'actions' },
                h('button', { class: `btn small ${open ? 'on' : ''}`, onclick: () => set({ item: open ? '' : key, a: 0, fix: '', trace: '' }) },
                    open ? 'Hide code' : fan.anchors.length > 1 ? `Show code (${fan.anchors.length})` : 'Show code'),
                item.fix ? h('button', { class: `btn small ${state.fix === key ? 'on' : ''}`, onclick: () => set({ item: key, fix: state.fix === key ? '' : key }) },
                    'Preview fix at every anchor') : null,
                unified ? null : h('button', { class: `btn small ${state.trace === key ? 'on' : ''}`, onclick: () => set({ item: key, trace: state.trace === key ? '' : key }) },
                    'Trace across turns')),
            open ? callout(layer, item, fan) : null,
            open && state.fix === key ? fixPreview(item) : null,
            open && state.trace === key ? tracePanel(layer, item) : null);
        if (open) el.classList.add('focus-anchor');
        return el;
    }

    const cssId = (s) => s.replace(/[^a-zA-Z0-9_-]/g, '_');

    function anchorTitle(a) {
        const r = a.resolved;
        const where = r.block ? `${r.block.name} (${r.block.id}, ${linesLabel(r)})` : `${r.path}:${r.line}`;
        return `${where}${a.anchor.confidence && a.anchor.confidence !== 'exact' ? ` · ${a.anchor.confidence}` : ''}`;
    }

    // The callout next to an item: the code at the current anchor, a stepper through the fan-out, and the anchors grouped by file.
    function callout(layer, item, fan) {
        const doc = state.doc;
        if (!fan.anchors.length) return h('div', { class: 'callout' }, h('p', { class: 'muted' }, 'This item has no anchors.'));
        const k = Math.min(Math.max(0, state.a), fan.anchors.length - 1);
        const cur = fan.anchors[k];
        const r = cur.resolved;
        const step = (d) => set({ a: (k + d + fan.anchors.length) % fan.anchors.length });
        const lines = M.fileLines(doc, state.turn, r.path);
        let snippet;
        if (r.status === 'orphan' || r.status === 'unknown') {
            snippet = h('div', { class: 'snippet orphan' }, h('p', null, `Block ${r.orphanId} has no live successor at turn ${state.turn}. `,
                h('button', { class: 'btn small', onclick: () => set({ block: r.orphanId }) }, `History of ${r.orphanId}`)));
        } else {
            const from = r.block ? r.first : Math.max(1, r.line - 3), to = r.block ? r.last : Math.min(lines.length, r.line + 3);
            const hot = r.status === 'live' || r.status === 'line' ? cur.anchor.line : -1;
            const rows = [];
            for (let n = from; n <= to; ++n) {
                let text = lines[n - 1] ?? '';
                let code = h('code', null, text || ' ');
                if (n === hot && cur.anchor.column) {
                    const c = cur.anchor.column - 1;
                    const m = text.slice(c).match(/^[\w.:]+(\(\))?|^./);
                    const len = m ? m[0].length : 1;
                    code = h('code', null, text.slice(0, c), h('mark', null, text.slice(c, c + len)), text.slice(c + len));
                }
                rows.push(h('div', { class: `line ${n === hot ? 'hot' : ''}` }, h('span', { class: 'ln' }, n), code));
            }
            snippet = h('div', { class: 'snippet' }, rows);
        }
        return h('div', { class: 'callout' },
            h('div', { class: 'callout-head' },
                fan.anchors.length > 1 ? h('span', { class: 'stepper' },
                    h('button', { class: 'btn icon', title: 'Previous anchor', onclick: () => step(-1) }, '◀'),
                    h('span', null, `anchor ${k + 1} of ${fan.anchors.length}`),
                    h('button', { class: 'btn icon', title: 'Next anchor', onclick: () => step(1) }, '▶')) : null,
                h('span', { class: 'where' }, r.path, cur.anchor.column ? `:${r.line}:${cur.anchor.column}` : `:${r.line}`),
                r.block ? h('button', { class: 'btn small', onclick: () => set({ block: r.block.id }) }, `${r.block.id} ${blockLabel(r.block)}`) : null,
                cur.anchor.confidence !== 'exact' ? h('span', { class: `note conf-${cur.anchor.confidence}` }, cur.anchor.confidence) : null,
                statusNote(r, cur.anchor),
                r.block ? h('button', { class: 'btn small', onclick: () => set({ centre: 'code', file: r.path, block: r.block.id }) }, 'Open in code') : null),
            snippet,
            fan.anchors.length > 1 ? h('div', { class: 'fanlist' }, fan.files.map((f) => h('div', { class: 'fanfile' },
                h('div', { class: 'fanpath' }, f.path, h('span', { class: 'muted' }, ` ${f.anchors.length}`)),
                f.anchors.map((a) => {
                    const i = fan.anchors.indexOf(a);
                    return h('button', { class: `fanrow ${i === k ? 'on' : ''}`, onclick: () => set({ a: i }) },
                        h('span', { class: 'ln' }, `${a.resolved.line}${a.anchor.column ? ':' + a.anchor.column : ''}`),
                        h('span', null, a.resolved.block ? a.resolved.block.name : '—'),
                        a.resolved.status !== 'live' ? h('span', { class: `tag st-${a.resolved.status}` }, a.resolved.status) : null,
                        a.anchor.confidence !== 'exact' ? h('span', { class: 'tag' }, a.anchor.confidence) : null);
                })))) : null);
    }

    function fixPreview(item) {
        const changes = M.previewFix(state.doc, state.turn, item);
        const applies = changes.filter((c) => c.applies).length;
        return h('div', { class: 'fixpreview' },
            h('div', { class: 'fixhead' }, h('strong', null, item.fix.summary || 'Proposed fix'),
                h('code', null, ` s/${item.fix.pattern}/${item.fix.replacement}/${item.fix.flags ?? 'g'}`),
                h('span', { class: 'muted' }, ` · ${applies} of ${changes.length} lines change (preview only; the producer applies fixes)`)),
            changes.map((c) => h('div', { class: `fixrow ${c.applies ? '' : 'noop'}` },
                h('div', { class: 'fixwhere' }, `${c.path}:${c.line}`, c.reason ? h('span', { class: 'muted' }, ` ${c.reason}`) : null),
                h('div', { class: 'del' }, h('code', null, '- ' + c.before)),
                c.applies ? h('div', { class: 'add' }, h('code', null, '+ ' + c.after)) : null)));
    }

    function tracePanel(layer, item) {
        const trace = M.itemTrace(state.doc, layer, item);
        return h('div', { class: 'trace' },
            h('div', { class: 'muted' }, `Matched across turns by ${item.id ? `id "${item.id}"` : 'kind and title (no id)'}`),
            h('div', { class: 'tracerow' }, trace.map((t) => h('button', { class: `tracecell ${t.item ? 'has' : 'gone'} ${t.turn === state.turn ? 'on' : ''}`,
                                                              onclick: () => set({ turn: t.turn }) },
                h('div', null, `turn ${t.turn}`),
                h('div', { class: 'muted' }, t.item ? `#${t.item.number} · ${t.fan.anchors.length} anchor${t.fan.anchors.length === 1 ? '' : 's'}` : 'absent'),
                t.fan ? h('div', { class: 'muted' }, t.fan.blocks.join(' ') || '—') : null))));
    }

    // ---- inspector: a block's attachments and history ---------------------------------------------------------------------------------------------

    function renderInspector() {
        const doc = state.doc;
        if (!state.block) {
            const orph = M.orphans(doc, state.turn);
            put($('#inspector'), h('h2', null, 'Inspector'),
                h('p', { class: 'muted' }, 'Select a block (in the code, the block list, or a callout) to see every item anchored to it and its history.'),
                orph.length ? h('div', null, h('h3', null, `Orphans at turn ${state.turn}`), orph.map((hit) => itemLink(hit))) : null);
            return;
        }
        const id = state.block;
        const r = M.resolveId(doc, state.turn, id);
        const hist = M.blockHistory(doc, id);
        const target = r.block ? r.block.id : id;
        const hits = r.block ? M.attachedTo(doc, state.turn, target) : M.orphans(doc, state.turn).filter((hit) => hit.resolved.orphanId === id);
        const byLayer = new Map();
        for (const hit of hits) {
            if (!byLayer.has(hit.layer)) byLayer.set(hit.layer, new Map());
            const m = byLayer.get(hit.layer);
            if (!m.has(hit.item)) m.set(hit.item, []);
            m.get(hit.item).push(hit);
        }
        put($('#inspector'), 
            h('div', { class: 'insphead' },
                h('div', null, h('span', { class: 'bid big' }, id), ' ', h('strong', null, r.block ? r.block.name : hist.names[hist.names.length - 1] || '')),
                r.block ? h('div', { class: 'muted' }, `${r.block.path} · ${linesLabel(r.block)} · `, h('code', null, r.block.signature || '')) : null,
                r.status === 'merged' ? h('div', { class: 'note merged' }, `merged into ${r.block.id} (${r.via.join(' → ')} → ${r.block.id})`) : null,
                r.status === 'orphan' ? h('div', { class: 'note orphan' }, `retired at turn ${state.turn} without a successor: its items are orphans`) : null,
                r.status === 'unknown' ? h('div', { class: 'note' }, `not present at turn ${state.turn}`) : null,
                h('button', { class: 'btn small close', title: 'Clear selection', onclick: () => set({ block: '' }) }, '×')),
            h('h3', null, `Anchored here at turn ${state.turn}`),
            hits.length ? [...byLayer].map(([layer, items]) => h('div', { class: 'attached' },
                h('div', { class: 'attached-layer' }, chip(layer), h('span', { class: 'muted' }, ` ${items.size} item${items.size === 1 ? '' : 's'}`)),
                [...items].map(([item, list]) => itemLink(list[0], list.length)))) : h('p', { class: 'muted' }, 'Nothing in any layer.'),
            h('h3', null, 'History'),
            historyView(hist));
    }

    function itemLink(hit, n = 1) {
        const key = M.itemKey(hit.layer, hit.item);
        const extra = hit.item.anchors.length > 1 ? ` (1→${hit.item.anchors.length})` : '';
        return h('button', { class: 'itemlink', onclick: () => set({ centre: hit.layer, item: key, a: hit.item.anchors.indexOf(hit.anchor) >= 0 ? fanIndex(hit) : 0, fix: '', trace: '' }) },
            h('span', { class: 'num' }, `#${hit.item.number}`), h('span', null, hit.item.title),
            h('span', { class: 'muted' }, `${n > 1 ? ` ×${n}` : ''}${extra}`),
            hit.resolved.status === 'merged' ? h('span', { class: 'tag st-merged' }, `via ${hit.anchor.blockId}`) : null);
    }

    // Index of a hit's anchor within the item's fan-out order (files, then lines), so the callout opens on it.
    function fanIndex(hit) {
        const fan = M.fanOut(state.doc, state.turn, hit.item);
        return Math.max(0, fan.anchors.findIndex((a) => a.anchor === hit.anchor));
    }

    const CARRY_TEXT = {
        added: () => 'added',
        kept: () => 'kept (same signature)',
        resigned: (e) => `signature changed${e.detail ? ` from ${e.detail}` : ''}`,
        renamed: (e) => `renamed from ${e.detail}`,
        revived: () => 'revived (written again)',
        retired: () => 'retired: items orphaned',
        merged: (e, id) => (e.id === id ? `merged into ${e.detail}` : `absorbed ${e.id}`),
    };

    function historyView(hist) {
        const layersEver = new Set(hist.timeline.flatMap((t) => Object.keys(t.layers)));
        return h('div', { class: 'history' },
            h('div', { class: 'histsum' },
                hist.names.length > 1 ? h('div', null, 'Names: ', hist.names.join(' → ')) : null,
                hist.paths.length > 1 ? h('div', null, 'Files: ', hist.paths.join(', ')) : null,
                hist.mergedFrom.length ? h('div', null, 'Absorbed: ', hist.mergedFrom.map((m) => h('button', { class: 'linkish', onclick: () => set({ block: m }) }, m))) : null,
                h('div', null, 'Layers ever attached: ', layersEver.size ? [...layersEver].map((l) => chip(l)) : h('span', { class: 'muted' }, 'none'))),
            hist.timeline.map((t) => h('div', { class: `histrow st-${t.status} ${t.turn === state.turn ? 'on' : ''}`, onclick: () => set({ turn: t.turn }) },
                h('div', { class: 'histturn' }, `turn ${t.turn}`, h('span', { class: `tag st-${t.status}` }, t.status === 'merged' ? `→ ${t.mergedInto}` : t.status)),
                h('div', { class: 'histbody' },
                    t.events.map((e) => h('div', { class: `ev c-${e.carry}` }, (CARRY_TEXT[e.carry] || ((x) => x.carry))(e, hist.id))),
                    t.status === 'live' && !t.events.some((e) => e.id === hist.id) ? h('div', { class: 'ev muted' }, t.changed ? 'text changed' : 'unchanged') : null,
                    t.status === 'live' && t.changed && t.events.some((e) => e.id === hist.id && !['added', 'revived'].includes(e.carry))
                        ? h('div', { class: 'ev muted' }, 'text changed') : null,
                    t.revisions.length ? h('div', { class: 'revs' }, t.revisions.map((rv) => h('span', { class: 'rev', title: `lines ${rv.from}-${rv.to} of r${rv.base}, Δ${rv.delta}` },
                        `r${rv.revision} ${rv.by}`))) : null,
                    Object.keys(t.layers).length ? h('div', { class: 'histlayers' }, Object.entries(t.layers).map(([l, items]) => chip(l, `${l} ${items.length}`))) : null))));
    }

    // ---- keyboard: [ and ] step anchors, , and . step turns --------------------------------------------------------------------------------------

    document.addEventListener('keydown', (e) => {
        if (!state.doc || e.target.tagName === 'INPUT') return;
        const turns = state.doc.turnNumbers, i = turns.indexOf(state.turn);
        if (e.key === ',' && i > 0) set({ turn: turns[i - 1] });
        else if (e.key === '.' && i < turns.length - 1) set({ turn: turns[i + 1] });
        else if ((e.key === '[' || e.key === ']') && state.item) {
            const open = findItem(state.item);
            if (!open) return;
            const n = open.item.anchors.length;
            set({ a: (state.a + (e.key === ']' ? 1 : -1) + n) % n });
        }
    });

    window.addEventListener('hashchange', () => {
        if (state.docs.length) { readHash(); render(); }
        else if (location.hash.length > 1 && window.SUB0CANVAS_DEMO) loadDemo(true);
    });

    // Start: ?bundle=<url> loads a JSON array of {path, text} (what `npm run mirror -- canvas` writes beside its folder); it needs the page served
    // (npm run serve), because a page opened from file:// cannot fetch. Otherwise a hash naming a view opens the demo there (links and screenshots),
    // and with neither the welcome page shows.
    const bundle = new URLSearchParams(location.search).get('bundle');
    if (bundle) {
        fetch(bundle)
            .then((r) => { if (!r.ok) throw new Error(`${r.status} ${r.statusText}`); return r.json(); })
            .then((entries) => load(entries, 'bundle'))
            .catch((e) => { render(); $('#problems').hidden = false; $('#problems').textContent = `Could not load ${bundle}: ${e.message}. A bundle needs the page served (npm run serve).`; });
    } else if (location.hash.length > 1 && window.SUB0CANVAS_DEMO) loadDemo(true);
    else render();

    window.S0View = { load, state, render };
})();
