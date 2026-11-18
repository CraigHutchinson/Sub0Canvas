#!/usr/bin/env node
// A zero-dependency server for the viewer, for browsers or tools that will not run scripts from file://, and a snapshot backend.
// Usage: node tools/serve.mjs [port]  (0 picks a free port)
//
//   /<file>                         the viewer's files (never .git/)
//   /snapshot?repo=NAME&paths=a,b[&rev=REV][&history=N][&max-files=N]
//                                   a bundle made from a local git repository by `sub0canvas snapshot` (app/), streamed: nothing is written to
//                                   disk. Open it with index.html?bundle=<that URL, encoded>.
//   /snapshot/repos                 the repository names a snapshot may read
//
// Repositories are named, never given by path in a request: `self` is this one; others come from SUB0CANVAS_REPOS ("name=path;name=path") or from
// <git common dir>/sub0canvas/repos.json ({"name": "path"}), which is local to this clone and never tracked, so a private repository's location never
// enters this public tree.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { existsSync, readFileSync } from 'node:fs';
import { join, normalize, extname, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, execFileSync } from 'node:child_process';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const port = Number(process.env.PORT ?? process.argv[2] ?? 8765);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' };

function repositories() {
    const repos = { self: root };
    try {
        const common = resolve(root, execFileSync('git', ['-C', root, 'rev-parse', '--git-common-dir'], { encoding: 'utf8' }).trim());
        const file = join(common, 'sub0canvas', 'repos.json');
        if (existsSync(file)) Object.assign(repos, JSON.parse(readFileSync(file, 'utf8')));
    } catch { /* not a git checkout: only `self` */ }
    for (const pair of (process.env.SUB0CANVAS_REPOS ?? '').split(';').filter(Boolean)) {
        const at = pair.indexOf('=');
        if (at > 0) repos[pair.slice(0, at)] = pair.slice(at + 1);
    }
    return repos;
}

function executable() {
    const candidates = [process.env.SUB0CANVAS_EXE, join(root, 'build', 'default', 'sub0canvas.exe'), join(root, 'build', 'default', 'sub0canvas')];
    return candidates.find((p) => p && existsSync(p));
}

function snapshot(url, res) {
    const repos = repositories();
    if (url.pathname === '/snapshot/repos') { res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' }).end(JSON.stringify(Object.keys(repos))); return; }
    const q = url.searchParams;
    const repo = repos[q.get('repo') ?? 'self'];
    const paths = (q.get('paths') ?? '').split(',').map((p) => p.trim()).filter(Boolean);
    const exe = executable();
    const bad = !repo ? `unknown repo; known: ${Object.keys(repos).join(', ')}` : !paths.length ? 'paths= is required: a snapshot is scoped, never the whole repository'
        : paths.some((p) => p.includes('..')) ? 'a path may not climb out of the repository' : !exe ? 'sub0canvas is not built: cmake --build --preset default' : '';
    if (bad) { res.writeHead(400, { 'content-type': 'text/plain; charset=utf-8' }).end(bad); return; }
    const args = ['snapshot', '--repo', repo, '--rev', q.get('rev') || 'HEAD', '--history', String(Number(q.get('history')) || 8)];
    if (q.get('max-files')) args.push('--max-files', String(Number(q.get('max-files')) || 150));
    const child = spawn(exe, [...args, '--', ...paths], { windowsHide: true });   // no shell: arguments reach the executable as they are
    let started = false, err = '';
    child.stderr.on('data', (d) => { err += d; });
    child.stdout.on('data', (d) => {
        if (!started) { started = true; res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' }); }
        res.write(d);
    });
    child.on('close', (code) => {
        console.log(err.trim());
        if (!started) res.writeHead(code === 0 ? 200 : 500, { 'content-type': 'text/plain; charset=utf-8' }).end(err || `sub0canvas exited ${code}`);
        else res.end();
    });
    res.on('close', () => { if (child.exitCode === null) child.kill(); });
}

createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    // A page on another site can make the browser send requests here; checking Host stops DNS rebinding from reading what this server answers.
    if (!/^(127\.0\.0\.1|localhost)(:\d+)?$/.test(req.headers.host ?? '')) { res.writeHead(403).end(); return; }
    if (url.pathname === '/snapshot' || url.pathname === '/snapshot/repos') { snapshot(url, res); return; }
    const rel = normalize(decodeURIComponent(url.pathname)).replace(/^[\\/]+/, '') || 'index.html';
    if (rel.startsWith('..') || /^\.git([\\/]|$)/.test(rel)) { res.writeHead(403).end(); return; }
    try {
        const body = await readFile(join(root, rel));
        res.writeHead(200, { 'content-type': types[extname(rel)] ?? 'text/plain; charset=utf-8', 'cache-control': 'no-store' }).end(body);
    } catch {
        res.writeHead(404).end('not found');
    }
}).listen(port, '127.0.0.1', function () { console.log(`sub0canvas on http://127.0.0.1:${this.address().port}/`); });
