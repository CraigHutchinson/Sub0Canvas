// A minimal Chrome DevTools Protocol client over Node's built-in WebSocket (Node 22+): no dependency, no download. `Cdp` is one browser connection with
// flattened sessions; `launchBrowser` starts a headless Chromium-family browser and reads its DevTools URL from stderr.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, existsSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export class Cdp {
    constructor(ws) {
        this.ws = ws;
        this.next = 1;
        this.pending = new Map();
        ws.addEventListener('message', (ev) => {
            const m = JSON.parse(typeof ev.data === 'string' ? ev.data : Buffer.from(ev.data).toString('utf8'));
            if (m.id !== undefined) {
                const p = this.pending.get(m.id);
                if (!p) return;
                this.pending.delete(m.id);
                if (m.error) p.reject(new Error(`${p.method}: ${m.error.message}`)); else p.resolve(m.result);
            }
        });
        ws.addEventListener('close', () => { for (const p of this.pending.values()) p.reject(new Error('DevTools connection closed')); this.pending.clear(); });
    }

    static connect(url) {
        return new Promise((resolve, reject) => {
            const ws = new WebSocket(url);
            ws.addEventListener('open', () => resolve(new Cdp(ws)), { once: true });
            ws.addEventListener('error', () => reject(new Error(`cannot connect to ${url}`)), { once: true });
        });
    }

    send(method, params = {}, sessionId) {
        const id = this.next++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject, method });
            this.ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
        });
    }

    /** A new tab at `url`, attached; returns `{ targetId, sessionId, eval(expr) }`. `eval` awaits promises and returns the value (throws on an exception). */
    async openTab(url = 'about:blank', { width = 1440, height = 900 } = {}) {
        const { targetId } = await this.send('Target.createTarget', { url: 'about:blank' });
        const { sessionId } = await this.send('Target.attachToTarget', { targetId, flatten: true });
        await this.send('Page.enable', {}, sessionId);
        await this.send('Runtime.enable', {}, sessionId);
        await this.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }, sessionId);
        const tab = {
            targetId, sessionId,
            send: (m, p) => this.send(m, p, sessionId),
            eval: async (expression) => {
                const r = await this.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, sessionId);
                if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
                return r.result.value;
            },
            close: () => this.send('Target.closeTarget', { targetId }).catch(() => {}),
        };
        if (url !== 'about:blank') await tab.send('Page.navigate', { url });
        return tab;
    }

    close() { try { this.ws.close(); } catch { /* already closed */ } }
}

async function waitForEndpoint(profile, child) {
    child.stderr.on('data', () => {});   // drained, never read: the file is the source
    const file = join(profile, 'DevToolsActivePort');
    const end = Date.now() + 30000;
    while (Date.now() < end) {
        if (existsSync(file)) {
            const [port, path] = readFileSync(file, 'utf8').split(/\r?\n/);
            if (port && path) return `ws://127.0.0.1:${port}${path}`;
        }
        await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error('the browser wrote no DevToolsActivePort in 30 s');
}

/** Launch the browser headless with a throwaway profile; resolves `{ cdp, version, stop }`. */
export async function launchBrowser(exe, { width = 1440, height = 900 } = {}) {
    const profile = mkdtempSync(join(tmpdir(), 'sub0canvas-k0-'));
    const child = spawn(exe, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
        '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows', '--remote-debugging-port=0',
        `--user-data-dir=${profile}`, `--window-size=${width},${height}`, 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
    // Some builds (Edge) start the browser from a launcher process that exits at once, so the child's life says nothing; the browser writes its port and
    // browser endpoint to <profile>/DevToolsActivePort (Chrome also prints the URL on stderr).
    const url = await waitForEndpoint(profile, child);
    const cdp = await Cdp.connect(url);
    const version = await cdp.send('Browser.getVersion');
    const stop = () => {
        cdp.send('Browser.close').catch(() => {});
        setTimeout(() => cdp.close(), 500).unref();
        child.kill();
        setTimeout(() => { try { rmSync(profile, { recursive: true, force: true }); } catch { /* the browser still holds a file; the temp folder is cleaned by the OS */ } }, 1500).unref();
    };
    return { cdp, version: version.product, stop };
}
