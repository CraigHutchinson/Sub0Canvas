#!/usr/bin/env node
// The synthetic mirror's command line (docs/MIRROR.md). Every mode starts from the same seeded generation (mirror.js), so its outputs agree.
//
//   node tools/mirror/cli.js shape  [--seed N]                       topic and repository statistics against the anonymised targets
//   node tools/mirror/cli.js source [--seed N] [--serve]             {state, truth} as JSON; --serve: a DocumentSource/ItemSource over JSON lines on stdio
//   node tools/mirror/cli.js local  [--seed N] --out DIR             DIR/repo (git, every commit id checked), DIR/mirror-state.json, DIR/mirror-truth.json
//   node tools/mirror/cli.js canvas [--seed N] --out DIR             DIR/mirror-seed-N/: layered-doc-1 turns the viewer opens (one per week of the
//                                                                     topic), and DIR/mirror-seed-N.bundle.json for index.html?bundle=<url>
//   node tools/mirror/cli.js github [--seed N] --out DIR --repo NAME [--public] [--no-drafts] [--execute]
//                                                                     the steps that publish it to a new forge repository and replay its pull
//                                                                     requests; prints them, and runs them only with --execute
//   node tools/mirror/cli.js fixture [--seed N] [--out DIR]          DIR/seed-N/{state,truth,shape}.json (default DIR: fixtures/mirror), the
//                                                                     committed fixture that test/mirror.test.js checks against regeneration
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { spawnSync } = require('node:child_process');
const { generateMirror, shape } = require('./mirror.js');
const { fastImportStream, canvasEntries, githubPlan } = require('./outputs.js');

function parseArgs(argv) {
    const out = { mode: argv[0], seed: 1 };
    for (let i = 1; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--seed') out.seed = Number(argv[++i]);
        else if (a === '--out') out.out = argv[++i];
        else if (a === '--repo') out.repo = argv[++i];
        else if (a === '--serve') out.serve = true;
        else if (a === '--public') out.visibility = 'public';
        else if (a === '--no-drafts') out.drafts = false;
        else if (a === '--execute') out.execute = true;
        else fail(`unknown argument ${a}`);
    }
    if (!Number.isInteger(out.seed) || out.seed < 0) fail('--seed takes a non-negative integer');
    return out;
}

function fail(msg) { process.stderr.write(`mirror: ${msg}\n`); process.exit(2); }

function run(cmd, args, opts = {}) {
    const r = spawnSync(cmd, args, { encoding: opts.input ? undefined : 'utf8', maxBuffer: 1 << 30, ...opts });
    if (r.status !== 0) fail(`${cmd} ${args.join(' ')} failed (${r.status}): ${String(r.stderr ?? '').trim()}`);
    return r;
}

function writeJson(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, JSON.stringify(value, null, 1) + '\n'); }

/** Build DIR/repo through git fast-import and check that git's commit ids are the ones the generator computed. */
function local(m, out) {
    const repo = path.join(out, 'repo');
    if (fs.existsSync(repo)) fail(`${repo} exists; choose an empty --out`);
    run('git', ['init', '-q', '--initial-branch=main', repo]);
    run('git', ['-C', repo, '-c', 'core.autocrlf=false', '-c', 'core.ignorecase=false', 'fast-import', '--quiet', '--done'], { input: fastImportStream(m) });
    const refs = run('git', ['-C', repo, 'for-each-ref', '--format=%(refname:short) %(objectname)', 'refs/heads']).stdout.trim().split('\n');
    const got = Object.fromEntries(refs.map((l) => l.split(' ')));
    const bad = [...m.branches].filter(([b, sha]) => got[b] !== sha);
    if (bad.length || refs.length !== m.branches.size) fail(`git's commit ids differ from the generator's for ${bad.map(([b]) => b).join(', ') || 'the branch count'}`);
    run('git', ['-C', repo, '-c', 'core.autocrlf=false', 'checkout', '-q', '-f', 'main']);
    writeJson(path.join(out, 'mirror-state.json'), m.state);
    writeJson(path.join(out, 'mirror-truth.json'), m.truth);
    return repo;
}

