#!/usr/bin/env node
// Generates the demo attempt in the `layered-doc-1` interchange format that Sub0Squad's canvas writes per turn:
//   fixtures/demo/attempt-1/layers/turn-N.json, canvas.json, canvas/<flat path>.r<N>
// and fixtures/demo.js, the same files bundled for the viewer's built-in demo (a page opened from file:// cannot fetch).
//
// The story covers what the layered document has to survive: a SYMBOL edit (kept), a batch regex fix applied at every anchor of a 1-N issue, a rename,
// a signature change (resigned), a whole-file rewrite, a merge (isDigit inlined into parseNumber), an orphan (printResult deleted), a revival
// (printResult written again), and prose, review and decision layers anchored to code. Block lines are computed from the texts, never typed by hand.
//
// Usage: node tools/gen-fixtures.mjs [outDir]

import { mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outDir = process.argv[2] ?? join(root, 'fixtures', 'demo', 'attempt-1');

// ---- the source texts -------------------------------------------------------------------------------------------------------------------------------

const P = 'src/parser.cpp', M = 'src/main.cpp', T = 'tests/test_parser.cpp';

const parser1 = `#include "parser.hpp"
#include <cstdlib>

static bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

int Parser::number()
{
    int start = pos_;
    while (pos_ < src_.size() && isDigit(src_[pos_])) ++pos_;
    return atoi(src_.substr(start, pos_ - start).c_str());
}

int Parser::factor()
{
    if (peek() == '(') {
        ++pos_;
        int v = expr();
        ++pos_;
        return v;
    }
    return number();
}

int Parser::term()
{
    int v = factor();
    while (peek() == '*' || peek() == '/' || peek() == '%') {
        char op = src_[pos_++];
        int rhs = factor();
        if (op == '*') v *= rhs;
        else if (op == '/') v /= rhs;
        else v = v % rhs;
    }
    return v;
}

int Parser::expr()
{
    int v = term();
    while (peek() == '+' || peek() == '-') {
        char op = src_[pos_++];
        int rhs = term();
        v = op == '+' ? v + rhs : v - rhs;
    }
    return v;
}
`;

const main1 = `#include "parser.hpp"
#include <cstdio>
#include <iostream>

void printResult(int v)
{
    std::printf("= %d\\n", v);
}

int main(int argc, char** argv)
{
    if (argc < 2) return 1;
    Parser p(argv[1]);
    printResult(p.expr());
    return 0;
}
`;

const tests1 = `#include "parser.hpp"
#include <cassert>

void testPrecedence()
{
    Parser p("2+3*4");
    assert(p.expr() == 14);
    assert(p.peek() == '\\0');
}

void testModulo()
{
    Parser p("7%3");
    assert(p.expr() == 1);
    assert(p.peek() == '\\0');
}
`;

// Turn 1 -> 2: the batch fix of the 1-N issue (one regex at every anchor), a SYMBOL edit of term, a rename of number.
const batchFix = { kind: 'regex', pattern: '\\bpeek\\(\\)', replacement: 'peekChar()', flags: 'g',
                   summary: "rename every call of 'peek()' to 'peekChar()'" };
const applyFix = (text) => text.replace(new RegExp(batchFix.pattern, batchFix.flags), batchFix.replacement);
const parser2 = applyFix(parser1);
const tests2 = applyFix(tests1);
const parser3 = parser2.replace('        else v = v % rhs;\n', '        else if (rhs == 0) throw std::domain_error("modulo by zero");\n        else v = v % rhs;\n');
const parser4 = parser3.replace('int Parser::number()', 'int Parser::parseNumber()').replace('return number();', 'return parseNumber();');

// Turn 2 -> 3: a whole-file rewrite of parser.cpp (top-down order, isDigit inlined, factor returns long); printResult deleted from main.cpp.
const parser5 = `#include "parser.hpp"
#include <stdexcept>
#include <string>

int Parser::expr()
{
    int v = term();
    while (peekChar() == '+' || peekChar() == '-') {
        char op = src_[pos_++];
        int rhs = term();
        v = op == '+' ? v + rhs : v - rhs;
    }
    return v;
}

int Parser::term()
{
    int v = factor();
    while (peekChar() == '*' || peekChar() == '/' || peekChar() == '%') {
        char op = src_[pos_++];
        int rhs = factor();
        if (op == '*') v *= rhs;
        else if (op == '/') v /= rhs;
        else if (rhs == 0) throw std::domain_error("modulo by zero");
        else v = v % rhs;
    }
    return v;
}

long Parser::factor()
{
    if (peekChar() == '(') {
        ++pos_;
        int v = expr();
        ++pos_;
        return v;
    }
    return parseNumber();
}

int Parser::parseNumber()
{
    int start = pos_;
    while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') ++pos_;
    return std::stoi(src_.substr(start, pos_ - start));
}
`;

const main2 = `#include "parser.hpp"
#include <cstdio>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2) return 1;
    Parser p(argv[1]);
    std::cout << p.expr() << '\\n';
    return 0;
}
`;

// Turn 3 -> 4: printResult written again (revived); skipSpace added and called from factor.
const main3 = main1;
const parser6 = parser5
    .replace("long Parser::factor()\n{\n", "long Parser::factor()\n{\n    skipSpace();\n")
    + `
void Parser::skipSpace()
{
    while (pos_ < src_.size() && src_[pos_] == ' ') ++pos_;
}
`;

// ---- revisions and the canvas log -------------------------------------------------------------------------------------------------------------------

const revisions = { [P]: [], [M]: [], [T]: [] };
const log = [];

/** Append a revision and its log entry: the changed span in the numbering of the revision before it, as Canvas::logJson writes it. */
function commit(path, text, by) {
    const revs = revisions[path];
    const revision = revs.length + 1;
    if (revs.length === 0) {
        revs.push(text);
        log.push({ path, by, revision, base: 0, from: 1, to: 0, delta: lines(text).length });
        return;
    }
    const a = lines(revs[revs.length - 1]), b = lines(text);
    let head = 0;
    while (head < a.length && head < b.length && a[head] === b[head]) ++head;
    let tail = 0;
    while (tail < a.length - head && tail < b.length - head && a[a.length - 1 - tail] === b[b.length - 1 - tail]) ++tail;
    revs.push(text);
    log.push({ path, by, revision, base: revision - 1, from: head + 1, to: a.length - tail, delta: b.length - a.length });
}

function lines(text) {
    const l = text.split('\n');
    if (l.length && l[l.length - 1] === '') l.pop();
    return l;
}

// ---- blocks -------------------------------------------------------------------------------------------------------------------------------------------

/** The block whose signature line is `signature`: from that line to the line that closes its first brace. */
function block(id, path, text, signature) {
    const l = lines(text);
    const first = l.findIndex((s) => s.trim() === signature);
    if (first < 0) throw new Error(`no block '${signature}' in ${path}`);
    let depth = 0, opened = false, last = -1;
    for (let i = first; i < l.length && last < 0; ++i)
        for (const c of l[i]) {
            if (c === '{') { ++depth; opened = true; }
            else if (c === '}' && --depth === 0 && opened) { last = i; break; }
        }
    const name = signature.slice(0, signature.indexOf('(')).split(/\s+/).pop();
    return { id, path, name, signature: signature.replace(/\s+/g, ' '), first: first + 1, last: last + 1 };
}

/** The 1-based line of the `occurrence`th line inside `b` that contains `needle`. */
function lineIn(text, b, needle, occurrence = 1) {
    const l = lines(text);
    for (let i = b.first - 1, seen = 0; i < b.last; ++i)
        if (l[i].includes(needle) && ++seen === occurrence) return i + 1;
    throw new Error(`'${needle}' #${occurrence} not in ${b.name}`);
}

// ---- turns --------------------------------------------------------------------------------------------------------------------------------------------

const turns = [];

function turn(n, texts, specs, retired, events, layers) {
    const files = {};
    for (const [path, text] of Object.entries(texts))
        files[path] = { revision: revisions[path].lastIndexOf(text) + 1, text };
    const blocks = specs.map(([id, path, sig]) => block(id, path, texts[path], sig));
    const byId = Object.fromEntries(blocks.map((b) => [b.id, b]));
    const built = typeof layers === 'function' ? layers(byId, texts) : layers;
    turns.push({ schema: 'layered-doc-1', turn: n, files, blocks, retired, events, layers: built });
}

const anchor = (b, line, confidence = 'exact') => ({ blockId: b.id, path: b.path, line, confidence });
const at = (b) => anchor(b, b.first);

/** One anchor per occurrence of `needle` inside `b`, with its 1-based column (the proposed optional `column`: several sites on one line stay apart). */
function sites(text, b, needle) {
    const l = lines(text), out = [];
    for (let i = b.first - 1; i < b.last; ++i)
        for (let c = l[i].indexOf(needle); c >= 0; c = l[i].indexOf(needle, c + 1)) out.push({ ...anchor(b, i + 1), column: c + 1 });
    return out;
}

// The prose layer: one description per function, written once and carried; its anchors are block ids, so it follows every edit below.
function prose(B, extra = {}) {
    const items = [
        { id: 'prose-grammar', kind: 'overview', title: 'Recursive descent over three precedence levels',
          detail: 'expr handles + and -, term handles *, / and %, factor handles parentheses and numbers. Each level calls the next one down, so '
                + 'precedence falls out of the call structure rather than a table.',
          anchors: [at(B.b5), at(B.b4), at(B.b3)] },
        { id: 'prose-expr', kind: 'function', title: 'expr: sums and differences', detail: 'Folds term results left to right with + and -.', anchors: [at(B.b5)] },
        { id: 'prose-term', kind: 'function', title: 'term: products, quotients and remainders',
          detail: extra.term ?? 'Folds factor results left to right with *, / and %.', anchors: [at(B.b4)] },
        { id: 'prose-factor', kind: 'function', title: 'factor: a parenthesised expression or a number',
          detail: 'Recurses into expr after an opening parenthesis, otherwise reads a number.', anchors: [at(B.b3)] },
        { id: 'prose-number', kind: 'function', title: 'number: a run of decimal digits', detail: 'Consumes digits from the cursor and converts them.',
          anchors: [at(B.b2)] },
        { id: 'prose-isdigit', kind: 'function', title: 'isDigit: an ASCII digit test', detail: "True for '0' to '9'. Locale-independent, unlike std::isdigit.",
          anchors: [B.b1 ? at(B.b1) : { blockId: 'b1', path: P, line: 4, confidence: 'exact' }] },
        { id: 'prose-print', kind: 'function', title: 'printResult: the one output format', detail: 'Prints "= value". Every front end should go through it.',
          anchors: [B.b6 ? at(B.b6) : { blockId: 'b6', path: M, line: 5, confidence: 'exact' }] },
        { id: 'prose-main', kind: 'function', title: 'main: parse argv[1] and print it', detail: 'Exit code 1 without an argument.', anchors: [at(B.b7)] },
    ];
    if (B.b10) items.push({ id: 'prose-skip', kind: 'function', title: 'skipSpace: blanks between tokens', detail: 'Skips spaces only; tabs are an error.',
                            anchors: [at(B.b10)] });
    return items.map((it, i) => ({ number: i + 1, ...it }));
}

const number = (items) => items.map((it, i) => ({ number: i + 1, ...it }));

// Turn 1: everything is new. The gate reports 'peek' undeclared at eight call sites in two files: one issue, eight anchors, one regex fix.
turn(1, { [P]: (commit(P, parser1, 'coder'), parser1), [M]: (commit(M, main1, 'coder'), main1), [T]: (commit(T, tests1, 'tester'), tests1) },
    [['b1', P, 'static bool isDigit(char c)'], ['b2', P, 'int Parser::number()'], ['b3', P, 'int Parser::factor()'], ['b4', P, 'int Parser::term()'],
     ['b5', P, 'int Parser::expr()'], ['b6', M, 'void printResult(int v)'], ['b7', M, 'int main(int argc, char** argv)'],
     ['b8', T, 'void testPrecedence()'], ['b9', T, 'void testModulo()']],
    [],
    [['b1', P, 'isDigit'], ['b2', P, 'Parser::number'], ['b3', P, 'Parser::factor'], ['b4', P, 'Parser::term'], ['b5', P, 'Parser::expr'],
     ['b6', M, 'printResult'], ['b7', M, 'main'], ['b8', T, 'testPrecedence'], ['b9', T, 'testModulo']]
        .map(([id, path, name]) => ({ carry: 'added', id, path, name, detail: '' })),
    (B, X) => ({
        issues: number([
            { id: 'issue-peek', kind: 'error', title: "use of undeclared identifier 'peek'; did you mean 'peekChar'?",
              detail: "src/parser.cpp:18:9: error: use of undeclared identifier 'peek'; did you mean 'peekChar'?\n"
                    + "  (and 7 more sites in src/parser.cpp and tests/test_parser.cpp)",
              fix: batchFix,
              anchors: [
                  ...sites(X[P], B.b3, 'peek()'), ...sites(X[P], B.b4, 'peek()'), ...sites(X[P], B.b5, 'peek()'),
                  ...sites(X[T], B.b8, 'peek()'), ...sites(X[T], B.b9, 'peek()'),
              ] },
            { id: 'issue-modulo', kind: 'test', title: 'FAIL 2 - testModulo: 7 % 0 terminates the process',
              detail: 'FAIL 2 - testModulo: SIGFPE on "7%0"; the expression under test reaches the % branch of the term loop.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v % rhs'), 'suspect')] },
        ]),
        prose: prose(B),
        review: number([
            { id: 'review-div0-a', kind: 'note', title: 'term(): modulo by zero is not handled', detail: 'reviewer-a: `v % rhs` with rhs == 0 is UB.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v % rhs'))] },
            { id: 'review-div0-b', kind: 'note', title: 'Modulo by zero unchecked in term', detail: 'reviewer-b: guard rhs before %, and decide what / by zero does.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v /= rhs')), anchor(B.b4, lineIn(X[P], B.b4, 'v % rhs'))] },
            { id: 'review-atoi', kind: 'note', title: 'atoi swallows overflow', detail: 'Prefer std::stoi or from_chars so an overflowing literal is an error.',
              anchors: [anchor(B.b2, lineIn(X[P], B.b2, 'atoi'))] },
        ]),
    }));

