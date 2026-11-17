#!/usr/bin/env node
// A zero-dependency static server for the viewer, for browsers or tools that will not run scripts from file://. Usage: node tools/serve.mjs [port]  (0 picks a free port)
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, normalize, extname, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const port = Number(process.env.PORT ?? process.argv[2] ?? 8765);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' };

createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    const rel = normalize(decodeURIComponent(url.pathname)).replace(/^[\\/]+/, '') || 'index.html';
    if (rel.startsWith('..')) { res.writeHead(403).end(); return; }
    try {
        const body = await readFile(join(root, rel));
        res.writeHead(200, { 'content-type': types[extname(rel)] ?? 'text/plain; charset=utf-8', 'cache-control': 'no-store' }).end(body);
    } catch {
        res.writeHead(404).end('not found');
    }
}).listen(port, '127.0.0.1', function () { console.log(`sub0canvas on http://127.0.0.1:${this.address().port}/`); });
