// Shared by the node tests (CommonJS; the name does not match *.test.js, so the runner does not load it as a test).
const fs = require('node:fs');
const path = require('node:path');

/** Every file under `dir`, recursively, as { path, text } with `path` relative to `dir` ('/'-separated, behind `prefix` when given), in directory order. */
function readEntries(dir, prefix = '') {
    const out = [];
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = prefix ? `${prefix}/${e.name}` : e.name;
        if (e.isDirectory()) out.push(...readEntries(path.join(dir, e.name), rel));
        else out.push({ path: rel, text: fs.readFileSync(path.join(dir, e.name), 'utf8') });
    }
    return out;
}

/** The same walk as an object path -> text. */
function readTree(dir) {
    return Object.fromEntries(readEntries(dir).map((e) => [e.path, e.text]));
}

module.exports = { readEntries, readTree };
