// A large generated subtree for K0: the reference scale of docs/AUTHORING.md section 3.2 and SCALE.md section 6.1 (1,590 files, about 20,000 blocks), as a
// layered-doc-1 bundle the viewer loads with ?bundle=. Deterministic (no randomness, no clock), invented code, no relation to any real codebase.
// Texts travel by id (`texts`, `textId`) as a producer at scale would send them, so a three-turn bundle costs the changed files, not three copies.
export const REFERENCE = { files: 1590, turns: 3 };

const MODULES = ['core', 'engine', 'io', 'net', 'ui', 'tools', 'store', 'sync'];
const NOUNS = ['Album', 'Photo', 'Cache', 'Index', 'Queue', 'Pool', 'Page', 'Tag', 'Cursor', 'Batch', 'Scan', 'Thumb'];
const VERBS = ['load', 'flush', 'find', 'resolve', 'push', 'read', 'build', 'merge', 'refresh', 'visit'];

/** The path of file `i`: eight modules, 20 subfolders each, ten files in a folder (159 folders), so a folder of 10 and a module of about 200 files exist. */
export function pathOf(i) {
    const folder = Math.floor(i / 10);
    const module = MODULES[folder % MODULES.length];
    const sub = `${NOUNS[Math.floor(folder / MODULES.length) % NOUNS.length].toLowerCase()}${Math.floor(folder / (MODULES.length * NOUNS.length))}`;
    return `${module}/${sub}/f${String(i).padStart(4, '0')}.cpp`;
}

/** A file's text and its blocks (ids from `firstId`): 12 or 13 functions of 5 to 8 lines, so the average is 12.6 blocks a file. */
export function fileOf(i, firstId) {
    const count = 12 + (i % 5 < 3 ? 1 : 0);
    const path = pathOf(i);
    const lines = [`// ${path}`, '#include "k0/types.h"', '', 'namespace k0 {', ''];
    const blocks = [];
    let id = firstId;
    for (let k = 0; k < count; k++) {
        const noun = NOUNS[(i + k) % NOUNS.length], verb = VERBS[(i * 7 + k) % VERBS.length];
        const name = `${noun}${i % 97}::${verb}${noun}${k}`;
        const signature = `int ${name}(int a, int b)`;
        const body = 3 + ((i + k) % 4);
        const first = lines.length + 1;
        lines.push(signature, '{');
        for (let j = 0; j < body; j++) lines.push(`    total += a * ${i + k + j} + b;   // ${verb} step ${j}`);
        lines.push('    return total;', '}');
        blocks.push({ id: `b${id++}`, path, name, signature, first, last: lines.length });
        lines.push('');
    }
    lines.push('}  // namespace k0', '');
    return { path, text: lines.join('\n'), blocks, next: id };
}

/** The entries of the bundle (`{path, text}`, what `npm run mirror -- canvas` writes and the viewer's ?bundle= loads) and its size. */
export function largeEntries({ files = REFERENCE.files, turns = REFERENCE.turns, name = 'k0-large' } = {}) {
    const base = [];
    let id = 1;
    for (let i = 0; i < files; i++) { const f = fileOf(i, id); id = f.next; base.push(f); }
    const allBlocks = base.flatMap((f) => f.blocks);
    // Two layers with anchors: items to centre on (a layer view builds one card per item) and anchors that resolve to the blocks above.
    const anchorsOf = (n, step) => Array.from({ length: 1 + (n % 3) }, (_, k) => {
        const b = allBlocks[(n * step + k * 131) % allBlocks.length];
        return { blockId: b.id, path: b.path, line: b.first, confidence: 'exact' };
    });
    const layers = {
        issues: Array.from({ length: 300 }, (_, n) => ({ id: `issue-${n + 1}`, number: n + 1, kind: n % 4 ? 'bug' : 'idea', title: `Issue ${n + 1}: ${VERBS[n % VERBS.length]} is slow`,
            detail: `Seen in ${anchorsOf(n, 53)[0].path}`, anchors: anchorsOf(n, 53) })),
        review: Array.from({ length: 120 }, (_, n) => ({ id: `note-${n + 1}`, number: n + 1, title: `Note ${n + 1}`, detail: 'Check this block.', anchors: anchorsOf(n, 97) })),
    };
    const entries = [];
    const texts = {};
    for (let t = 1; t <= turns; t++) {
        const turnFiles = {};
        // Every 20th file changes per turn after the first: a new revision (a trailing comment line; the blocks are unchanged).
        for (let i = 0; i < files; i++) {
            const f = base[i];
            const changedNow = t > 1 && i % 20 === 0;
            const textId = `t${i}-${changedNow ? t : 1}`;
            if (!(textId in texts)) texts[textId] = changedNow ? `${f.text}// turn ${t}\n` : f.text;
            turnFiles[f.path] = { revision: changedNow ? t : 1, textId };
        }
        const turn = { schema: 'layered-doc-1', turn: t, revision: `k0-${t}`, files: turnFiles, blocks: allBlocks, retired: [], events: [], layers };
        if (t === 1) turn.texts = { ...texts };
        else turn.texts = Object.fromEntries(Object.entries(texts).filter(([, v]) => v.endsWith(`// turn ${t}\n`)));
        entries.push({ path: `${name}/layers/turn-${t}.json`, text: JSON.stringify(turn) });
    }
    entries.push({ path: `${name}/canvas.json`, text: JSON.stringify({ files: {}, log: [] }) });
    return { entries, files, blocks: allBlocks.length, turns };
}
