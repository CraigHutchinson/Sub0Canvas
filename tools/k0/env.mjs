// K0's view of the machine: where the browser is, whether the machine is on mains power and quiet, and the host alias the record is named by.
// The probes are separate from the decisions (`decide`) so the rules are unit-tested without a machine (test/k0.test.js).
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { cpus, hostname, platform, release, arch } from 'node:os';

/** The browser candidates, in the order tools/screenshots.mjs tries them (the pattern this follows): an argument, BROWSER, then the usual places. */
export function browserCandidates(arg, env = process.env) {
    return [arg, env.BROWSER, 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
        'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/usr/bin/google-chrome',
        '/usr/bin/chromium', '/usr/bin/microsoft-edge'].filter(Boolean);
}

export const findBrowser = (arg, env = process.env, exists = existsSync) => browserCandidates(arg, env).find((p) => exists(p)) ?? null;

/** The baseline name: SUB0PERF_HOST, or the pre-rename SUB0MATH_PERF_HOST (this repository is public, so no real host names are committed), else the lower-case host name. */
export const hostAlias = (env = process.env, host = hostname()) => (env.SUB0PERF_HOST || host).toLowerCase();

/**
 * Mains or battery from what a probe read. `battery` is `{present, discharging}`; a machine with no battery is on mains. Windows BatteryStatus 1 is
 * "discharging" (and 4, 5 low or critical, which are also on battery); 2 and 6-9 are on mains.
 */
export function decidePower(probe) {
    if (!probe || probe.unknown) return { state: 'unknown', onBattery: false };
    if (!probe.present) return { state: 'ac', onBattery: false, battery: false };
    return { state: probe.discharging ? 'battery' : 'ac', onBattery: !!probe.discharging, battery: true };
}

export function windowsBatteryProbe(json) {
    const list = [].concat(json ? JSON.parse(json) : []);
    if (!list.length) return { present: false };
    return { present: true, discharging: list.some((b) => [1, 4, 5].includes(b.BatteryStatus)) };
}

export function readPower() {
    try {
        if (platform() === 'win32') {
            const out = execFileSync('powershell', ['-NoProfile', '-Command', 'Get-CimInstance Win32_Battery | Select-Object BatteryStatus | ConvertTo-Json'],
                { encoding: 'utf8', timeout: 20000, windowsHide: true }).trim();
            return decidePower(windowsBatteryProbe(out));
        }
        if (platform() === 'linux') {
            const base = '/sys/class/power_supply';
            const batteries = readdirSync(base).filter((n) => /^BAT/i.test(n));
            if (!batteries.length) return decidePower({ present: false });
            const status = readFileSync(`${base}/${batteries[0]}/status`, 'utf8').trim();
            return decidePower({ present: true, discharging: status === 'Discharging' });
        }
        if (platform() === 'darwin') {
            const out = execFileSync('pmset', ['-g', 'batt'], { encoding: 'utf8' });
            return decidePower({ present: /InternalBattery/.test(out), discharging: /discharging/.test(out) });
        }
    } catch { /* fall through */ }
    return decidePower({ unknown: true });
}

/** CPU busy fraction between two `os.cpus()` snapshots. */
export function busyFraction(a, b) {
    let idle = 0, total = 0;
    a.forEach((c, i) => {
        const x = c.times, y = b[i].times;
        const dIdle = y.idle - x.idle;
        const dTotal = Object.keys(y).reduce((s, k) => s + (y[k] - x[k]), 0);
        idle += dIdle; total += dTotal;
    });
    return total > 0 ? 1 - idle / total : 0;
}

export async function sampleBusy(ms = 1500) {
    const a = cpus();
    await new Promise((r) => setTimeout(r, ms));
    return busyFraction(a, cpus());
}

/** Whether to measure: `{ ok, reason }`. A busy machine or a battery skips (the family rule); `force` measures anyway and the record says so. */
export function decide({ power, busy, maxBusy = 0.2, force = false }) {
    if (force) return { ok: true, forced: true, reason: 'forced' };
    if (power.onBattery) return { ok: false, reason: 'on battery' };
    if (busy > maxBusy) return { ok: false, reason: `machine not quiet (cpu ${Math.round(busy * 100)}% busy, limit ${Math.round(maxBusy * 100)}%)` };
    return { ok: true, reason: 'ok' };
}

export function machineInfo() {
    const c = cpus();
    return { system: `${platform()} ${release()}`, arch: arch(), cpu: c[0]?.model?.trim(), logicalCpus: c.length };
}
