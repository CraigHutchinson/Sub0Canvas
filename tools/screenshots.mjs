#!/usr/bin/env node
// Regenerates the README screenshots with a headless Chromium-family browser (Edge or Chrome) against tools/serve.mjs.
// Usage: node tools/screenshots.mjs [browserPath]     (BROWSER env also works)
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, rmSync, mkdtempSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const candidates = [process.argv[2], process.env.BROWSER,
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/microsoft-edge'].filter(Boolean);
const browser = candidates.find((p) => existsSync(p));
if (!browser) { console.error('no Chromium-family browser found; pass its path'); process.exit(1); }

const shots = {
    'prose-to-code': '#turn=1&centre=prose&item=prose%23prose-grammar&block=b4',
    'code-to-layers': '#turn=3&centre=code&block=b2',
    'block-history': '#turn=4&centre=prose&block=b6&item=prose%23prose-print&trace=prose%23prose-print',
    'one-to-many': '#turn=1&centre=issues&item=issues%23issue-peek&block=b4&a=2&fix=issues%23issue-peek',
    'merge-orphan': '#turn=3&centre=prose&block=b6&item=prose%23prose-print',
    'duplicates': '#turn=1&centre=review&unified=review%23unified%3Areview-div0-a%2Breview-div0-b&item=review%23unified%3Areview-div0-a%2Breview-div0-b',
};

const server = spawn(process.execPath, [join(root, 'tools', 'serve.mjs'), '0'], { env: { ...process.env, PORT: '0' }, stdio: ['ignore', 'pipe', 'inherit'] });
const port = await new Promise((resolve) => server.stdout.on('data', (d) => { const m = String(d).match(/:(\d+)\//); if (m) resolve(m[1]); }));
const out = join(root, 'docs', 'screenshots');
mkdirSync(out, { recursive: true });
const profile = mkdtempSync(join(tmpdir(), 'sub0canvas-shot-'));
try {
    for (const [name, hash] of Object.entries(shots)) {
        execFileSync(browser, ['--headless=new', '--disable-gpu', '--hide-scrollbars', `--user-data-dir=${profile}`, '--force-color-profile=srgb',
            '--window-size=1440,900', '--virtual-time-budget=2000', `--screenshot=${join(out, name + '.png')}`, `http://127.0.0.1:${port}/index.html${hash}`],
            { stdio: 'ignore' });
        console.log(`docs/screenshots/${name}.png`);
    }
} finally {
    server.kill();
    rmSync(profile, { recursive: true, force: true });
}
