// The mirror's outputs, as pure functions of a generated mirror (mirror.js): the git fast-import stream (`local` mode), the `layered-doc-1`
// snapshots the viewer opens (`canvas` mode), and the ordered steps that recreate the project on a forge (`github` mode). The CLI (cli.js) does the
// I/O; tests call these directly.
'use strict';

const { applyOp, locate } = require('./mirror.js');

// ---- local: git fast-import ------------------------------------------------------------------------------------------------------------------------

/** A fast-import stream whose commits hash to exactly the mirror's commit ids (the mirror computes them; `local` mode checks git agrees). */
function fastImportStream(m) {
    const parts = [];
    const blobMarks = new Map(), commitMarks = new Map();
    let mark = 0;
    const data = (buf) => { parts.push(Buffer.from(`data ${buf.length}\n`), buf, Buffer.from('\n')); };
    for (const c of m.commits) {
        const parent = c.parents.length ? m.bySha.get(c.parents[0]).tree : new Map();
        const changed = [], removed = [];
        for (const [p, f] of c.tree) {
            const r = m.render(f);
            if (!parent.has(p) || m.render(parent.get(p)).blob !== r.blob) changed.push([p, r]);
        }
        for (const p of parent.keys()) if (!c.tree.has(p)) removed.push(p);
        for (const [, r] of changed) {
            if (blobMarks.has(r.blob)) continue;
            blobMarks.set(r.blob, ++mark);
            parts.push(Buffer.from(`blob\nmark :${mark}\n`)); data(r.bytes);
        }
        commitMarks.set(c.sha, ++mark);
        const who = (p) => `${p.name} <${p.email}> ${c.time} +0000`;
        parts.push(Buffer.from(`commit refs/heads/${c.branch}\nmark :${mark}\nauthor ${who(c.author)}\ncommitter ${who(c.committer)}\n`));
        data(Buffer.from(c.message, 'utf8'));
        if (c.parents[0]) parts.push(Buffer.from(`from :${commitMarks.get(c.parents[0])}\n`));
        for (const p of c.parents.slice(1)) parts.push(Buffer.from(`merge :${commitMarks.get(p)}\n`));
        for (const p of removed.sort()) parts.push(Buffer.from(`D ${p}\n`));
        for (const [p, r] of changed.sort((a, b) => (a[0] < b[0] ? -1 : 1))) parts.push(Buffer.from(`M 100644 :${blobMarks.get(r.blob)} ${p}\n`));
        parts.push(Buffer.from('\n'));
    }
    for (const [b, sha] of [...m.branches.entries()].sort()) parts.push(Buffer.from(`reset refs/heads/${b}\nfrom :${commitMarks.get(sha)}\n\n`));
    parts.push(Buffer.from('done\n'));
    return Buffer.concat(parts);
}

// ---- canvas: weekly layered-doc-1 snapshots of trunk, with the topic as layers --------------------------------------------------------------------

const DAY = 86400;

/**
 * The topic seen as a layered document: one turn per week of the window (trunk at that time), the topic's code files, and layers `changes` (one item
 * per change open or decided by then, anchored to its footprint), `topics`, `spikes` (each spike hunk with its state at that time) and `review`.
 * Returns [{path, text}] under `<name>/`, the entries the viewer's loader takes.
 */
