// Content ids of the `layered-doc-1` format: a text's id is its git blob id (the same id lib/src/content_id.cpp computes), so a producer, the generators
// and the tests agree on it. CommonJS, like tools/mirror, because tests `require` it. node:crypto only computes bytes: no I/O.
const crypto = require('node:crypto');

/** The git object id of `bytes` (a Buffer) stored as `type` ('blob', 'tree' or 'commit'). */
function gitObjectId(type, bytes) {
    return crypto.createHash('sha1').update(`${type} ${bytes.length}\0`).update(bytes).digest('hex');
}

/** The id of a text: its git blob id over the UTF-8 bytes. */
function blobId(text) {
    return gitObjectId('blob', Buffer.from(text, 'utf8'));
}

module.exports = { gitObjectId, blobId };
