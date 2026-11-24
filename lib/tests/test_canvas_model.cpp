// The canvas against an independent model (docs/REQUIREMENTS.md, R-CAN-1 to R-CAN-5): random sequences of transactions, stale transactions, malformed
// ones and reverts, checked after every step. The model restates the canvas's rules from the requirements, not from its code, so the canvas may be
// rewritten freely as long as it still agrees. First written as Sub0Squad's `canvasModelCheck` (test_blocks, block 5); it uses only the library's API,
// so it lives with the library.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/canvas.hpp"
#include "bench/rng.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace sub0canvas;

namespace {

std::vector<std::string> splitLines(const std::string& t)
{
    std::vector<std::string> v;
    std::istringstream in(t);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}
std::string joinLines(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& l : v) s += l + "\n";
    return s;
}

struct ModelOp
{
    CanvasOp::Kind kind;
    int from, to;
    std::vector<std::string> text;
};   // replaceLines [from,to], or insertAfter from

/// The independent model: operations in the numbering of the base, applied together from the bottom up. False when one is out of range or two touch
/// the same place (R-CAN-1: all or nothing; R-CAN-3: overlapping operations are refused).
bool modelApply(std::vector<std::string>& lines, const std::vector<ModelOp>& ops)
{
    const int n = static_cast<int>(lines.size());
    struct Span { int lo, hi; std::vector<std::string> text; };   // replaces the half-open 0-based range [lo, hi)
    std::vector<Span> spans;
    for (const auto& op : ops) {
        if (op.kind == CanvasOp::Kind::insertAfter) {
            if (op.from < 0 || op.from > n) return false;
            spans.push_back({op.from, op.from, op.text});
        } else {
            if (op.from < 1 || op.to < op.from || op.to > n) return false;
            spans.push_back({op.from - 1, op.to, op.text});
        }
    }
    std::ranges::stable_sort(spans, [](const Span& a, const Span& b) { return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi); });
    for (std::size_t i = 1; i < spans.size(); ++i) {
        const bool prevInsert = spans[i - 1].lo == spans[i - 1].hi;
        const int prevEnd = prevInsert ? spans[i - 1].lo : spans[i - 1].hi;   // 1-based last line touched (an insertion "touches" up to lo)
        if (spans[i].lo + 1 <= prevEnd) return false;
    }
    for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
        lines.erase(lines.begin() + it->lo, lines.begin() + it->hi);
        lines.insert(lines.begin() + it->lo, it->text.begin(), it->text.end());
    }
    return true;
}

Transaction toTransaction(const std::string& path, int base, const std::vector<ModelOp>& ops)
{
    Transaction tx{path, base, "model", {}};
    for (const auto& op : ops) tx.ops.push_back({op.kind, op.from, op.kind == CanvasOp::Kind::insertAfter ? 0 : op.to, {}, joinLines(op.text)});
    return tx;
}

std::string toReply(const std::string& path, const std::vector<ModelOp>& ops)
{
    std::string r;
    for (const auto& op : ops) {
        r += "=== EDIT: " + path + (op.kind == CanvasOp::Kind::insertAfter ? " AFTER " + std::to_string(op.from) : " LINES " + std::to_string(op.from) + "-" + std::to_string(op.to)) + " ===\n";
        r += joinLines(op.text) + "=== END EDIT ===\n";
    }
    return r;
}

struct ModelCheck { int cases = 0, steps = 0, failures = 0; std::string firstFailure; };