function canvasEntries(m, { name = `mirror-seed-${m.seed}` } = {}) {
    const mainCommits = m.commits.filter((c) => c.branch === 'main');
    const start = m.bySha.get(m.windowStart).time;
    const truth = m.truth;
    const topicBlocks = new Set(), topicFiles = new Set();
    for (const c of Object.values(truth.changes)) { for (const b of c.footprint) topicBlocks.add(b); for (const f of c.files) if (/\.(cpp|h|py)$/.test(f)) topicFiles.add(f); }
    const entries = [];
    const revisions = new Map();                     // path -> {rev, blob}
    const canvasLog = [];
    let prevBlocks = new Map();
    const weeks = 8;
    for (let w = 0; w < weeks; w++) {
        const cutoff = start + w * 7 * DAY + (w === weeks - 1 ? 7 * DAY : 0);
        const tipCommit = mainCommits.filter((c) => c.time <= cutoff).at(-1);
        const tree = tipCommit.tree;
        const files = {}, blocks = [], live = new Map();
        // The topic's files as they are now: a moved file or block is followed by its blocks, not by its old path.
        const topicPaths = new Set([...topicFiles].filter((p) => tree.has(p)));
        for (const b of topicBlocks) { const p = locate(tree, '', b); if (p) topicPaths.add(p); }
        for (const p of [...topicPaths].sort()) {
            const f = tree.get(p);
            if (!f || f.kind !== 'code') continue;
            const r = m.render(f);
            const prev = revisions.get(p);
            if (!prev || prev.blob !== r.blob) {
                const rev = (prev?.rev ?? 0) + 1;
                revisions.set(p, { rev, blob: r.blob });
                const text = r.bytes.toString('utf8');
                entries.push({ path: `${name}/canvas/${p.replace(/\//g, '%2F')}.r${rev}`, text });
                canvasLog.push({ path: p, by: w === 0 ? 'trunk' : `trunk ${tipCommit.sha.slice(0, 10)}`, revision: rev, base: rev - 1, from: 1, to: text.split('\n').length - 1, delta: 0 });
            }
            files[p] = { revision: revisions.get(p).rev, text: r.bytes.toString('utf8') };
            for (const b of f.blocks) {
                const [first, last] = r.spans[b.id];
                const blk = { id: b.id, path: p, name: b.name, signature: b.sig, first, last };
                blocks.push(blk); live.set(b.id, blk);
            }
        }
        const events = [], retired = [];
        for (const [id, b] of live) {
            const was = prevBlocks.get(id);
            if (!was) { if (w > 0) events.push({ carry: 'added', id, path: b.path, name: b.name }); }
            else if (was.name !== b.name) events.push({ carry: 'renamed', id, path: b.path, name: b.name, detail: was.name });
            else if (was.signature !== b.signature) events.push({ carry: 'resigned', id, path: b.path, name: b.name, detail: was.signature });
        }
        for (const [id, b] of prevBlocks) if (!live.has(id)) { retired.push({ id, path: b.path, name: b.name, mergedInto: '' }); events.push({ carry: 'retired', id, path: b.path, name: b.name }); }
        prevBlocks = live;

        const stateAt = (key) => {
            const s = m.changeState.get(key);
            if (s.opened > cutoff) return null;
            if (s.reverted && s.reverted <= cutoff) return 'reverted';
            if (s.merged && s.merged <= cutoff) return 'merged';
            if (s.closed && s.closed <= cutoff) return 'closed';
            return 'open';
        };
        const anchor = (blockId) => (live.has(blockId) ? [{ blockId, path: live.get(blockId).path, line: live.get(blockId).first, confidence: 'exact' }] : []);
        const linksOf = (key) => truth.links.filter((l) => l.from === key || l.to === key).map((l) => `${l.from} ${l.kind} ${l.to}`);
        const changes = [];
        for (const ch of m.plan) {
            const st = stateAt(ch.key);
            if (!st) continue;
            const s = m.changeState.get(ch.key), t = truth.changes[ch.key];
            changes.push({ id: `change/${s.number}`, number: changes.length + 1, kind: ch.kind, title: `#${s.number} ${ch.title}`,
                detail: [`state: ${st}${ch.draft && st === 'open' ? ' (draft)' : ''}`, `base: ${s.baseBranch}`, `branch: ${s.branch}`, `topic: ${ch.topic}`, `files: ${t.files.length}`, ...linksOf(ch.key)].join('\n'),
                anchors: t.footprint.flatMap(anchor) });
        }
        const spikes = [];
        for (const sp of m.plan.filter((c) => c.kind === 'spike' && stateAt(c.key))) {
            const pushed = sp.commits.filter(([day]) => start + day * DAY <= cutoff + 0.02 * DAY).flatMap(([, h]) => h).filter((h) => h.lineage && /^L\d+$/.test(h.lineage));
            for (const h of pushed) {
                const holders = m.plan.filter((c) => c.kind !== 'spike' && c.kind !== 'origin' && c.commits.some(([, hs]) => hs.some((x) => x.lineage === h.lineage)));
                const st = holders.map((c) => ({ c, st: stateAt(c.key) })).filter((x) => x.st);
                let state;
                if (st.some((x) => x.st === 'merged' && m.changeState.get(x.c.key).baseBranch === 'main')) state = 'landed';
                else if (st.some((x) => x.st === 'open')) state = 'inFlight';
                else if (st.length) state = 'dropped';
                else state = applyOp(tree, h).ok ? 'notCarved' : 'obsolete';
                spikes.push({ id: `spike/${sp.key}/${h.lineage}`, number: spikes.length + 1, kind: state, title: `${sp.key} ${h.lineage}: ${state}`,
                    detail: `${h.op} in ${h.path}${st.length ? `\nheld by ${st.map((x) => `#${m.changeState.get(x.c.key).number} (${x.st})`).join(', ')}` : ''}`, anchors: anchor(h.block) });
            }
        }
        const topicCounts = {};
        for (const ch of m.plan) { const st = stateAt(ch.key); if (st) { topicCounts[ch.topic] ??= {}; topicCounts[ch.topic][st] = (topicCounts[ch.topic][st] ?? 0) + 1; } }
        const topics = Object.entries(topicCounts).map(([k, c], i) => ({ id: `topic/${k}`, number: i + 1, kind: 'topic', title: `${k}: ${Object.entries(c).map(([s, n]) => `${n} ${s}`).join(', ')}`,
            detail: (m.state.issues.find((x) => x.key === k)?.title) ?? '', anchors: [] }));
        const review = m.state.reviews.filter((r) => Date.parse(r.at) / 1000 <= cutoff).map((r, i) => ({ id: `review/${r.pull}/${i}`, number: i + 1, kind: r.state, title: `#${r.pull}: ${r.body}`, detail: `${r.author} on ${r.path}`, anchors: anchor(r.blockId) }));
        const turn = { schema: 'layered-doc-1', turn: w, revision: tipCommit.sha, files, blocks, retired, events, layers: { changes, topics, spikes, review } };
        entries.push({ path: `${name}/layers/turn-${w}.json`, text: JSON.stringify(turn, null, 1) + '\n' });
    }
    const filesNow = Object.fromEntries([...revisions].map(([p, r]) => [p, r.rev]));
    entries.push({ path: `${name}/canvas.json`, text: JSON.stringify({ files: filesNow, log: canvasLog }, null, 1) + '\n' });
    return entries;
}

