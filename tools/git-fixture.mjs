// The G1 git fixture (docs/TESTING.md rule 2 and "The git fixture"): an INVENTED small C++ project, "tinycalc", built as a real local git repository of
// 200 first-parent commits with scripted lives for five functions (edits, a rename, signature changes, a move between files, file renames, a deletion and a
// revival, a merge of a side branch, a helper folded into another function), and the plumbing output captured from it, so that tests read files and never run git
// or touch the network (R-SRC-8). Nothing here is read from any real project.
//
//   node tools/git-fixture.mjs capture [--out fixtures/git/tinycalc]   build the repository in a temporary directory and write the captured output
//   node tools/git-fixture.mjs build DIR                               build the repository in DIR (an empty or missing directory), for the live-git tests
//
// The truth is NOT produced here: fixtures/scenarios/g1-tinycalc/truth.json is written by hand from the plan in `STEPS` below (and checked against
// `git log -L` and the diffs), and the tests compare the library's answers with it. Commit ids are deterministic (fixed identity, dates and contents), so a
// regeneration is byte-identical (test/git_fixture.test.js).
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
export const LOG_FORMAT = '%x01%H%x02%P%x02%an%x02%aI%x02%s';
const LOG_ARGS = ['log', '--first-parent', '--diff-merges=first-parent', '-M', '-C', '--name-status', '-z', `--format=${LOG_FORMAT}`];
export const COMMITS = 200;
/// The windows a test replays from mid-history: the last `n` revisions of `scope` (`RevisionIndex::Options::maxRevisions`). The base revision's tree and texts are
/// captured so that the baseline listing is answerable.
export const WINDOWS = [{ name: 'src-last-40', scope: ['src/'], n: 40 }];
/// Scopes whose path-limited log git's own answer is captured for (the cross-check of the test source's emulation of a pathspec, R-SCALE-9).
export const PATH_LOGS = [
    { name: 'src', scope: ['src/'] },
    { name: 'util', scope: ['src/util.cpp'] },
    { name: 'geometry', scope: ['src/geometry.cpp'] },
    { name: 'statistics', scope: ['src/statistics.cpp'] },
    { name: 'numeric', scope: ['src/numeric.cpp'] },
    { name: 'docs', scope: ['docs/'] },
];
export const SHALLOW_DEPTH = 25;

// ---- the project: files of functions, rendered Allman style -------------------------------------------------------------------------------------------------

