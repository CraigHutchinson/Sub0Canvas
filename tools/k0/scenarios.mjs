// K0's scenarios: one function per row of docs/AUTHORING.md section 3.2's budget table, injected into the page as `window.__k0`. Each performs its input on a
// real element (a click or an input event on the element the viewer attached its handler to) and times from the input to the next painted frame: a
// requestAnimationFrame callback runs just before the frame is painted and the task queued after it runs once that frame is done. Every repetition does
// work: the scenarios alternate between two states (expand, then collapse; two files; two blocks in different files; first and last turn; `a`, then `ab`).
// Measurement only: nothing here changes the viewer.

/** Scenario ids in the order they run (the rows of perf/k0/budgets.json they fill; `centre` fills two rows). */
export const SCENARIOS = ['open-file', 'select-block', 'select-block-code', 'switch-turn', 'centre', 'filter-key', 'folder-toggle'];

/** The page-side runner, as source text for Runtime.evaluate. `__k0.run(id, reps)` resolves `{ samples: { <row>: [ms...] } }` or `{ error }`. */
export const PAGE_SCRIPT = String.raw`(() => {
    const V = window.S0View;
    const frame = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));
    const settle = async () => { await frame(); await frame(); };
    const time = async (act) => { await settle(); const t0 = performance.now(); act(); await frame(); return performance.now() - t0; };
    const q = (s) => document.querySelector(s), qa = (s) => [...document.querySelectorAll(s)];
    const need = (x, what) => { if (!x) throw new Error('no ' + what); return x; };
    const reset = async () => {
        Object.assign(V.state, { query: '', centre: 'code', block: '', item: '', a: 0, fix: '', trace: '', unified: '' });
        V.state.turn = V.state.doc.turnNumbers[0];
        V.render();
        await settle();
    };
    const byTitle = (sel, title) => need(qa(sel).find((e) => e.getAttribute('title') === title), sel + ' ' + title);
    const byBid = (sel, bid) => need(qa(sel).find((e) => e.querySelector('.bid')?.textContent === bid), sel + ' ' + bid);
    const sizeOf = (row) => Number(row.lastElementChild?.textContent) || 0;

    const scenarios = {
        async floor(n) { await reset(); const out = []; for (let i = 0; i < n; i++) out.push(await time(() => {})); return { floor: out }; },
        async 'open-file'(n) {
            await reset();
            const files = qa('.trow.file');
            const [a, b] = [need(files[0], 'file row').title, need(files[1], 'second file row').title];
            const out = [];
            for (let i = 0; i < n; i++) { const t = i % 2 ? a : b; const el = byTitle('.trow.file', t); out.push(await time(() => el.click())); }
            return { 'open-file': out };
        },
        async 'select-block'(n) {
            await reset();
            const rows = qa('#nav .blockrow');
            const ids = [rows[3], rows[rows.length >> 1]].map((r) => need(r, 'block row').querySelector('.bid').textContent);
            const out = [];
            for (let i = 0; i < n; i++) { const el = byBid('#nav .blockrow', ids[i % 2]); out.push(await time(() => el.click())); }
            return { 'select-block': out };
        },
        async 'select-block-code'(n) {
            await reset();
            const heads = qa('.code .blockhead');
            const ids = [heads[1], heads[3]].map((r) => need(r, 'block header in the code').querySelector('.bid').textContent);
            const out = [];
            for (let i = 0; i < n; i++) { const el = byBid('.code .blockhead', ids[i % 2]); out.push(await time(() => el.click())); }
            return { 'select-block-code': out };
        },
        async 'switch-turn'(n) {
            await reset();
            const turns = V.state.doc.turnNumbers;
            const ends = [turns[turns.length - 1], turns[0]];
            const out = [];
            for (let i = 0; i < n; i++) { const el = need(qa('#turns .turn').find((b) => b.textContent === 'turn ' + ends[i % 2]), 'turn button'); out.push(await time(() => el.click())); }
            return { 'switch-turn': out };
        },
        async centre(n) {
            await reset();
            const layers = qa('.layerbtn').filter((b) => b.querySelector('span:nth-child(2)').textContent !== 'code');
            const biggest = layers.map((b) => [b, Number(b.lastElementChild.textContent.split(' ')[0]) || 0]).sort((x, y) => y[1] - x[1])[0];
            const name = need(biggest, 'layer button')[0].querySelector('span:nth-child(2)').textContent;
            const find = (l) => need(qa('.layerbtn').find((b) => b.querySelector('span:nth-child(2)').textContent === l), 'layer ' + l);
            const layer = [], code = [];
            for (let i = 0; i < n; i++) {
                const to = find(name); layer.push(await time(() => to.click()));
                const back = find('code'); code.push(await time(() => back.click()));
            }
            return { 'centre-layer': layer, 'centre-code': code };
        },
        async 'filter-key'(n) {
            await reset();
            const input = need(q('#nav input[type=search]'), 'filter input');
            const out = [];
            for (let i = 0; i < n; i++) out.push(await time(() => { input.value = i % 2 ? 'ab' : 'a'; input.dispatchEvent(new Event('input', { bubbles: true })); }));
            return { 'filter-key': out };
        },
        async 'folder-toggle'(n) {
            await reset();
            const dirs = qa('.trow.dir');
            const biggest = need(dirs.sort((x, y) => sizeOf(y) - sizeOf(x))[0], 'folder row');
            const title = biggest.title, below = sizeOf(biggest);
            const out = [];
            for (let i = 0; i < n; i++) { const el = byTitle('.trow.dir', title); out.push(await time(() => el.click())); }
            return { 'folder-toggle': out, below };
        },
    };

    window.__k0 = {
        sizes() {
            const d = V.state.doc, t = d.turns[d.turns.length - 1];
            return { files: Object.keys(t.files).length, blocks: t.blocks.length, turns: d.turnNumbers.length, domNodes: document.querySelectorAll('*').length };
        },
        async run(id, n) { try { return await scenarios[id](n); } catch (e) { return { error: String(e && e.message || e) }; } },
    };
    return true;
})()`;

/** Installed before the page's own scripts: records when the tree and the first file are painted, as performance.now() (the time since navigation start). */
export const FIRST_VIEW_SCRIPT = String.raw`(() => {
    const r = window.__k0first = { tree: null, file: null };
    const stamp = (k) => requestAnimationFrame(() => setTimeout(() => { r[k] = performance.now(); }, 0));
    let seenTree = false, seenFile = false;
    new MutationObserver(() => {
        if (!seenTree && document.querySelector('.filetree .trow')) { seenTree = true; stamp('tree'); }
        if (!seenFile && document.querySelector('.code .line')) { seenFile = true; stamp('file'); }
    }).observe(document, { childList: true, subtree: true });
})()`;