// Turn 2: the batch fix, the SYMBOL edit of term, the rename of number. Ids hold: term is kept by signature, number is carried by body similarity.
commit(P, parser2, 'batch-fix:issue-peek'); commit(T, tests2, 'batch-fix:issue-peek');
commit(P, parser3, 'coder (SYMBOL Parser::term)');
commit(P, parser4, 'coder (SYMBOL Parser::number)');
turn(2, { [P]: parser4, [M]: main1, [T]: tests2 },
    [['b1', P, 'static bool isDigit(char c)'], ['b2', P, 'int Parser::parseNumber()'], ['b3', P, 'int Parser::factor()'], ['b4', P, 'int Parser::term()'],
     ['b5', P, 'int Parser::expr()'], ['b6', M, 'void printResult(int v)'], ['b7', M, 'int main(int argc, char** argv)'],
     ['b8', T, 'void testPrecedence()'], ['b9', T, 'void testModulo()']],
    [],
    [{ carry: 'kept', id: 'b3', path: P, name: 'Parser::factor', detail: '' },
     { carry: 'kept', id: 'b4', path: P, name: 'Parser::term', detail: '' },
     { carry: 'renamed', id: 'b2', path: P, name: 'Parser::parseNumber', detail: 'Parser::number' },
     { carry: 'kept', id: 'b5', path: P, name: 'Parser::expr', detail: '' },
     { carry: 'kept', id: 'b8', path: T, name: 'testPrecedence', detail: '' },
     { carry: 'kept', id: 'b9', path: T, name: 'testModulo', detail: '' }],
    (B, X) => ({
        issues: number([
            { id: 'issue-cstdlib', kind: 'warning', title: "'<cstdlib>' is included but only atoi uses it",
              detail: 'src/parser.cpp:2:1: warning: include could be narrowed [misc-include-cleaner]',
              anchors: [{ blockId: '', path: P, line: 2, confidence: 'exact' }] },
        ]),
        prose: prose(B, { term: 'Folds factor results left to right with *, / and %; a zero right-hand side of % throws std::domain_error.' }),
        review: number([
            { id: 'review-div0-a', kind: 'note', title: 'term(): modulo by zero is not handled', detail: 'reviewer-a: resolved by the guard, keep / consistent.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'rhs == 0'))] },
            { id: 'review-div0-b', kind: 'note', title: 'Modulo by zero unchecked in term', detail: 'reviewer-b: guard rhs before %, and decide what / by zero does.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v /= rhs')), anchor(B.b4, lineIn(X[P], B.b4, 'rhs == 0'))] },
            { id: 'review-atoi', kind: 'note', title: 'atoi swallows overflow', detail: 'Prefer std::stoi or from_chars so an overflowing literal is an error.',
              anchors: [anchor(B.b2, lineIn(X[P], B.b2, 'atoi'))] },
        ]),
    }));

