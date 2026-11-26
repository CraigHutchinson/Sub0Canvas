// Sub0Canvas model: the layered document as data, and the queries the viewer asks of it. Pure (no DOM, no I/O), so Node tests load it directly.
//
// Input is a set of files from one or more attempt directories, as Sub0Squad's canvas writes them:
//   <attempt>/layers/turn-N.json   one `layered-doc-1` snapshot per turn (files, live blocks, retired ids, id events, layers)
//   <attempt>/canvas.json          the revision log {files:{path:rev}, log:[{path,by,revision,base,from,to,delta}]}
//   <attempt>/canvas/<flat>.r<N>   every revision's text ('/' in the path flattened to '_')
// Layers are freeform: whatever keys `layers` holds. "code" is not a layer of the file; it is the files and blocks themselves.
//
// Both shapes of the format are read (docs/BACKBONE.md section 7, additive): a file's `revision` number and/or its opaque string `revisionId`; its inline `text`
// and/or a `textId` naming a text in some turn's `texts` table (each text sent once per attempt); `events` cumulative or, with `eventsFrom`, only the events
// from that position of the log on (a producer that writes `eventsFrom` and a cumulative list is read the same); `policies` and `layerMeta` beside the layers.
//
// Works as a classic browser script (window.S0Model) and as a CommonJS module (require('./model.js')).

