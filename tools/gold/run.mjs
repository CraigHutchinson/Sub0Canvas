#!/usr/bin/env node
// Gold extraction pipeline: census, hunks, join. Reads a local git repository named by an alias (never a path in argv or in a script) and writes
// only under out/gold/<run>/, which git ignores. Every stage writes a manifest and a rerun skips a finished stage (--force repeats it).
//   node tools/gold/run.mjs --alias NAME --since "30 days ago" [--stage census|hunks|join|all] [--min-touches N] [--run ID] [--force]
// Git cost rules: each stage is one batched git walk (pathspecs are split only to fit the command line, never one process per file or commit),
// and every call carries the maximum rename limit so a large commit keeps its renames.
import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const GIT_SETTINGS = ['-c', 'core.quotepath=off', '-c', 'diff.renameLimit=32767'];
export const CODE = /\.(cpp|h|hpp|c|cc|mm|cs|js|ts|py|shader|cginc|hlsl|java|kt|swift|rs|go)$/;
const CMD_BUDGET = 24000;   // characters of pathspec per git call: Windows allows about 32k on a command line

/** The alias table: this checkout as `self`, `.git/sub0canvas/repos.json`, then SUB0CANVAS_REPOS (`name=path;...`). */
export function repositories() {
    const repos = { self: root };
    try {
        const common = path.resolve(root, execFileSync('git', ['-C', root, 'rev-parse', '--git-common-dir'], { encoding: 'utf8' }).trim());
        const file = path.join(common, 'sub0canvas', 'repos.json');
        if (fs.existsSync(file)) Object.assign(repos, JSON.parse(fs.readFileSync(file, 'utf8')));
    } catch { /* not a checkout */ }
    for (const pair of (process.env.SUB0CANVAS_REPOS ?? '').split(';').filter(Boolean)) {
        const at = pair.indexOf('=');
        if (at > 0) repos[pair.slice(0, at)] = pair.slice(at + 1);
    }
    return repos;
}

/** Splits pathspecs into batches that each fit one command line. */
export function chunkPaths(paths, budget = CMD_BUDGET) {
    const out = [];
    let cur = [], size = 0;
    for (const p of paths) {
        if (cur.length && size + p.length + 3 > budget) { out.push(cur); cur = []; size = 0; }
        cur.push(p);
        size += p.length + 3;
    }
    if (cur.length) out.push(cur);
    return out;
}

/** Runs git, streaming stdout to a file (a monorepo's log is never held in memory); rejects on a non-zero exit. */
function gitToFile(repo, args, file) {
    return new Promise((resolve, reject) => {
        const out = fs.createWriteStream(file);
        const p = spawn('git', ['-C', repo, ...GIT_SETTINGS, ...args], { stdio: ['ignore', 'pipe', 'inherit'], windowsHide: true });
        p.stdout.pipe(out);
        p.on('error', reject);
        p.on('close', (code) => out.end(() => (code === 0 ? resolve(code) : reject(new Error(`git exited ${code}`)))));
    });
}

const sha = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const hist = (a, edges) => {
    const h = {};
    for (const x of a) { const k = edges.find((e) => x < e) ?? `${edges.at(-1)}+`; h[k] = (h[k] || 0) + 1; }
    return h;
};

function manifest(dir, stage, data) {
    fs.writeFileSync(path.join(dir, `${stage}.manifest.json`), JSON.stringify({ stage, at: new Date().toISOString(), ...data }, null, 2) + '\n');
}
const finished = (dir, stage, force) => !force && fs.existsSync(path.join(dir, `${stage}.manifest.json`));