// ---- github: the steps that recreate the project and its pull requests on a forge -----------------------------------------------------------------

/**
 * Ordered steps (argv arrays) that publish the mirror to a new forge repository and replay its pull requests in time order: history is pushed as it
 * happened, a pull request is opened when its branch first exists, a merge is the push of the merge commit (the forge marks the pull request
 * merged), a close is a close. `{owner}`, `{repo}`, `{remote}` and `{pr:KEY}` are filled in by the executor. Review comments are not replayed.
 */
function githubPlan(m, { repo, visibility = 'private', drafts = true } = {}) {
    const full = `{owner}/${repo}`;
    const steps = [];
    const sh = (argv, note) => steps.push({ argv, note });
    sh(['gh', 'repo', 'create', full, `--${visibility}`, '--description', `${m.product.name}: an invented project generated by Sub0Canvas tools/mirror (seed ${m.seed}); test data only.`], 'create the repository');
    sh(['git', 'remote', 'add', '{remote}', `https://github.com/${full}.git`], 'point the local mirror at it');
    sh(['gh', 'label', 'create', 'spike', '-R', full, '--color', 'C5DEF5', '--description', 'Exploration, never merged', '--force'], 'label');
    for (const iss of m.state.issues) sh(['gh', 'issue', 'create', '-R', full, '--title', `[${iss.key}] ${iss.title}`, '--body', `Tracker item ${iss.key} (${iss.type}, ${iss.status}).\n\nLinks: ${iss.links.map((l) => `${l.type} ${l.key}`).join(', ') || 'none'}`], `tracker item ${iss.key}`);

    const pushed = new Map();                        // branch -> sha last pushed
    // Change branches are pushed with force: a rebase rewrites one (the force-pushed-rebase edge case); shared branches only ever move forward.
    const changeBranches = new Set([...m.changeState.values()].map((s) => s.branch));
    const push = (branch, sha, note) => {
        if (pushed.get(branch) === sha) return;
        pushed.set(branch, sha);
        sh(['git', 'push', '{remote}', `${changeBranches.has(branch) ? '+' : ''}${sha}:refs/heads/${branch}`], note);
    };
    const lastOn = (branch, time) => m.commits.filter((c) => c.branch === branch && c.time <= time).at(-1);
    const advanceShared = (time) => {
        for (const b of ['main', 'release/2.0', 'release/2.1', 'feature/scan-v2']) {
            let c = lastOn(b, time);
            if (!c && m.branches.has(b)) {               // a branch created by pointing at another's commit: its first own commit's parent, or its final tip
                const own = m.commits.find((x) => x.branch === b);
                const at = own ? m.bySha.get(own.parents[0]) : m.bySha.get(m.branches.get(b));
                if (at && at.time <= time && (own || pushed.has('main'))) c = at;
            }
            if (c) push(b, c.sha, `advance ${b}`);
        }
    };
    const timeline = [];
    for (const ch of m.plan) {
        const s = m.changeState.get(ch.key);
        s.pushes.forEach((sha, i) => timeline.push({ time: m.bySha.get(sha).time, kind: i === 0 ? 'open' : 'push', ch, sha }));
        if (s.merged) timeline.push({ time: s.merged, kind: 'merge', ch });
        if (s.closed) timeline.push({ time: s.closed, kind: 'close', ch });
    }
    timeline.sort((a, b) => a.time - b.time);
    for (const ev of timeline) {
        advanceShared(ev.time);
        const s = m.changeState.get(ev.ch.key);
        if (ev.kind === 'open' || ev.kind === 'push') push(s.branch, ev.sha, `${ev.ch.key}: branch`);
        if (ev.kind === 'open') {
            const p = m.state.pulls.find((x) => x.key === ev.ch.key);
            const argv = ['gh', 'pr', 'create', '-R', full, '--base', s.baseBranch, '--head', s.branch, '--title', p.title, '--body', p.body];
            if (p.draft && drafts) argv.push('--draft');
            sh(argv, `{pr:${ev.ch.key}} open`);
            for (const l of p.labels) sh(['gh', 'pr', 'edit', `{pr:${ev.ch.key}}`, '-R', full, '--add-label', l], `${ev.ch.key}: label`);
        }
        if (ev.kind === 'merge') push(s.baseBranch, s.mergeSha, `${ev.ch.key}: merge (the forge marks the pull request merged)`);
        if (ev.kind === 'close') sh(['gh', 'pr', 'close', `{pr:${ev.ch.key}}`, '-R', full], `${ev.ch.key}: close`);
    }
    for (const [b, sha] of [...m.branches.entries()].sort()) push(b, sha, `final ${b}`);
    return steps;
}

module.exports = { fastImportStream, canvasEntries, githubPlan };