(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.S0Model = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    const SCHEMA = 'layered-doc-1';
    const CARRIES = ['kept', 'resigned', 'renamed', 'revived', 'added', 'retired', 'merged'];
    const CONFIDENCES = ['exact', 'suspect', 'none'];

    // ---- validation -------------------------------------------------------------------------------------------------------------------------------

    /** The problems with one parsed turn file, as readable strings; [] when it is a usable `layered-doc-1` snapshot. Unknown fields are allowed. */
    function validateTurn(t) {
        const errors = [];
        const need = (cond, msg) => { if (!cond) errors.push(msg); };
        if (!t || typeof t !== 'object') return ['not an object'];
        need(t.schema === SCHEMA, `schema is ${JSON.stringify(t.schema)}, expected "${SCHEMA}"`);
        need(Number.isInteger(t.turn), 'turn is not an integer');
        need(t.files && typeof t.files === 'object', 'files is missing');
        for (const [p, f] of Object.entries(t.files || {})) {
            need(f && (typeof f.text === 'string' || typeof f.textId === 'string'), `files["${p}"] needs a text or a textId`);
            need(f && (Number.isInteger(f.revision) || typeof f.revisionId === 'string'), `files["${p}"] needs a revision number or a revisionId`);
        }
        need(t.texts === undefined || (t.texts && typeof t.texts === 'object' && !Array.isArray(t.texts)), 'texts is not an object');
        need(t.eventsFrom === undefined || (Number.isInteger(t.eventsFrom) && t.eventsFrom >= 0), 'eventsFrom is not a non-negative integer');
        need(t.policies === undefined || (t.policies && typeof t.policies === 'object' && !Array.isArray(t.policies)), 'policies is not an object');
        need(t.layerMeta === undefined || (t.layerMeta && typeof t.layerMeta === 'object' && !Array.isArray(t.layerMeta)), 'layerMeta is not an object');
        need(Array.isArray(t.blocks), 'blocks is not an array');
        const ids = new Set();
        for (const b of t.blocks || []) {
            need(typeof b.id === 'string' && b.id !== '', 'a block has no id');
            need(!ids.has(b.id), `block id ${b.id} is live twice`);
            ids.add(b.id);
            need(Number.isInteger(b.first) && Number.isInteger(b.last) && b.first <= b.last, `block ${b.id} has bad lines ${b.first}-${b.last}`);
            need(!t.files || t.files[b.path] !== undefined, `block ${b.id} names unknown file ${b.path}`);
        }
        for (const r of t.retired || []) {
            need(!ids.has(r.id), `block ${r.id} is both live and retired`);
            need(r.mergedInto === undefined || typeof r.mergedInto === 'string', `retired ${r.id} has a non-string mergedInto`);
        }
        for (const e of t.events || []) need(CARRIES.includes(e.carry), `event for ${e.id} has unknown carry "${e.carry}"`);
        need(t.layers === undefined || (t.layers && typeof t.layers === 'object' && !Array.isArray(t.layers)), 'layers is not an object');
        for (const [name, items] of Object.entries(t.layers || {})) {
            if (!Array.isArray(items)) { errors.push(`layer "${name}" is not an array`); continue; }
            items.forEach((it, i) => {
                need(Array.isArray(it.anchors), `${name}[${i}] has no anchors array`);
                for (const a of it.anchors || [])
                    need(a.confidence === undefined || CONFIDENCES.includes(a.confidence), `${name}[${i}] anchor has unknown confidence "${a.confidence}"`);
            });
        }
        return errors;
    }

    // ---- loading ----------------------------------------------------------------------------------------------------------------------------------

    /**
     * Group a flat list of {path, text} entries (from a folder picker, a drop, or the bundled demo) into documents, one per attempt directory.
     * Returns {docs: [Doc], problems: [string]}. Files that are not part of the format are ignored.
     */
    function loadEntries(entries) {
        const groups = new Map();
        const problems = [];
        const group = (key) => {
            if (!groups.has(key)) groups.set(key, { key, turns: [], canvas: null, revisionFiles: [] });
            return groups.get(key);
        };
        for (const { path, text } of entries) {
            const p = path.replace(/\\/g, '/');
            let m;
            if ((m = p.match(/^(.*?)(?:^|\/)?layers\/turn-(\d+)\.json$/)) || (m = p.match(/^(.*?)(?:^|\/)?turn-(\d+)\.json$/))) {
                let t;
                try { t = JSON.parse(text); } catch (e) { problems.push(`${p}: ${e.message}`); continue; }
                const errs = validateTurn(t);
                if (errs.length) { problems.push(...errs.map((e) => `${p}: ${e}`)); if (t?.schema !== SCHEMA) continue; }
                group(trimSlash(m[1])).turns.push(normaliseTurn(t));
            } else if ((m = p.match(/^(.*?)\/?canvas\.json$/))) {
                try { group(trimSlash(m[1])).canvas = JSON.parse(text); } catch (e) { problems.push(`${p}: ${e.message}`); }
            } else if ((m = p.match(/^(.*?)\/?canvas\/([^/]+)\.r(\d+)$/))) {
                group(trimSlash(m[1])).revisionFiles.push({ flat: m[2], revision: Number(m[3]), text });
            }
        }
        const docs = [...groups.values()].filter((g) => g.turns.length).map((g) => buildDoc(g, problems));
        docs.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
        return { docs, problems };
    }

    const trimSlash = (s) => s.replace(/\/+$/, '');

    function normaliseTurn(t) {
        const layers = {};
        for (const [name, items] of Object.entries(t.layers || {}))
            layers[name] = (items || []).map((it, i) => ({
                ...it,
                number: Number.isInteger(it.number) ? it.number : i + 1,
                anchors: (it.anchors || []).map((a) => ({ blockId: a.blockId || '', path: a.path || '', line: a.line || 0, confidence: a.confidence || 'none', ...a })),
            }));
        return { ...t, files: t.files || {}, blocks: t.blocks || [], retired: t.retired || [], events: t.events || [], layers };
    }

    /** A document: the turns of one attempt in order, with indexes built once so every query below is a lookup. */
    function buildDoc(g, problems = []) {
        const turns = g.turns.sort((a, b) => a.turn - b.turn);
        resolveTexts(turns, problems, g.key);
        numberRevisions(turns);
        perTurnEvents(turns);
        const paths = new Set(turns.flatMap((t) => Object.keys(t.files)));
        if (g.canvas?.files) for (const p of Object.keys(g.canvas.files)) paths.add(p);
        // Revision texts: map each flattened name back to a path we know ('/' -> '_' is lossy, so match against the known paths).
        const revisions = {};
        const unflatten = new Map([...paths].flatMap((p) => [[p.replace(/\//g, '_'), p], [p.replace(/\//g, '%2F'), p]]));   // the old '_' and the new '%2F' names
        for (const r of g.revisionFiles) {
            const path = unflatten.get(r.flat) ?? r.flat;
            (revisions[path] ??= {})[r.revision] = r.text;
        }
        for (const t of turns) for (const [p, f] of Object.entries(t.files)) (revisions[p] ??= {})[f.revision] ??= f.text;
        const doc = {
            name: g.key || '(attempt)',
            turns,
            turnNumbers: turns.map((t) => t.turn),
            log: g.canvas?.log || [],
            layerMeta: Object.assign({}, ...turns.map((t) => t.layerMeta || {})),
            policies: turns.length ? turns[turns.length - 1].policies || {} : {},
            events: turns.flatMap((t) => t.events),   // the whole log, whichever shape the turns carried it in
            revisions,
            paths: [...paths].sort(),
        };
        doc.index = buildIndex(doc);
        return doc;
    }

    /**
     * Texts by content id: every file's text, from its inline `text` or the `texts` table of any turn of the attempt (a turn carries only the texts the earlier
     * ones did not). A file whose id no turn carries is reported and shown empty, never fatal.
     */
    function resolveTexts(turns, problems, key) {
        const table = new Map();
        for (const t of turns) {
            for (const [id, text] of Object.entries(t.texts || {})) table.set(id, text);
            for (const f of Object.values(t.files)) if (typeof f.text === 'string' && typeof f.textId === 'string') table.set(f.textId, f.text);
        }
        for (const t of turns) {
            const files = {};
            for (const [p, f] of Object.entries(t.files)) {
                let text = f.text;
                if (typeof text !== 'string') {
                    text = table.get(f.textId);
                    if (text === undefined) { problems.push(`${key ? key + '/' : ''}turn ${t.turn}: no turn carries the text ${f.textId} of ${p}`); text = ''; }
                }
                files[p] = { ...f, text };
            }
            t.files = files;
        }
    }

    /** A producer that gives only opaque revision ids has no numbers: each path's distinct ids, in turn order, are its revisions 1, 2, ... */
    function numberRevisions(turns) {
        const order = new Map();   // path -> ids in first-seen order
        for (const t of turns)
            for (const [p, f] of Object.entries(t.files)) {
                if (typeof f.revisionId !== 'string') { f.revisionId = `r${f.revision}`; continue; }
                if (Number.isInteger(f.revision)) continue;
                const ids = order.get(p) || [];
                if (!ids.includes(f.revisionId)) ids.push(f.revisionId);
                order.set(p, ids);
                f.revision = ids.indexOf(f.revisionId) + 1;
            }
    }

    /**
     * Each turn's events as what happened since the previous turn. With `eventsFrom` (a position in the producer's log) a repeated prefix is dropped by position
     * (a cumulative list with eventsFrom 0 and a true slice read the same); without it, by comparing with the turn before.
     */
    function perTurnEvents(turns) {
        if (turns.length && turns.every((t) => Number.isInteger(t.eventsFrom))) {
            let carried = 0;
            for (const t of turns) {
                const end = t.eventsFrom + t.events.length;
                t.events = t.events.slice(Math.max(0, carried - t.eventsFrom));
                carried = Math.max(carried, end);
            }
            return;
        }
        stripCumulativeEvents(turns);
    }

    /** Events are meant to be what happened since the previous turn. If a producer writes them cumulatively, drop the prefix the last turn had. */
    function stripCumulativeEvents(turns) {
        for (let i = turns.length - 1; i > 0; --i) {
            const prev = turns[i - 1].events, cur = turns[i].events;
            if (prev.length && cur.length >= prev.length && prev.every((e, k) => sameEvent(e, cur[k]))) turns[i].events = cur.slice(prev.length);
        }
    }
    const sameEvent = (a, b) => a.carry === b.carry && a.id === b.id && a.name === b.name && a.detail === b.detail && a.path === b.path;

    // ---- the index ----------------------------------------------------------------------------------------------------------------------------------

    /**
     * Per turn: live blocks by id, retired by id, and every anchor by the block id it RESOLVES to (through merges), so "what is attached to b4" is one
     * lookup. Per id across turns: the turns it appears in. Item keys tie a layer item to itself across turns.
     */
    function buildIndex(doc) {
        const perTurn = new Map();
        const ids = new Map();   // id -> {names:Set, paths:Set}
        const note = (id, name, path) => {
            if (!id) return;
            if (!ids.has(id)) ids.set(id, { names: new Set(), paths: new Set() });
            if (name) ids.get(id).names.add(name);
            if (path) ids.get(id).paths.add(path);
        };
        for (const t of doc.turns) {
            const live = new Map(t.blocks.map((b) => [b.id, b]));
            const retired = new Map(t.retired.map((r) => [r.id, r]));
            const byPath = new Map();
            for (const b of t.blocks) {
                note(b.id, b.name, b.path);
                if (!byPath.has(b.path)) byPath.set(b.path, []);
                byPath.get(b.path).push(b);
            }
            for (const list of byPath.values()) list.sort((a, b) => a.first - b.first || b.last - a.last);
            for (const r of t.retired) note(r.id, r.name, r.path);
            for (const e of t.events) note(e.id, e.name, e.path);
            const entry = { turn: t, live, retired, byPath, attached: new Map(), mergedInto: new Map() };
            for (const r of t.retired) if (r.mergedInto) {
                if (!entry.mergedInto.has(r.mergedInto)) entry.mergedInto.set(r.mergedInto, []);
                entry.mergedInto.get(r.mergedInto).push(r.id);
            }
            perTurn.set(t.turn, entry);
            for (const [layer, items] of Object.entries(t.layers))
                for (const item of items)
                    item.anchors.forEach((anchor, anchorIndex) => {
                        const r = resolveAnchorIn(entry, anchor);
                        const key = r.block ? r.block.id : r.orphanId ? `orphan:${r.orphanId}` : `line:${anchor.path}`;
                        if (!entry.attached.has(key)) entry.attached.set(key, []);
                        entry.attached.get(key).push({ layer, item, anchor, anchorIndex, resolved: r });
                    });
        }
        return { perTurn, ids };
    }

    // ---- resolution -------------------------------------------------------------------------------------------------------------------------------

    /** Follow an id through merges at one turn: {block, via:[ids followed], status: live|merged|orphan|unknown}. */
    function resolveIdIn(entry, id) {
        const via = [];
        let cur = id;
        for (let guard = 0; guard < 64; ++guard) {
            const b = entry.live.get(cur);
            if (b) return { block: b, via, status: via.length ? 'merged' : 'live' };
            const r = entry.retired.get(cur);
            if (!r) return { block: null, via, status: via.length ? 'orphan' : 'unknown', orphanId: cur };
            if (!r.mergedInto) return { block: null, via, status: 'orphan', orphanId: cur, retired: r };
            via.push(cur);
            cur = r.mergedInto;
        }
        return { block: null, via, status: 'orphan', orphanId: id };
    }

    /**
     * Where an anchor lands at a turn. A block anchor resolves through merges to the block's CURRENT lines (an exact line inside the block is kept
     * when it still falls in it); a block-less anchor is a path and line of the revision it was found in.
     */
    function resolveAnchorIn(entry, anchor) {
        if (anchor.blockId) {
            const r = resolveIdIn(entry, anchor.blockId);
            if (r.block) {
                const b = r.block;
                const line = r.status === 'live' && anchor.line >= b.first && anchor.line <= b.last ? anchor.line : b.first;
                return { ...r, path: b.path, first: b.first, last: b.last, line };
            }
            return { ...r, path: anchor.path, first: anchor.line, last: anchor.line, line: anchor.line };
        }
        const holder = blockAtIn(entry, anchor.path, anchor.line);
        return { block: null, via: [], status: 'line', path: anchor.path, first: anchor.line, last: anchor.line, line: anchor.line, within: holder };
    }

    /** The innermost live block of `path` holding `line` at that turn, or null. */
    function blockAtIn(entry, path, line) {
        let best = null;
        for (const b of entry.byPath.get(path) || [])
            if (line >= b.first && line <= b.last && (!best || b.last - b.first <= best.last - best.first)) best = b;
        return best;
    }

    const entryOf = (doc, turn) => {
        const e = doc.index.perTurn.get(turn);
        if (!e) throw new Error(`no turn ${turn}`);
        return e;
    };

    // ---- queries the viewer asks ------------------------------------------------------------------------------------------------------------------

    /** Every layer name over all turns, in first-seen order (freeform: nothing is assumed about the set). */
    function layerNames(doc) {
        const seen = [];
        for (const t of doc.turns) for (const name of Object.keys(t.layers)) if (!seen.includes(name)) seen.push(name);
        return seen;
    }

    const turnOf = (doc, turn) => entryOf(doc, turn).turn;
    const resolveId = (doc, turn, id) => resolveIdIn(entryOf(doc, turn), id);
    const blockAt = (doc, turn, path, line) => blockAtIn(entryOf(doc, turn), path, line);
    const blocksOf = (doc, turn, path) => entryOf(doc, turn).byPath.get(path) || [];

    /** The lines of a file at a turn (1-based access via [n - 1]). */
    function fileLines(doc, turn, path) {
        const f = turnOf(doc, turn).files[path];
        return f ? splitLines(f.text) : [];
    }
    function splitLines(text) {
        const l = text.split('\n');
        if (l.length && l[l.length - 1] === '') l.pop();
        return l;
    }

    /**
     * Code -> layers: every item of every layer anchored to block `id` at `turn`, including items still naming ids that were merged into it.
     * Each hit is {layer, item, anchor, anchorIndex, resolved}. Items anchored more than once to the block appear once per anchor.
     */
    function attachedTo(doc, turn, id) {
        return entryOf(doc, turn).attached.get(id) || [];
    }

    /** Items whose anchors resolve to no live block (their block was retired without a successor). */
    function orphans(doc, turn) {
        const out = [];
        for (const [key, hits] of entryOf(doc, turn).attached) if (key.startsWith('orphan:')) out.push(...hits);
        return out;
    }

    /** Items anchored to a path and line outside every block. */
    function lineAnchored(doc, turn) {
        const out = [];
        for (const [key, hits] of entryOf(doc, turn).attached) if (key.startsWith('line:')) out.push(...hits);
        return out;
    }

    /** Per block id at a turn: {layer: count} of attached anchors, for the code view's badges. */
    function attachmentCounts(doc, turn) {
        const counts = new Map();
        for (const [key, hits] of entryOf(doc, turn).attached) {
            const c = {};
            for (const h of hits) c[h.layer] = (c[h.layer] || 0) + 1;
            counts.set(key, c);
        }
        return counts;
    }

    /**
     * Prose (or any layer) -> code: every anchor of an item resolved at `turn`, grouped by file in file order. The fan-out of a 1-N item.
     * Returns {anchors: [{anchor, anchorIndex, resolved}], files: [{path, anchors:[...]}], blocks: [ids], fileCount, blockCount}.
     */
    function fanOut(doc, turn, item) {
        const entry = entryOf(doc, turn);
        const anchors = item.anchors.map((anchor, anchorIndex) => ({ anchor, anchorIndex, resolved: resolveAnchorIn(entry, anchor) }));
        const files = new Map();
        for (const a of anchors) {
            const p = a.resolved.path || '(no path)';
            if (!files.has(p)) files.set(p, []);
            files.get(p).push(a);
        }
        for (const list of files.values()) list.sort((x, y) => x.resolved.line - y.resolved.line || (x.anchor.column || 0) - (y.anchor.column || 0));
        const blocks = [...new Set(anchors.filter((a) => a.resolved.block).map((a) => a.resolved.block.id))];
        return {
            anchors: [...files.values()].flat(),
            files: [...files.entries()].map(([path, list]) => ({ path, anchors: list })),
            blocks,
            fileCount: files.size,
            blockCount: blocks.length,
        };
    }

    /** A stable key for an item across turns: its `id` when the producer gives one, else layer + kind + normalised title. */
    function itemKey(layer, item) {
        return item.id ? `${layer}#${item.id}` : `${layer}~${item.kind || ''}~${normaliseTitle(item.title || '')}`;
    }
    const normaliseTitle = (s) => s.toLowerCase().replace(/[^a-z0-9_]+/g, ' ').trim();

    /** One item followed through every turn: [{turn, item|null, fan}] — when it appeared, changed anchors, or went away (fixed). */
    function itemTrace(doc, layer, item) {
        const key = itemKey(layer, item);
        return doc.turns.map((t) => {
            const found = (t.layers[layer] || []).find((it) => itemKey(layer, it) === key) || null;
            return { turn: t.turn, item: found, fan: found ? fanOut(doc, t.turn, found) : null };
        });
    }

    /**
     * The history of one block id over every turn: what happened to its identity, the revisions that touched it, and which layers held information
     * attached to it. Built from the per-turn index, so it costs one pass over the turns.
     *   {id, names, paths, mergedFrom:[ids], timeline:[{turn, status, block, name, events, revisions, changed, layers:{layer:[item]}, mergedInto}]}
     */
    function blockHistory(doc, id) {
        const info = doc.index.ids.get(id) || { names: new Set(), paths: new Set() };
        const mergedFrom = new Set();
        const timeline = [];
        let prev = null;   // {turn, path, text} of the block at the last turn it was live
        let prevTurn = null;
        for (const t of doc.turns) {
            const entry = entryOf(doc, t.turn);
            const block = entry.live.get(id) || null;
            const retired = entry.retired.get(id) || null;
            const events = t.events.filter((e) => e.id === id || (e.carry === 'merged' && e.detail === id));
            for (const e of t.events) if (e.carry === 'merged' && e.detail === id) mergedFrom.add(e.id);
            for (const from of entry.mergedInto.get(id) || []) mergedFrom.add(from);
            const status = block ? 'live' : retired ? (retired.mergedInto ? 'merged' : 'retired') : 'absent';
            const text = block ? blockText(doc, t.turn, block) : null;
            const changed = !!block && (!prev || prev.text !== text || prev.path !== block.path);
            // The canvas revisions of this block's file since the previous turn: what could have touched it.
            const path = block?.path || retired?.path || prev?.path || null;
            const revisions = path ? revisionsBetween(doc, path, prevTurn, t) : [];
            const layers = {};
            for (const h of attachedTo(doc, t.turn, id)) (layers[h.layer] ??= []).includes(h.item) || layers[h.layer].push(h.item);
            for (const h of entry.attached.get(`orphan:${id}`) || []) (layers[h.layer] ??= []).includes(h.item) || layers[h.layer].push(h.item);
            timeline.push({
                turn: t.turn, status, block, name: block?.name || retired?.name || null, events, revisions, changed,
                mergedInto: retired?.mergedInto || '', layers,
            });
            if (block) prev = { path: block.path, text };
            prevTurn = t;
        }
        return { id, names: [...info.names], paths: [...info.paths], mergedFrom: [...mergedFrom], timeline };
    }

    /** The text of a block at a turn. */
    function blockText(doc, turn, block) {
        return fileLines(doc, turn, block.path).slice(block.first - 1, block.last).join('\n');
    }

    /** Canvas log entries for `path` with a revision after the previous turn's revision of it and up to this turn's. */
    function revisionsBetween(doc, path, prevTurn, turn) {
        const lo = prevTurn?.files[path]?.revision ?? 0;
        const hi = turn.files[path]?.revision ?? lo;
        return doc.log.filter((e) => e.path === path && e.revision > lo && e.revision <= hi);
    }

    /** Ids known to the document whose name or id contains `q` (case-insensitive): the history query box. */
    function findBlocks(doc, q) {
        const needle = q.trim().toLowerCase();
        const out = [];
        for (const [id, info] of doc.index.ids)
            if (!needle || id.toLowerCase() === needle || [...info.names].some((n) => n.toLowerCase().includes(needle)))
                out.push({ id, names: [...info.names], paths: [...info.paths] });
        return out.sort((a, b) => Number(a.id.slice(1)) - Number(b.id.slice(1)) || a.id.localeCompare(b.id));
    }

    // ---- 1-N workflows: duplicates, unified reports, batch fixes ----------------------------------------------------------------------------------

    const tokens = (s) => new Set(normaliseTitle(s).split(' ').filter((w) => w.length > 1));
    function jaccard(a, b) {
        if (!a.size && !b.size) return 1;
        let n = 0;
        for (const x of a) if (b.has(x)) ++n;
        return n / (a.size + b.size - n);
    }

    /**
     * Groups of items in one layer at one turn that look like the same finding: same kind, titles with token Jaccard >= `threshold` (0.4), and at least one
     * resolved block in common (or both block-less). The first step of merging duplicates into one unified report.
     */
    function duplicateGroups(doc, turn, layer, threshold = 0.4) {
        const items = turnOf(doc, turn).layers[layer] || [];
        const entry = entryOf(doc, turn);
        const info = items.map((it) => ({
            it,
            words: tokens(it.title || ''),
            blocks: new Set(it.anchors.map((a) => resolveAnchorIn(entry, a)).map((r) => r.block?.id || `${r.path}:${r.line}`)),
        }));
        const parent = info.map((_, i) => i);
        const find = (i) => (parent[i] === i ? i : (parent[i] = find(parent[i])));
        for (let i = 0; i < info.length; ++i)
            for (let j = i + 1; j < info.length; ++j) {
                const a = info[i], b = info[j];
                if ((a.it.kind || '') !== (b.it.kind || '')) continue;
                if (jaccard(a.words, b.words) < threshold) continue;
                if (![...a.blocks].some((x) => b.blocks.has(x))) continue;
                parent[find(j)] = find(i);
            }
        const groups = new Map();
        info.forEach((x, i) => {
            const r = find(i);
            if (!groups.has(r)) groups.set(r, []);
            groups.get(r).push(x.it);
        });
        return [...groups.values()].filter((g) => g.length > 1);
    }

    /**
     * The unified report of several items: the first item's title, every detail, and the union of their anchors (an anchor at the same block, path,
     * line and column counts once). `mergedFrom` keeps the sources so a producer can record the merge.
     */
    function unify(layer, items) {
        const seen = new Set();
        const anchors = [];
        for (const it of items)
            for (const a of it.anchors) {
                const k = `${a.blockId}|${a.path}|${a.line}|${a.column || 0}`;
                if (!seen.has(k)) { seen.add(k); anchors.push(a); }
            }
        return {
            id: `unified:${items.map((it) => it.id || it.number).join('+')}`,
            number: items[0].number,
            kind: items[0].kind,
            title: items[0].title,
            detail: items.map((it) => `#${it.number} ${it.title}\n${it.detail || ''}`).join('\n\n'),
            anchors,
            mergedFrom: items.map((it) => itemKey(layer, it)),
            fix: items.find((it) => it.fix)?.fix,
        };
    }

    /**
     * What an item's proposed fix would change at each anchor, without changing anything: [{path, line, before, after, anchorIndex, applies}].
     * Supports `{kind:"regex", pattern, replacement, flags}` on the anchor's line. An anchor's own `fix` overrides the item's. Lines are deduplicated:
     * a regex with the g flag fixes every site on a line at once, so several anchors on one line yield one change.
     */
    function previewFix(doc, turn, item) {
        const entry = entryOf(doc, turn);
        const out = [];
        const done = new Set();
        item.anchors.forEach((anchor, anchorIndex) => {
            const fix = anchor.fix || item.fix;
            if (!fix) return;
            const r = resolveAnchorIn(entry, anchor);
            const lines = fileLines(doc, turn, r.path);
            const line = r.status === 'live' || r.status === 'line' ? anchor.line : r.line;
            const key = `${r.path}:${line}`;
            if (done.has(key)) return;
            done.add(key);
            const before = lines[line - 1];
            if (before === undefined) { out.push({ path: r.path, line, before: '', after: '', anchorIndex, applies: false, reason: 'line not in file' }); return; }
            let after = before, reason = '';
            if (fix.kind === 'regex') {
                try { after = before.replace(new RegExp(fix.pattern, fix.flags ?? 'g'), fix.replacement ?? ''); } catch (e) { reason = e.message; }
            } else reason = `unsupported fix kind "${fix.kind}"`;
            out.push({ path: r.path, line, before, after, anchorIndex, applies: !reason && after !== before, reason: reason || (after === before ? 'no match on this line' : '') });
        });
        return out;
    }

    // A missing parameter is not turn 0: snapshot bundles number their turns from 0, so Number(null) would open every bundle on its base commit.
    const pickTurn = (turnNumbers, param) => {
        const t = param === null || param === undefined || param === '' ? NaN : Number(param);
        return turnNumbers.includes(t) ? t : turnNumbers[turnNumbers.length - 1];
    };

    return {
        SCHEMA, pickTurn,
        validateTurn, loadEntries, splitLines,
        layerNames, turnOf, resolveId, blockAt, blocksOf, fileLines,
        attachedTo, orphans, lineAnchored, attachmentCounts, fanOut, itemKey, itemTrace, blockHistory, findBlocks,
        duplicateGroups, unify, previewFix,
    };
});