/** G0: every commit in the window with its files, every merge with its header, and the files touched at least `minTouches` times. */
export async function census(repo, dir, { since, minTouches }) {
    const files = path.join(dir, 'commits-files.tsv'), merges = path.join(dir, 'merges.tsv');
    await gitToFile(repo, ['log', `--since=${since}`, '--no-merges', '--name-only', '--format=C%x09%H%x09%ct'], files);
    await gitToFile(repo, ['log', `--since=${since}`, '--merges', '--format=M%x09%H%x09%ct%x09%P%x09%s'], merges);
    const touches = new Map();
    let commits = 0;
    for (const l of fs.readFileSync(files, 'utf8').split('\n')) {
        if (!l) continue;
        if (l.startsWith('C\t')) commits++;
        else if (CODE.test(l)) touches.set(l, (touches.get(l) || 0) + 1);
    }
    const set = [...touches].filter(([, n]) => n >= minTouches).sort((a, b) => b[1] - a[1]);
    fs.writeFileSync(path.join(dir, 'set.tsv'), set.map(([f, n]) => `${f}\t${n}`).join('\n') + (set.length ? '\n' : ''));
    const mergeLines = fs.readFileSync(merges, 'utf8').split('\n').filter(Boolean);
    const m = {
        commits, mergeCommits: mergeLines.length, mergesWithPr: mergeLines.filter((l) => /\tMerge (PR|pull request) #\d+/.test(l)).length,
        codeFilesTouched: touches.size, touchHistogram: hist([...touches.values()], [2, 3, 5, 10]), minTouches, setFiles: set.length,
    };
    manifest(dir, 'census', { since, ...m, outputs: { 'commits-files.tsv': sha(files), 'merges.tsv': sha(merges) } });
    return m;
}

/** G1: the -U0 hunks of every commit in the window that touches the set, one batched walk per command-line-sized slice. */
export async function hunks(repo, dir, { since }) {
    const paths = fs.readFileSync(path.join(dir, 'set.tsv'), 'utf8').split('\n').filter(Boolean).map((l) => l.split('\t')[0]);
    const parts = chunkPaths(paths);
    const outputs = {};
    for (const [i, part] of parts.entries()) {
        const f = path.join(dir, `hunks-${i}.txt`);
        await gitToFile(repo, ['log', `--since=${since}`, '--no-merges', '-M', '-p', '-U0', '--no-color', '--format=@C%x09%H%x09%ct', '--', ...part], f);
        outputs[`hunks-${i}.txt`] = sha(f);
    }
    manifest(dir, 'hunks', { since, files: paths.length, gitCalls: parts.length, outputs });
    return { files: paths.length, gitCalls: parts.length };
}

/** G2, first cut: groups hunks by (file, function named in the hunk header) and measures how often one function is hit by several commits. */
export function join(dir) {
    const fn = new Map(), commits = new Map();
    let hunkCount = 0, unnamed = 0;
    for (const part of fs.readdirSync(dir).filter((f) => /^hunks-\d+\.txt$/.test(f)).sort()) {
        let cur = null, file = null;
        for (const line of fs.readFileSync(path.join(dir, part), 'utf8').split('\n')) {
            if (line.startsWith('@C\t')) {
                const id = line.split('\t')[1];
                cur = commits.get(id) ?? commits.set(id, { files: new Set(), hunks: 0 }).get(id);
                file = null;
            } else if (line.startsWith('+++ b/')) {
                file = line.slice(6);
            } else if (line.startsWith('@@') && cur && file) {
                const m = /^@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@ ?(.*)$/.exec(line);
                if (!m) continue;
                hunkCount++;
                const named = m[1].trim() !== '';
                if (!named) unnamed++;
                const key = `${file}|${named ? m[1].trim().slice(0, 80) : ''}`;
                (fn.get(key) ?? fn.set(key, new Set()).get(key)).add(cur);
                cur.files.add(file);
                cur.hunks++;
            }
        }
    }
    const named = [...fn].filter(([k]) => !k.endsWith('|')).map(([, s]) => s.size);
    const metrics = {
        commits: commits.size, hunks: hunkCount, hunksWithoutFunction: unnamed, namedFunctions: named.length,
        commitsPerFunction: hist(named, [2, 3, 4, 6, 11, 21]), functionsHitTwiceOrMore: named.filter((x) => x >= 2).length,
        functionsHitThreeOrMore: named.filter((x) => x >= 3).length, maxCommitsPerFunction: named.length ? Math.max(...named) : 0,
        hunksPerCommit: hist([...commits.values()].map((c) => c.hunks), [2, 3, 5, 10, 25, 100]),
        filesPerCommit: hist([...commits.values()].map((c) => c.files.size), [2, 3, 5, 10]),
    };
    fs.writeFileSync(path.join(dir, 'metrics.json'), JSON.stringify(metrics, null, 2) + '\n');
    manifest(dir, 'join', { metrics: 'metrics.json' });
    return metrics;
}

/** The only place a run directory is made: under out/gold/ of this checkout, whatever the alias or run id says. */
export function runDir(run, base = path.join(root, 'out', 'gold')) {
    const dir = path.resolve(base, run);
    if (!dir.startsWith(base + path.sep)) throw new Error(`run id escapes out/gold: ${run}`);
    fs.mkdirSync(dir, { recursive: true });
    return dir;
}

async function main() {
    const a = process.argv.slice(2);
    const get = (k, d) => { const i = a.indexOf(`--${k}`); return i < 0 ? d : a[i + 1]; };
    const alias = get('alias'), since = get('since', '30 days ago'), stage = get('stage', 'all'), force = a.includes('--force');
    const minTouches = Number(get('min-touches', 10));
    if (!alias) {
        console.error('usage: run.mjs --alias NAME --since "30 days ago" [--stage census|hunks|join|all] [--min-touches N] [--run ID] [--force]');
        process.exit(2);
    }
    const repo = repositories()[alias];
    if (!repo) { console.error(`unknown alias "${alias}" (register it in .git/sub0canvas/repos.json)`); process.exit(2); }
    const dir = runDir(get('run', `${alias}-${since.replace(/[^A-Za-z0-9]+/g, '-')}`));
    const want = (s) => stage === 'all' || stage === s;
    if (want('census')) console.log(finished(dir, 'census', force) ? 'census: finished earlier (--force repeats it)' : `census ${JSON.stringify(await census(repo, dir, { since, minTouches }))}`);
    if (want('hunks')) console.log(finished(dir, 'hunks', force) ? 'hunks: finished earlier (--force repeats it)' : `hunks ${JSON.stringify(await hunks(repo, dir, { since }))}`);
    if (want('join')) console.log(`join ${JSON.stringify(join(dir))}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch((e) => { console.error(e.message); process.exit(1); });