// Turn 3: a whole-file rewrite. expr, term, parseNumber keep their ids by signature; factor by name (resigned); isDigit is merged into parseNumber.
// main.cpp loses printResult, whose body lives nowhere: retired without a successor, so its prose is an orphan.
commit(P, parser5, 'coder (whole-file rewrite)');
commit(M, main2, 'coder (SYMBOL main)');
turn(3, { [P]: parser5, [M]: main2, [T]: tests2 },
    [['b5', P, 'int Parser::expr()'], ['b4', P, 'int Parser::term()'], ['b3', P, 'long Parser::factor()'], ['b2', P, 'int Parser::parseNumber()'],
     ['b7', M, 'int main(int argc, char** argv)'], ['b8', T, 'void testPrecedence()'], ['b9', T, 'void testModulo()']],
    [{ id: 'b1', path: P, name: 'isDigit', mergedInto: 'b2' }, { id: 'b6', path: M, name: 'printResult', mergedInto: '' }],
    [{ carry: 'kept', id: 'b5', path: P, name: 'Parser::expr', detail: '' },
     { carry: 'kept', id: 'b4', path: P, name: 'Parser::term', detail: '' },
     { carry: 'resigned', id: 'b3', path: P, name: 'Parser::factor', detail: 'int Parser::factor()' },
     { carry: 'kept', id: 'b2', path: P, name: 'Parser::parseNumber', detail: '' },
     { carry: 'merged', id: 'b1', path: P, name: 'isDigit', detail: 'b2' },
     { carry: 'kept', id: 'b7', path: M, name: 'main', detail: '' },
     { carry: 'retired', id: 'b6', path: M, name: 'printResult', detail: '' }],
    (B, X) => ({
        issues: number([
            { id: 'issue-narrow', kind: 'warning', title: "implicit conversion loses integer precision: 'long' to 'int'",
              detail: 'src/parser.cpp: warning: implicit conversion loses integer precision [-Wshorten-64-to-32] (2 sites)',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'int v = factor()')), anchor(B.b4, lineIn(X[P], B.b4, 'int rhs = factor()'))] },
        ]),
        prose: prose(B),
        review: number([
            { id: 'review-div0-b', kind: 'note', title: 'Modulo by zero unchecked in term', detail: 'reviewer-b: guard rhs before %, and decide what / by zero does.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v /= rhs'))] },
        ]),
        decisions: number([
            { id: 'decision-inline', kind: 'decision', title: 'Inline isDigit into parseNumber', detail: 'One call site; the helper hid the range check.',
              anchors: [anchor(B.b2, lineIn(X[P], B.b2, "'0'"))] },
            { id: 'decision-output', kind: 'decision', title: 'Print the bare value', detail: 'Drop the "= " prefix so the output pipes into other tools.',
              anchors: [anchor(B.b7, lineIn(X[M], B.b7, 'std::cout'))] },
        ]),
    }));