function serve(m) {
    const methods = {
        revisions: ({ from, to }) => m.source.revisions(from, to),
        files: ({ revision }) => m.source.files(revision),
        changedPaths: ({ a, b }) => m.source.changedPaths(a, b),
        blocks: ({ revision, path: p }) => m.source.blocks(revision, p),
        fetch: ({ cursor }) => m.items.fetch(cursor),
        state: () => m.state,
        truth: () => m.truth,
    };
    const rl = readline.createInterface({ input: process.stdin });
    rl.on('line', (line) => {
        if (!line.trim()) return;
        let req;
        try { req = JSON.parse(line); } catch (e) { process.stdout.write(JSON.stringify({ error: `bad JSON: ${e.message}` }) + '\n'); return; }
        const fn = methods[req.method];
        const reply = fn ? (() => { try { return { id: req.id, result: fn(req.params ?? {}) }; } catch (e) { return { id: req.id, error: e.message }; } })()
            : { id: req.id, error: `unknown method ${req.method}; known: ${Object.keys(methods).join(', ')}` };
        process.stdout.write(JSON.stringify(reply) + '\n');
    });
}

function github(m, args) {
    if (!args.repo) fail('github needs --repo NAME (for example sub0canvas_test1234)');
    if (!/^[A-Za-z0-9._-]+$/.test(args.repo)) fail('--repo is a bare repository name');
    const steps = githubPlan(m, { repo: args.repo, visibility: args.visibility ?? 'private', drafts: args.drafts !== false });
    if (!args.execute) {
        for (const s of steps) process.stdout.write(`${s.argv.map((a) => (/[\s"]/.test(a) ? JSON.stringify(a) : a)).join(' ')}    # ${s.note}\n`);
        process.stdout.write(`# ${steps.length} steps; nothing was run. Add --execute to run them (creates a ${args.visibility ?? 'private'} repository under your forge account).\n`);
        return;
    }
    if (!args.out) fail('github --execute needs --out DIR (the local mirror is built there, or reused)');
    const repo = fs.existsSync(path.join(args.out, 'repo')) ? path.join(args.out, 'repo') : local(m, args.out);
    const owner = run('gh', ['api', 'user', '--jq', '.login']).stdout.trim();
    const remote = 'mirror';
    const prs = {};
    const fill = (a) => a.replace('{owner}', owner).replace('{remote}', remote).replace(/\{pr:(\w+)\}/, (_, k) => String(prs[k]));
    for (const s of steps) {
        const argv = s.argv.map(fill);
        process.stdout.write(`> ${s.note}\n`);
        const r = run(argv[0], argv.slice(1), { cwd: repo });
        const opened = s.note.match(/^\{pr:(\w+)\} open$/);
        if (opened) prs[opened[1]] = Number(String(r.stdout).trim().match(/\/pull\/(\d+)/)?.[1]);
    }
    writeJson(path.join(args.out, 'github-map.json'), { repository: `${owner}/${args.repo}`, pulls: prs });
}

function main() {
    const args = parseArgs(process.argv.slice(2));
    const m = generateMirror({ seed: args.seed });
    switch (args.mode) {
        case 'shape': process.stdout.write(JSON.stringify(shape(m), null, 1) + '\n'); break;
        case 'source': if (args.serve) serve(m); else process.stdout.write(JSON.stringify({ state: m.state, truth: m.truth }) + '\n'); break;
        case 'local': if (!args.out) fail('local needs --out DIR'); process.stdout.write(`${local(m, args.out)}\n`); break;
        case 'canvas': {
            if (!args.out) fail('canvas needs --out DIR');
            const entries = canvasEntries(m);
            for (const e of entries) { const p = path.join(args.out, e.path); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, e.text); }
            // The same files as one bundle, for the served viewer: index.html?bundle=<this file's URL>.
            const bundleFile = path.join(args.out, `mirror-seed-${m.seed}.bundle.json`);
            fs.writeFileSync(bundleFile, JSON.stringify(entries));
            process.stdout.write(`${path.join(args.out, `mirror-seed-${m.seed}`)}\n${bundleFile}\n`);
            break;
        }
        case 'github': github(m, args); break;
        case 'fixture': {
            const dir = path.join(args.out ?? path.join(__dirname, '..', '..', 'fixtures', 'mirror'), `seed-${m.seed}`);
            writeJson(path.join(dir, 'state.json'), m.state);
            writeJson(path.join(dir, 'truth.json'), m.truth);
            writeJson(path.join(dir, 'shape.json'), shape(m));
            process.stdout.write(`${dir}\n`);
            break;
        }
        default: fail('mode is one of shape, source, local, canvas, github, fixture');
    }
}

main();
