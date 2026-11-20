// Recorded replies (docs/REQUIREMENTS.md R-EDIT-7, R-EDIT-8, R-CAN-1): every reply shape seen on real runs, with the files before and the outcome that
// must follow (fixtures/replies, format in its README), applied the way an agent applies a reply: continuations joined (turn control), the reply parsed
// and applied all or nothing. Moved from Sub0Squad's test_blocks (block 1) with `applyReply` itself, 2026-11-18; the runner is restated here from the
// case format, and the cases are unchanged. argv[1] is the fixture directory.
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/edits.hpp"
#include "sub0canvas/turn_control.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sub0canvas;

namespace {

struct ReplyCase
{
    std::string name;
    std::map<std::string, std::string> base, want;
    std::vector<std::string> replies;
    std::string expectError;          // non-empty: refused with a message containing this, and nothing changes
    int expectReads = -1;             // >= 0: only read requests, this many
    bool classic = false;             // the classic applier (`applyEdits`) must agree
    std::vector<std::string> allowed; // default: the paths of base and want
};

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    std::string t = s.str();
    std::erase(t, '\r');   // a checkout with CRLF line endings reads as the case was written
    return t;
}

std::vector<std::string> splitLines(const std::string& t)
{
    std::vector<std::string> v;
    std::istringstream in(t);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}

bool parseReplyCase(const std::string& text, ReplyCase& c, std::string& error)
{
    c = {};
    std::string kind, arg, body;
    bool inSection = false;
    const auto close = [&] {
        if (!inSection) return;
        if (kind == "base") c.base[arg] = body;
        else if (kind == "want") c.want[arg] = body;
        else if (kind == "reply") {
            if (!body.empty()) body.pop_back();
            if (arg == "cut" && !body.empty() && body.back() == '\\') body.pop_back();
            c.replies.push_back(body);
        }
        body.clear();
    };
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("--- ", 0) == 0) {
            close();
            std::istringstream h(line.substr(4));
            kind.clear();
            arg.clear();
            h >> kind >> arg;
            if (kind != "base" && kind != "want" && kind != "reply") { error = "unknown section '" + kind + "'"; return false; }
            if (kind != "reply" && arg.empty()) { error = "a " + kind + " section names no path"; return false; }
            inSection = true;
            continue;
        }
        if (!inSection) {
            if (line.rfind("# ", 0) != 0) continue;
            const auto colon = line.find(": ");
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(2, colon - 2), value = line.substr(colon + 2);
            if (key == "name") c.name = value;
            else if (key == "expect-error") c.expectError = value;
            else if (key == "expect-reads") c.expectReads = std::atoi(value.c_str());
            else if (key == "classic") c.classic = value == "yes";
            else if (key == "allowed") { std::istringstream a(value); for (std::string p; a >> p;) c.allowed.push_back(p); }
            continue;
        }
        body += line + "\n";
    }
    close();
    if (c.replies.empty()) { error = "the case has no reply"; return false; }
    if (c.expectError.empty() && c.expectReads < 0 && c.want.empty()) { error = "the case states no outcome (want, expect-error or expect-reads)"; return false; }
    return true;
}

