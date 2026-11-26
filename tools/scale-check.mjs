#!/usr/bin/env node
// Gate for a scale-test bundle: fails when the view is too small to say anything about scale. A bundle that opens on one commit, or holds a few
// dozen files, passes every viewer test and proves nothing, so the minimums are explicit.
//   node tools/scale-check.mjs out/scale.bundle.json [--turns N] [--files N] [--blocks N]
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const require = createRequire(import.meta.url);
const M = require('../src/model.js');

/** What one working week of a busy subtree looks like: about 20 commits over about a thousand files and ten thousand blocks. */
export const MINIMUMS = { turns: 20, files: 1000, blocks: 10000 };

/** Returns the list of failed minimums (empty when the bundle is representative). */
export function checkScale(entries, minimums = MINIMUMS) {
    const { docs, problems } = M.loadEntries(entries);
    const fails = problems.map((p) => `load: ${p}`);
    if (!docs.length) return [...fails, 'load: no document'];
    const doc = docs[0];
    const last = doc.turns.at(-1);
    const got = { turns: doc.turns.length, files: Object.keys(last.files).length, blocks: last.blocks.length };
    for (const k of Object.keys(minimums)) if (got[k] < minimums[k]) fails.push(`${k}: ${got[k]} is below the minimum ${minimums[k]}`);
    const commits = last.layers.commits?.length ?? 0;
    if (commits !== got.turns) fails.push(`commits layer: ${commits} items for ${got.turns} turns`);
    if (M.pickTurn(doc.turnNumbers, null) !== doc.turnNumbers.at(-1)) fails.push('the viewer does not open on the latest turn');
    return fails;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [file, ...rest] = process.argv.slice(2);
    if (!file) { console.error('usage: scale-check.mjs BUNDLE [--turns N] [--files N] [--blocks N]'); process.exit(2); }
    const mins = { ...MINIMUMS };
    for (let i = 0; i < rest.length; i += 2) mins[rest[i].replace(/^--/, '')] = Number(rest[i + 1]);
    const fails = checkScale(JSON.parse(readFileSync(file, 'utf8')), mins);
    if (fails.length) { console.error(fails.join('\n')); process.exit(1); }
    console.log(`scale-check: ok (minimums ${JSON.stringify(mins)})`);
}