ModelCheck check(int cases, unsigned seed)
{
    ModelCheck out;
    sub0canvas::rng::SplitMix64 rng(seed);   // bench/rng.hpp: the same sequence on every standard library, one draw per statement
    const auto pick = [&](int lo, int hi) { return lo + static_cast<int>(rng.below(static_cast<std::size_t>(hi - lo + 1))); };
    int label = 0;
    const auto fresh = [&](int n) { std::vector<std::string> v; for (int i = 0; i < n; ++i) v.push_back("line " + std::to_string(++label)); return v; };
    const auto randomOps = [&](int count, int lineCount) {
        std::vector<ModelOp> ops;
        for (int i = 0; i < count; ++i) {
            if (pick(0, 3) == 0) {
                const int after = pick(0, lineCount);
                const int count = pick(1, 3);
                ops.push_back({CanvasOp::Kind::insertAfter, after, 0, fresh(count)});
            }
            else {
                const int a = pick(1, std::max(1, lineCount));
                const int span = pick(0, 3);
                const int b = std::min(lineCount, a + span);
                const int count = pick(0, 3);
                ops.push_back({CanvasOp::Kind::replaceLines, a, std::max(a, b), fresh(count)});
            }
        }
        return ops;
    };
    const auto fail = [&](int c, int step, const std::string& what) {
        ++out.failures;
        if (out.firstFailure.empty()) out.firstFailure = "case " + std::to_string(c) + " (seed " + std::to_string(seed) + "), step " + std::to_string(step) + ": " + what;
    };
    for (int c = 0; c < cases; ++c) {
        ++out.cases;
        const std::string path = "f.txt";
        Canvas canvas;
        std::vector<std::vector<std::string>> history = {fresh(pick(4, 30))};   // history[i] is the model's text at revision i + 1
        canvas.addFile(path, joinLines(history[0]));
        const int steps = pick(3, 9);
        bool bad = false;
        for (int s = 0; s < steps && !bad; ++s) {
            ++out.steps;
            const int current = static_cast<int>(history.size());
            const int kind = pick(0, 9);
            if (kind == 0 && current > 1) {   // R-CAN-5: restore an earlier revision as a new one
                const int to = pick(1, current - 1);
                const TxResult r = canvas.revertTo(path, to, "model");
                history.push_back(history[static_cast<std::size_t>(to) - 1]);
                if (!r.ok || r.revision != current + 1) { fail(c, s, "revert was refused"); bad = true; }
            } else if (kind == 1 && current > 1) {
                // R-CAN-4: a STALE transaction, made against the previous revision: rebased when it does not touch what changed since (every line it
                // wrote is present, in order), otherwise refused with nothing changed.
                const auto ops = randomOps(pick(1, 2), static_cast<int>(history[static_cast<std::size_t>(current) - 2].size()));
                const TxResult r = canvas.apply(toTransaction(path, current - 1, ops));
                if (r.ok) {
                    const auto now = splitLines(canvas.text(path));
                    std::vector<std::string> wrote;
                    auto sorted = ops;
                    // The order the text takes is by the 0-based start of each span, then its end (modelApply's order): `AFTER 22` starts at 22 but `LINES 22` at 21,
                    // so sorting by `from` alone left that tie to std::sort (a sequence from a new generator found it).
                    const auto spanOf = [](const ModelOp& o) {
                        return o.kind == CanvasOp::Kind::insertAfter ? std::pair{o.from, o.from} : std::pair{o.from - 1, o.to};
                    };
                    std::ranges::stable_sort(sorted, [&](const ModelOp& a, const ModelOp& b) { return spanOf(a) < spanOf(b); });
                    for (const auto& op : sorted) wrote.insert(wrote.end(), op.text.begin(), op.text.end());
                    std::size_t at = 0;
                    for (const auto& l : now) if (at < wrote.size() && l == wrote[at]) ++at;
                    if (at != wrote.size()) { fail(c, s, "a rebased transaction lost lines it wrote"); bad = true; }
                    history.push_back(now);
                } else if (canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) {
                    fail(c, s, "a refused stale transaction changed the file");
                    bad = true;
                }
            } else if (kind == 2) {   // R-CAN-1: out of range: refused whole even though its first operation is valid
                auto ops = randomOps(1, static_cast<int>(history.back().size()));
                ops.push_back({CanvasOp::Kind::replaceLines, static_cast<int>(history.back().size()) + 2, static_cast<int>(history.back().size()) + 3, fresh(1)});
                const TxResult r = canvas.apply(toTransaction(path, current, ops));
                if (r.ok || canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) { fail(c, s, "an out-of-range transaction was not refused whole"); bad = true; }
            } else {   // up to date, one to three operations; half go through the reply text (R-EDIT-1: the reply form means the same transaction)
                const auto ops = randomOps(pick(1, 3), static_cast<int>(history.back().size()));
                auto model = history.back();
                const bool valid = modelApply(model, ops);
                Transaction tx = toTransaction(path, current, ops);
                if (pick(0, 1) == 0) {
                    const CanvasReply parsed = parseCanvasReply(toReply(path, ops));
                    if (!parsed.error.empty() || parsed.transactions.size() != 1) { fail(c, s, "the reply form did not parse: " + parsed.error); bad = true; break; }
                    tx = parsed.transactions[0];
                    tx.baseRevision = current;
                }
                const TxResult r = canvas.apply(tx);
                if (r.ok != valid) { fail(c, s, std::string{"the canvas "} + (r.ok ? "accepted" : "refused (" + r.error + ")") + " what the model " + (valid ? "accepts" : "refuses")); bad = true; }
                else if (valid) { history.push_back(model); if (canvas.text(path) != joinLines(model)) { fail(c, s, "the text differs from the model"); bad = true; } }
                else if (canvas.text(path) != joinLines(history.back()) || canvas.revision(path) != current) { fail(c, s, "a refused transaction changed the file"); bad = true; }
            }
            if (!bad && (canvas.revision(path) != static_cast<int>(history.size()) || canvas.text(path) != joinLines(history.back()))) { fail(c, s, "revision count or text out of step with the model"); bad = true; }
        }
        for (std::size_t i = 0; i < history.size() && !bad; ++i)   // R-CAN-5: every revision is kept
            if (canvas.textAt(path, static_cast<int>(i) + 1) != joinLines(history[i])) { fail(c, steps, "revision " + std::to_string(i + 1) + " is not retrievable"); bad = true; }
    }
    return out;
}

}  // namespace