// Turn 4: printResult comes back with its old signature and revives b6, so its prose resolves again; skipSpace is new. With -Wextra the gate reports
// a signed/unsigned comparison at three sites in two blocks: one issue, three anchors, one regex fix.
commit(M, main3, 'coder (revert decision-output)');
commit(P, parser6, 'coder (SYMBOL Parser::skipSpace)');
const signFix = { kind: 'regex', pattern: 'pos_ < src_\\.size\\(\\)', replacement: 'pos_ < static_cast<int>(src_.size())', flags: 'g',
                  summary: 'compare pos_ with a signed size at every site' };
turn(4, { [P]: parser6, [M]: main3, [T]: tests2 },
    [['b5', P, 'int Parser::expr()'], ['b4', P, 'int Parser::term()'], ['b3', P, 'long Parser::factor()'], ['b2', P, 'int Parser::parseNumber()'],
     ['b10', P, 'void Parser::skipSpace()'], ['b6', M, 'void printResult(int v)'], ['b7', M, 'int main(int argc, char** argv)'],
     ['b8', T, 'void testPrecedence()'], ['b9', T, 'void testModulo()']],
    [{ id: 'b1', path: P, name: 'isDigit', mergedInto: 'b2' }],
    [{ carry: 'kept', id: 'b3', path: P, name: 'Parser::factor', detail: '' },
     { carry: 'added', id: 'b10', path: P, name: 'Parser::skipSpace', detail: '' },
     { carry: 'revived', id: 'b6', path: M, name: 'printResult', detail: '' },
     { carry: 'kept', id: 'b7', path: M, name: 'main', detail: '' }],
    (B, X) => ({
        issues: number([
            { id: 'issue-sign', kind: 'warning', title: "comparison of integers of different signs: 'int' and 'size_t'",
              detail: 'warning: comparison of integers of different signs [-Wsign-compare] (3 sites)',
              fix: signFix,
              anchors: [anchor(B.b2, lineIn(X[P], B.b2, 'src_.size()')), anchor(B.b10, lineIn(X[P], B.b10, 'src_.size()')),
                        anchor(B.b4, lineIn(X[P], B.b4, 'int v = factor()'), 'suspect')] },
            { id: 'issue-narrow', kind: 'warning', title: "implicit conversion loses integer precision: 'long' to 'int'",
              detail: 'src/parser.cpp: warning: implicit conversion loses integer precision [-Wshorten-64-to-32] (3 sites)',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'int v = factor()')), anchor(B.b4, lineIn(X[P], B.b4, 'int rhs = factor()')),
                        anchor(B.b3, lineIn(X[P], B.b3, 'int v = expr()'))] },
            { id: 'issue-narrow-expr', kind: 'warning', title: "Implicit conversion loses integer precision: long to int",
              detail: 'clang-tidy: bugprone-narrowing-conversions in Parser::expr (reported by a second tool)',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'int rhs = factor()'))] },
        ]),
        prose: prose(B),
        review: number([
            { id: 'review-div0-b', kind: 'note', title: 'Modulo by zero unchecked in term', detail: 'reviewer-b: guard rhs before %, and decide what / by zero does.',
              anchors: [anchor(B.b4, lineIn(X[P], B.b4, 'v /= rhs'))] },
            { id: 'review-tabs', kind: 'note', title: 'skipSpace ignores tabs', detail: "Use std::isspace, or document that '\\t' is an error.",
              anchors: [anchor(B.b10, lineIn(X[P], B.b10, "' '"))] },
        ]),
        decisions: number([
            { id: 'decision-inline', kind: 'decision', title: 'Inline isDigit into parseNumber', detail: 'One call site; the helper hid the range check.',
              anchors: [anchor(B.b2, lineIn(X[P], B.b2, "'0'"))] },
            { id: 'decision-output', kind: 'decision', title: 'Keep "= value" via printResult', detail: 'Reverted: the portal parses the "= " prefix.',
              anchors: [at(B.b6), anchor(B.b7, lineIn(X[M], B.b7, 'printResult'))] },
        ]),
    }));

// ---- write --------------------------------------------------------------------------------------------------------------------------------------------

rmSync(outDir, { recursive: true, force: true });
const written = [];
function put(rel, text) {
    const full = join(outDir, rel);
    mkdirSync(dirname(full), { recursive: true });
    writeFileSync(full, text);
    written.push({ path: `attempt-1/${rel}`, text });
}
for (const t of turns) put(`layers/turn-${t.turn}.json`, JSON.stringify(t, null, 1) + '\n');
put('canvas.json', JSON.stringify({ files: Object.fromEntries(Object.entries(revisions).map(([p, r]) => [p, r.length])), log }, null, 1) + '\n');
for (const [path, revs] of Object.entries(revisions))
    revs.forEach((text, i) => put(`canvas/${path.replaceAll('/', '_')}.r${i + 1}`, text));

const bundle = join(root, 'fixtures', 'demo.js');
writeFileSync(bundle, '// Generated by tools/gen-fixtures.mjs: the demo attempt, bundled so the viewer can open it from file://. Do not edit.\n'
                    + `window.SUB0CANVAS_DEMO = ${JSON.stringify(written)};\n`);
console.log(`wrote ${written.length} files to ${outDir} and ${bundle}`);