/// "" when the case's outcome follows, else what went wrong.
std::string runReplyCase(const ReplyCase& c)
{
    std::set<std::string> allowed(c.allowed.begin(), c.allowed.end());
    if (allowed.empty()) {
        for (const auto& [p, t] : c.base) allowed.insert(p);
        for (const auto& [p, t] : c.want) allowed.insert(p);
    }
    std::string text;
    for (std::size_t i = 0; i < c.replies.size(); ++i) text = i == 0 ? c.replies[i] : joinContinuation(text, c.replies[i]);
    Canvas canvas;
    for (const auto& [p, t] : c.base) canvas.addFile(p, t);
    const CanvasReply parsed = parseCanvasReply(text);
    if (c.expectReads >= 0) {
        if (parsed.hasEdits()) return "expected only read requests, but the reply holds an edit";
        if (static_cast<int>(parsed.reads.size()) != c.expectReads) return "expected " + std::to_string(c.expectReads) + " read request(s), found " + std::to_string(parsed.reads.size());
        return {};
    }
    const auto before = canvas.files();
    std::string refusal;
    if (parsed.hasEdits() || !parsed.error.empty()) {
        if (auto r = applyReply(canvas, parsed, {allowed, "case", false}); !r) refusal = r.error();
    } else {
        refusal = "the reply holds no edit";
    }
    if (!c.expectError.empty()) {
        if (refusal.empty()) return "expected a refusal containing '" + c.expectError + "', but the reply applied";
        if (refusal.find(c.expectError) == std::string::npos) return "refused with '" + refusal + "', expected a message containing '" + c.expectError + "'";
        if (canvas.files() != before) return "refused, but the canvas changed";   // R-CAN-1: now checked on the canvas itself, not on a scratch copy
        return {};
    }
    if (!refusal.empty()) return "refused: " + refusal;
    for (const auto& [p, want] : c.want) {
        if (!canvas.has(p)) return "'" + p + "' was not written";
        if (canvas.text(p) != want) {
            const auto a = splitLines(canvas.text(p)), b = splitLines(want);
            std::size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            return "'" + p + "' differs from the intended text at line " + std::to_string(i + 1) + ": got '" + (i < a.size() ? a[i] : std::string{"<end>"}) + "', want '" +
                   (i < b.size() ? b[i] : std::string{"<end>"}) + "'";
        }
    }
    if (c.classic) {
        std::string err;
        const auto classic = applyEdits(text, c.base, allowed, err);
        if (!classic) return "the classic applier refused it: " + err;
        for (const auto& [p, want] : c.want)
            if (classic->count(p) == 0 || classic->at(p) != want) return "the classic applier gives a different '" + p + "'";
    }
    return {};
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_replies <fixtures/replies>\n"); return 2; }
    {   // R-EDIT-7: every recorded case gives its intended result.
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(argv[1]))
            if (e.path().extension() == ".case") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        CHECK_GE(files.size(), static_cast<std::size_t>(30));   // every reply form and every refusal has a case
        for (const auto& f : files) {
            ReplyCase c;
            std::string error;
            if (!parseReplyCase(slurp(f), c, error)) { std::fprintf(stderr, "%s: unreadable case: %s\n", f.filename().string().c_str(), error.c_str()); CHECK(false); continue; }
            if (const std::string bad = runReplyCase(c); !bad.empty()) { std::fprintf(stderr, "%s: %s\n", f.filename().string().c_str(), bad.c_str()); CHECK(false); }
        }
        // The runner itself can fail: a case without an outcome is refused, and a wrong intended text is reported with its line.
        ReplyCase c;
        std::string err;
        CHECK(!parseReplyCase("# name: x\n--- reply\nhello\n", c, err) && err.find("states no outcome") != std::string::npos);
        CHECK(parseReplyCase("# name: x\n--- base a.py\nx = 1\n--- reply\n=== EDIT: a.py LINES 1 ===\nx = 2\n=== END EDIT ===\n--- want a.py\nx = 3\n", c, err));
        CHECK(runReplyCase(c).find("differs from the intended text at line 1") != std::string::npos);
    }
    {   // R-CAN-1 for a whole reply: a file written whole, then an edit that fails, leaves the canvas exactly as it was (no revision, no new file).
        Canvas cv;
        cv.addFile("a.py", "x = 1\n");
        const auto before = cv.files();
        const CanvasReply r = parseCanvasReply("=== FILE: b.py ===\ny = 2\n=== END FILE ===\n=== EDIT: a.py LINES 9 ===\nx = 3\n=== END EDIT ===\n");
        const auto out = applyReply(cv, r, {{"a.py", "b.py"}, "w", false});
        CHECK(!out && out.error().find("a.py") != std::string::npos);
        CHECK(cv.files() == before && cv.revision("a.py") == 1 && !cv.has("b.py") && cv.log().empty());
        // The same reply applied partially: the whole file lands, the bad edit is reported.
        const auto part = applyReply(cv, r, {{"a.py", "b.py"}, "w", true});
        CHECK(part && part->skipped.size() == 1 && part->skipped[0].find("a.py") != std::string::npos && cv.text("b.py") == "y = 2\n" && cv.text("a.py") == "x = 1\n");
    }
    {   // R-EDIT-8: partial mode keeps the good operations beside a bad one, each against the revision the author was shown.
        Canvas cv;
        cv.addFile("f.txt", "a\nb\nc\nd\n");
        const CanvasReply r = parseCanvasReply("=== EDIT: f.txt LINES 1 ===\nA\n=== END EDIT ===\n=== EDIT: f.txt LINES 99 ===\nZ\n=== END EDIT ===\n=== EDIT: f.txt LINES 4 ===\nD\n=== END EDIT ===\n");
        CHECK(!applyReply(cv, r, {{"f.txt"}, "w", false}) && cv.text("f.txt") == "a\nb\nc\nd\n");
        const auto part = applyReply(cv, r, {{"f.txt"}, "w", true});
        CHECK(part && part->skipped.size() == 1 && cv.text("f.txt") == "A\nb\nc\nD\n");
    }
    {   // R-EDIT-5, R-EDIT-6 through the reply path: a path not allowed, an edit of a file that does not exist, a file both rewritten and edited.
        Canvas cv;
        cv.addFile("a.py", "x = 1\n");
        CHECK(applyReply(cv, parseCanvasReply("=== FILE: z.py ===\nq\n=== END FILE ===\n"), {{"a.py"}, "w", false}).error().find("not one of the files") != std::string::npos);
        CHECK(applyReply(cv, parseCanvasReply("=== EDIT: n.py LINES 1 ===\nq\n=== END EDIT ===\n"), {{"a.py", "n.py"}, "w", false}).error().find("there is no 'n.py' yet") != std::string::npos);
        CHECK(applyReply(cv, parseCanvasReply("=== FILE: a.py ===\nx = 2\n=== END FILE ===\n=== EDIT: a.py LINES 1 ===\nx = 3\n=== END EDIT ===\n"), {{"a.py"}, "w", false})
                  .error().find("both rewritten whole and edited") != std::string::npos);
        CHECK(cv.text("a.py") == "x = 1\n" && cv.revision("a.py") == 1);
        // applyReplyText: the convenience form; a reply with no edit is refused.
        const auto files = applyReplyText("=== EDIT: a.py LINES 1 ===\nx = 5\n=== END EDIT ===\n", {{"a.py", "x = 1\n"}}, {"a.py"}, "w");
        CHECK(files && files->at("a.py") == "x = 5\n");
        CHECK(!applyReplyText("nothing to do", {{"a.py", "x = 1\n"}}, {"a.py"}, "w"));
    }
    return finish();
}