int main()
{
    for (const auto& [cases, seed] : {std::pair{1000, 20261112u}, std::pair{200, 7u}}) {
        const ModelCheck m = check(cases, seed);
        if (m.failures) std::fprintf(stderr, "canvas model: %s\n", m.firstFailure.c_str());
        CHECK_EQ(m.cases, cases);
        CHECK_EQ(m.failures, 0);
        CHECK(m.steps > 3 * cases);
    }
    {   // R-CAN-4 through applyReply (ApplyOptions::baseRevision): a reply written against an older revision is rebased where its lines do not overlap
        // what changed since, and refused naming who changed them where they do, exactly as the canvas rules for one transaction; in either mode.
        const std::string base = "a\nb\nc\nd\n";
        for (const bool partial : {false, true}) {
            Canvas canvas;
            canvas.addFile("f.txt", base);
            const CanvasReply first = parseCanvasReply("=== EDIT: f.txt LINES 1 ===\nA\n=== END EDIT ===\n");
            CHECK(applyReply(canvas, first, {{"f.txt"}, "alice", partial, 0}).has_value());   // revision 2 changes line 1 only
            // bob was shown revision 1 and edits line 4: no overlap, so it lands on revision 3, with line 4 still line 4.
            const CanvasReply other = parseCanvasReply("=== EDIT: f.txt LINES 4 ===\nD\n=== END EDIT ===\n");
            const auto rebased = applyReply(canvas, other, {{"f.txt"}, "bob", partial, 1});
            CHECK(rebased.has_value() && (!rebased || rebased->skipped.empty()));
            CHECK(canvas.text("f.txt") == "A\nb\nc\nD\n" && canvas.revision("f.txt") == 3);
            // bob edits line 1 from the same stale view: refused, and the canvas stays as it was.
            const CanvasReply clash = parseCanvasReply("=== EDIT: f.txt LINES 1 ===\nX\n=== END EDIT ===\n");
            const auto refused = applyReply(canvas, clash, {{"f.txt"}, "bob", partial, 1});
            const std::string why = !refused ? refused.error() : refused->skipped.empty() ? std::string{} : refused->skipped.front();
            CHECK(why.contains("changed by alice in revision 2"));
            CHECK(canvas.text("f.txt") == "A\nb\nc\nD\n" && canvas.revision("f.txt") == 3);
            // Without a base the same reply is made against the current text and simply applies; a base that does not exist is refused.
            const auto current = applyReply(canvas, clash, {{"f.txt"}, "bob", partial, 0});
            CHECK(current.has_value() && canvas.text("f.txt") == "X\nb\nc\nD\n");
            const auto missing = applyReply(canvas, clash, {{"f.txt"}, "bob", partial, 9});
            const std::string gone = !missing ? missing.error() : missing->skipped.empty() ? std::string{} : missing->skipped.front();
            CHECK(gone.contains("base revision 9 does not exist"));
        }
    }
    return finish();
}