/// A function: its signature, body lines and optional doc comment line.
const fn = (sig, body, doc = null) => ({ sig, body: [...body], doc });
const nameOf = (f) => /(\w+)\(/.exec(f.sig)[1];

function render(file) {
    const out = [...file.head];
    for (const f of file.fns) {
        out.push('');
        if (f.doc) out.push(f.doc);
        out.push(f.sig, '{', ...f.body.map((l) => '    ' + l), '}');
    }
    return out.join('\n') + '\n';
}

const project = new Map();   // path -> {head, fns} for code, a string for text
const code = (p) => project.get(p);

function initialProject() {
    project.clear();
    project.set('README.md', '# tinycalc\n\nA small invented calculator library used only as a test fixture.\n');
    project.set('docs/notes.md', '# Notes\n\n');
    project.set('data/table.txt', 'version 1\n');
    project.set('src/util.cpp', {
        head: ['#include <cmath>', ''],
        fns: [
            fn('double clamp(double value, double floorv, double ceilv)', ['if (value < floorv) return floorv;', 'if (value > ceilv) return ceilv;', 'return value;'], '// Keeps a value inside a closed range.'),
            fn('double lerp(double fromv, double tov, double weight)', ['return fromv + (tov - fromv) * weight;'], '// Blends two values.'),
            fn('int sign(double signval)', ['const int rev = 0;', 'if (signval > 0.5) return 1;', 'if (signval < -0.5) return -1;', 'return 0;']),
        ],
    });
    project.set('src/stats.cpp', {
        head: ['#include <vector>', ''],
        fns: [
            fn('double mean(const std::vector<double>& xs)', ['double total = 0;', 'for (double x : xs) total += x;', 'return xs.empty() ? 0 : total / xs.size();'], '// The arithmetic mean.'),
            fn('double variance(const std::vector<double>& xs)', ['const int rev = 0;', 'double sqsum = 0;', 'for (double v : xs) sqsum += (v - 1.5) * (v - 1.5);', 'return sqsum / 2;']),
            fn('double median(const std::vector<double>& xs)', ['const int rev = 0;', 'std::vector<double> ordered = xs;', 'std::nth_element(ordered.begin(), ordered.begin() + 1, ordered.end());', 'return ordered[1];']),
        ],
    });
    project.set('src/geometry.cpp', {
        head: ['#include <cmath>', '', 'constexpr double kTwoPi = 6.283185307179586;', 'constexpr double kPiApprox = 3.14159;'],
        fns: [
            fn('double area(double radius)', ['const int rev = 0;', 'return kPiApprox * radius * radius;']),
            fn('double perimeter(double sideA, double sideB)', ['const int rev = 0;', 'return 2 * (sideA + sideB);']),
            fn('double normalize(double angle)', ['double turns = angle / kTwoPi;', 'return (turns - std::floor(turns)) * kTwoPi;'], '// Wraps an angle into one turn.'),
        ],
    });
    project.set('src/parse.cpp', {
        head: ['#include <cstdlib>', '#include <string>', ''],
        fns: [
            fn('double parseNumber(const std::string& text)', ['double parsed = std::strtod(text.c_str(), nullptr);', 'if (parsed != parsed) return 0.0;', 'return parsed;'], '// Reads a decimal number.'),
            fn('std::string tokenize(const std::string& line)', ['const int rev = 0;', "std::string trimmed = line.substr(line.find_first_not_of(' '));", 'return trimmed;']),
        ],
    });
    project.set('src/main.cpp', { head: ['#include <cstdio>', ''], fns: [fn('int main()', ['const int rev = 0;', 'std::puts("tinycalc");', 'return 0;'])] });
    project.set('tests/test_calc.cpp', {
        head: ['#include <cassert>', ''],
        fns: [
            fn('void testClampRange()', ['const int rev = 0;', 'assert(clamp(5, 0, 3) == 3);']),
            fn('void testMeanBasic()', ['const int rev = 0;', 'assert(mean({1, 3}) == 2);']),
            fn('void testAreaUnit()', ['const int rev = 0;', 'assert(area(1) > 3);']),
        ],
    });
}

const find = (p, name) => code(p).fns.find((f) => nameOf(f) === name);
const take = (p, name) => { const i = code(p).fns.findIndex((f) => nameOf(f) === name); return code(p).fns.splice(i, 1)[0]; };

// ---- the scripted lives (the plan the truth is derived from) -----------------------------------------------------------------------------------------------

/// What each special commit does. Every other commit is noise (see `noise`). Commit numbers are the labels in the subjects (c001 ... c200).
const STEPS = {
    1: { msg: 'initial project', run() { /* the project as built */ } },
    14: { msg: 'clamp: a NaN guard', run() { find('src/util.cpp', 'clamp').body.splice(2, 0, 'if (value != value) return floorv;'); } },
    18: { msg: 'rename mean to average', run() { const f = find('src/stats.cpp', 'mean'); f.sig = f.sig.replace('mean(', 'average('); f.doc = '// The arithmetic average.'; } },
    22: { msg: 'remove parseNumber (unused)', run() { take('src/parse.cpp', 'parseNumber'); } },
    31: { msg: 'clamp: a strict flag', run() { find('src/util.cpp', 'clamp').sig = 'double clamp(double value, double floorv, double ceilv, bool strict)'; } },
    35: { msg: 'remove lerp (unused)', run() { take('src/util.cpp', 'lerp'); } },
    42: { msg: 'average: return a double zero', run() { find('src/stats.cpp', 'average').body[2] = 'return xs.empty() ? 0.0 : total / xs.size();'; } },
    48: { msg: 'normalize: guard NaN', run() { find('src/geometry.cpp', 'normalize').body.unshift('if (angle != angle) return 0.0;'); } },
    57: {
        msg: 'move clamp to mathx',
        run() {
            const clamp = take('src/util.cpp', 'clamp');
            project.set('src/mathx.cpp', {
                head: ['#include <cmath>', ''],
                fns: [clamp, fn('long isqrt(long squarev)', ['long rootv = static_cast<long>(std::sqrt(static_cast<double>(squarev)));', 'while (rootv * rootv > squarev) --rootv;', 'return rootv;'])],
            });
        },
    },
    66: { msg: 'bring lerp back, in mathx', run() { code('src/mathx.cpp').fns.push(fn('double lerp(double fromv, double tov, double weight)', ['return fromv + (tov - fromv) * weight;'], '// Blends two values.')); } },
    70: { msg: 'bring parseNumber back', run() { code('src/parse.cpp').fns.unshift(fn('double parseNumber(const std::string& text)', ['double parsed = std::strtod(text.c_str(), nullptr);', 'if (parsed != parsed) return 0.0;', 'return parsed;'], '// Reads a decimal number.')); } },
    83: { msg: 'clamp: honour strict', run() { find('src/mathx.cpp', 'clamp').body.splice(2, 0, 'if (strict && value == ceilv) return ceilv;'); } },
    92: { msg: 'rename stats.cpp to statistics.cpp', rename: ['src/stats.cpp', 'src/statistics.cpp'] },
    100: { msg: 'add wrapAngle', run() { code('src/geometry.cpp').fns.push(fn('double wrapAngle(double rawAngle)', ['double cycles = rawAngle / kTwoPi;', 'return (cycles - std::floor(cycles)) * kTwoPi;'], '// Wraps a raw angle.')); } },
    106: { msg: 'notes', side: true, run() { code('docs/notes.md'); project.set('docs/notes.md', project.get('docs/notes.md') + 'note 106: branching for the side work\n'); } },
    110: { msg: 'merge the side branch', merge: true },
    121: { msg: 'rename mathx.cpp to numeric.cpp', rename: ['src/mathx.cpp', 'src/numeric.cpp'] },
    130: { msg: 'lerp: clamp the weight', run() { find('src/numeric.cpp', 'lerp').body.unshift('weight = weight < 0 ? 0 : weight;'); } },
    140: {
        msg: 'split scanDigits out of parseNumber',
        run() {
            find('src/parse.cpp', 'parseNumber').body = ['size_t used = scanDigits(text);', 'double parsed = std::strtod(text.c_str(), nullptr);', 'if (used == 0) return 0.0;', 'return parsed;'];
            code('src/parse.cpp').fns.splice(0, 0, fn('size_t scanDigits(const std::string& text)', ['size_t used = 0;', 'while (used < text.size() && std::isdigit(text[used])) ++used;', 'return used;']));
        },
    },
    151: { msg: 'average: take a span', run() { find('src/statistics.cpp', 'average').sig = 'double average(std::span<const double> xs)'; } },
    160: { msg: 'normalize: simpler arithmetic', run() { const f = find('src/geometry.cpp', 'normalize'); f.body[f.body.length - 1] = 'return (turns - std::floor(turns)) * kTwoPi + 0.0;'; } },
    177: { msg: 'parseNumber: a base argument', run() { const f = find('src/parse.cpp', 'parseNumber'); f.sig = 'double parseNumber(const std::string& text, int base)'; f.body[1] = 'double parsed = std::strtod(text.c_str(), nullptr) + base * 0;'; } },
    185: {
        msg: 'inline wrapAngle into normalize',
        run() {
            take('src/geometry.cpp', 'wrapAngle');
            find('src/geometry.cpp', 'normalize').body = ['if (angle != angle) return 0.0;', 'double cycles = angle / kTwoPi;', 'return (cycles - std::floor(cycles)) * kTwoPi + 0.0;'];
        },
    },
};
/// The two commits on the side branch, merged at c110: they edit `normalize` only.
const SIDE = [
    { msg: 'side: normalize without the extra turn', run() { const f = find('src/geometry.cpp', 'normalize'); f.body[f.body.length - 1] = 'return (turns - std::floor(turns)) * kTwoPi * 1.0;'; } },
    { msg: 'side: say what normalize does', run() { find('src/geometry.cpp', 'normalize').doc = '// Wraps an angle into one turn, [0, 2pi).'; } },
];

/// Noise: a deterministic mix of commits that touch other things (notes, tests, data, untracked functions, sometimes a tracked file's other function). Returns
/// the commit's subject. Commits 107-109 stay in the notes so that the side branch merges cleanly.
function noise(n) {
    const pick = (n * 7919 + 13) % 20;
    const bump = (p, name) => { const f = find(p, name); const i = f.body.findIndex((l) => l.startsWith('const int rev')); f.body[i] = `const int rev = ${n};`; };
    if (pick < 9 || (n >= 106 && n <= 109)) { project.set('docs/notes.md', project.get('docs/notes.md') + `note ${n}: nothing important\n`); return 'notes'; }
    if (pick < 12) { const t = ['testClampRange', 'testMeanBasic', 'testAreaUnit'][n % 3]; bump('tests/test_calc.cpp', t); return `tests: ${t}`; }
    if (pick < 14) { project.set('data/table.txt', project.get('data/table.txt') + `row ${n}\n`); return 'data'; }
    const targets = [['src/util.cpp', 'sign'], ['src/stats.cpp', 'variance'], ['src/stats.cpp', 'median'], ['src/geometry.cpp', 'area'], ['src/geometry.cpp', 'perimeter'], ['src/parse.cpp', 'tokenize'], ['src/main.cpp', 'main']];
    let [p, name] = targets[n % targets.length];
    if (n >= 92 && p === 'src/stats.cpp') p = 'src/statistics.cpp';
    bump(p, name);
    return `tweak ${name}`;
}

// ---- building the repository -------------------------------------------------------------------------------------------------------------------------------

const label = (n) => `c${String(n).padStart(3, '0')}`;
const unixOf = (n, extraMinutes = 0) => Date.UTC(2026, 0, 1, 0, extraMinutes) / 1000 + n * 3600;

const FLAGS = ['-c', 'core.autocrlf=false', '-c', 'core.safecrlf=false', '-c', 'commit.gpgsign=false'];
function run(cwd, args, input = null) {
    return execFileSync('git', [...FLAGS, ...args], { cwd, maxBuffer: 1 << 28, input: input ?? undefined, stdio: [input === null ? 'ignore' : 'pipe', 'pipe', 'pipe'] });
}

/// Builds the repository as a bare repository in `dir` through one `git fast-import` (a stream of commits, each the whole project: 200 commits in a second, where
/// 200 checkouts and commits took 40). 200 commits on `main` (labels c001..c200), and a side branch between c106 and c110 that main merges (a real two-parent commit).
export function buildRepository(dir) {
    fs.mkdirSync(dir, { recursive: true });
    run(dir, ['init', '-q', '--bare', '-b', 'main', '.']);
    initialProject();
    const chunks = [];
    const emit = (s) => chunks.push(Buffer.from(s));
    let marks = 0;
    const commitOf = (ref, subject, when, from, merge = null) => {
        const mark = ++marks;
        const who = `Test Author <author@example.invalid> ${when} +0000`;
        const msg = Buffer.from(`${subject}\n`);
        emit(`commit refs/heads/${ref}\nmark :${mark}\nauthor ${who}\ncommitter ${who}\ndata ${msg.length}\n`);
        chunks.push(msg);
        emit('\n');
        if (from) emit(`from :${from}\n`);
        if (merge) emit(`merge :${merge}\n`);
        emit('deleteall\n');
        for (const [p, v] of project) {
            const body = Buffer.from(typeof v === 'string' ? v : render(v));
            emit(`M 100644 inline ${p}\ndata ${body.length}\n`);
            chunks.push(body);
            emit('\n');
        }
        return mark;
    };
    let main = 0, side = 0;
    for (let n = 1; n <= COMMITS; ++n) {
        const step = STEPS[n];
        if (step?.rename) {
            const [from, to] = step.rename;
            project.set(to, project.get(from));
            project.delete(from);
        } else if (step?.merge) {
            SIDE.forEach((s) => s.run());   // main takes the side branch's edits
        }
        const subject = step ? (step.run?.(), step.msg) : noise(n);
        main = commitOf('main', `${label(n)}: ${subject}`, unixOf(n), main, step?.merge ? side : null);
        if (step?.side) {
            const saved = structuredClone(project);
            SIDE.forEach((s, i) => { s.run(); side = commitOf('side', s.msg, unixOf(n, 10 + i), i === 0 ? main : side); });
            project.clear();
            for (const [k, v] of saved) project.set(k, v);   // main continues from c106, without the side edits, until the merge
        }
    }
    run(dir, ['fast-import', '--quiet'], Buffer.concat(chunks));
    run(dir, ['update-ref', '-d', 'refs/heads/side']);   // the side commits stay reachable through the merge
    return dir;
}

// ---- capture -----------------------------------------------------------------------------------------------------------------------------------------------

/// The records of a `--name-status -z` log: [{id, subject, changes: [{status, path, from}]}]. (The library has its own parser; this one is independent and only
/// picks what to capture.)
function parseLogForCapture(buf) {
    const out = [];
    for (const rec of buf.toString('latin1').split('\x01').slice(1)) {
        const [id, , , , rest] = rec.split('\x02');
        const parts = rest.split('\0');
        const subject = parts.shift();
        const flat = parts.map((e) => e.replace(/^\n/, '')).filter((e, k, a) => !(e === '' && k === a.length - 1));
        const changes = [];
        for (let i = 0; i < flat.length;) {
            const status = flat[i++];
            if (status === '') continue;
            const a = flat[i++];
            const b = status[0] === 'R' || status[0] === 'C' ? flat[i++] : null;
            changes.push({ status, path: b ?? a, from: b === null ? null : a });
        }
        out.push({ id, subject, changes });
    }
    return out;
}

function catBatch(dir, refs) {
    return execFileSync('git', ['-C', dir, 'cat-file', '--batch'], { input: refs.join('\n') + '\n', maxBuffer: 1 << 28, env: { ...process.env, GIT_NO_LAZY_FETCH: '1' } });
}

/// Every (commit, path) a replay under `prefix` reads: the non-deleted changes (a rename's target included) of the whole-repository log.
function refsUnder(logBuf, prefix) {
    const refs = [];
    for (const c of parseLogForCapture(logBuf)) for (const ch of c.changes) if (ch.status[0] !== 'D' && ch.path.startsWith(prefix)) refs.push(`${c.id}:${ch.path}`);
    return refs;
}

export function capture(outDir) {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'sub0canvas-g1-'));
    try {
        const dir = buildRepository(path.join(tmp, 'tinycalc'));
        fs.mkdirSync(outDir, { recursive: true });
        const put = (name, data) => fs.writeFileSync(path.join(outDir, name), data);
        const text = (args) => run(dir, args).toString('latin1');
        const head = text(['rev-parse', 'main']).trim();

        const log = run(dir, [...LOG_ARGS, 'main']);
        put('log.z', log);
        for (const p of PATH_LOGS) put(`log-path-${p.name}.z`, run(dir, [...LOG_ARGS, 'main', '--', ...p.scope]));

        // Texts: every non-deleted change under src/ at its commit, then the full trees of the windows' base revisions.
        const refs = refsUnder(log, 'src/');
        const windows = [];
        for (const w of WINDOWS) {
            const ids = text(['log', '--first-parent', `-n${w.n}`, '--format=%H', 'main', '--', ...w.scope]).trim().split('\n');
            const base = text(['rev-parse', `${ids[ids.length - 1]}^`]).trim();
            const tree = run(dir, ['ls-tree', '-r', '-z', base, '--', ...w.scope]);
            put(`tree-${w.name}.z`, tree);
            for (const e of tree.toString('latin1').split('\0').filter(Boolean)) refs.push(`${base}:${e.split('\t')[1]}`);
            windows.push({ name: w.name, scope: w.scope, n: w.n, base, listing: `tree-${w.name}.z` });
        }
        put('blobs.refs', refs.join('\n') + '\n');
        put('blobs.batch', catBatch(dir, refs));

        // A shallow clone of it (a local file URL: no network), as git itself serves a cut history.
        const shallowDir = path.join(tmp, 'shallow');
        execFileSync('git', [...FLAGS, 'clone', '-q', '--depth', String(SHALLOW_DEPTH), '--no-local', '--branch', 'main', pathToFileURL(dir).href, shallowDir], { stdio: 'ignore' });
        const shallowLog = run(shallowDir, [...LOG_ARGS, 'main']);
        put('shallow-log.z', shallowLog);
        const shallowRefs = refsUnder(shallowLog, 'src/');
        put('shallow-blobs.refs', shallowRefs.join('\n') + '\n');
        put('shallow-blobs.batch', catBatch(shallowDir, shallowRefs));

        // The whole diff of the commit that renamed stats.cpp, as `changes()` is asked for it.
        const renameCommit = parseLogForCapture(log).find((c) => c.subject.startsWith(label(92) + ':')).id;
        put('diff-rename.z', run(dir, ['diff-tree', '-r', '-M', '-C', '--name-status', '-z', `${renameCommit}^`, renameCommit]));

        put('manifest.json', JSON.stringify({ schema: 'g1-git-fixture-1', project: 'tinycalc', commits: COMMITS, head, shallowDepth: SHALLOW_DEPTH, windows, renameCommit,
            pathLogs: PATH_LOGS.map((p) => ({ name: p.name, scope: p.scope })) }, null, 2) + '\n');
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const [cmd, arg] = process.argv.slice(2);
    if (cmd === 'capture') {
        const i = process.argv.indexOf('--out');
        capture(path.resolve(here, '..', i > 0 ? process.argv[i + 1] : 'fixtures/git/tinycalc'));
    } else if (cmd === 'build' && arg) {
        buildRepository(path.resolve(arg));
    } else {
        console.error('usage: node tools/git-fixture.mjs capture [--out DIR] | build DIR');
        process.exit(2);
    }
}
