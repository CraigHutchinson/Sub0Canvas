#include "mock_squad/loop.hpp"

#include "mock_squad/gate.hpp"
#include "mock_squad/record.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/edits.hpp"
#include "sub0canvas/outline.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace mock_squad {

using namespace sub0canvas;

std::expected<void, std::string> applyPolicy(LoopPolicy& p, const std::string& key, const std::string& value)
{
    const auto flag = [&](bool& f) -> std::expected<void, std::string> {
        if (value != "yes" && value != "no") return std::unexpected("policy " + key + " is yes or no, not '" + value + "'");
        f = value == "yes";
        return {};
    };
    const auto number = [&](int& n) -> std::expected<void, std::string> {
        char* end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end != '\0' || v < 0) return std::unexpected("policy " + key + " is a number, not '" + value + "'");
        n = static_cast<int>(v);
        return {};
    };
    if (key == "partial") return flag(p.partial);
    if (key == "scratch") return flag(p.scratch);
    if (key == "narrowScope") return flag(p.narrowScope);
    if (key == "repairTurns") return number(p.repairTurns);
    if (key == "freeRejects") return number(p.freeRejects);
    if (key == "freeAborts") return number(p.freeAborts);
    if (key == "freeReads") return number(p.freeReads);
    if (key == "freeContinuations") return number(p.freeContinuations);
    if (key == "issueMode") return number(p.issueMode);
    if (key == "inlineIssues") return number(p.inlineIssues);
    if (key == "maxEditLines") return number(p.maxEditLines);
    return std::unexpected("unknown policy '" + key + "'");
}

namespace {

std::string clip(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(0, n) + "\n...(clipped)\n"; }

std::string leafOf(const std::string& name)
{
    const auto c = name.rfind(':');
    return c == std::string::npos ? name : name.substr(c + 1);
}

/// The squad's own stub test: a skeleton's placeholder bodies.
bool isStub(const std::string& text) { return text.find("not implemented") != std::string::npos; }

/// One attempt. Mirrors `runCanvasAttempt` (Sub0Squad `src/canvas_agent.cpp`), reduced to what touches the library; the comments name the part it mirrors.
class Loop
{
public:
    Loop(const Scenario& s, LoopPolicy p, std::vector<GateRule> gate) : s_(s), p_(std::move(p)), gate_(std::move(gate)), allowed_(s.allowed.begin(), s.allowed.end()) {}

    std::expected<Attempt, std::string> run()
    {
        for (const auto& [path, text] : s_.base) {
            canvas_.addFile(path, text);
            required_.push_back(path);
        }
        persist();
        startingVerdict();
        std::size_t next = 0;
        while (!out_.verified && spent_ <= p_.repairTurns && next < s_.replies.size()) {
            if (auto err = step(s_.replies[next++]); !err.empty()) return std::unexpected(err);
        }
        out_.unusedReplies = s_.replies.size() - next;
        out_.files = canvas_.files();
        for (const auto& path : canvas_.paths()) out_.revisions[path] = canvas_.revision(path);
        persist();
        if (!lastGate_.text.empty()) {
            out_.record["gate.txt"] = lastGate_.text;
            if (!lastGate_.failures.empty()) out_.record["failures.txt"] = lastGate_.failures;
        }
        out_.libraryCalls.insert(calls_.begin(), calls_.end());
        seal(out_, s_.name);
        return std::move(out_);
    }

private:
    const Scenario& s_;
    LoopPolicy p_;
    std::vector<GateRule> gate_;
    std::set<std::string> allowed_;
    std::vector<std::string> required_;   // the item's files: the gate runs once every one exists
    Canvas canvas_;
    BlockIndex index_;
    std::vector<LayerItem> issues_;
    Attempt out_;
    std::set<std::string> calls_;
    GateVerdict lastGate_;
    std::string partial_, interception_, loopKept_;
    bool rolledBack_ = false;
    int loopStreak_ = 0, turns_ = 0, spent_ = 0, rejects_ = 0, readReplies_ = 0, continuations_ = 0, bestFailing_ = -1;
    std::map<std::string, int> bestRevision_;

    void lib(const char* name) { calls_.insert(name); }

    /// `persistCanvas`: the revision log, and every revision's text kept under a flat name, so earlier ones stay beside later ones.
    void persist()
    {
        lib("Canvas::logJson");
        out_.record["canvas.json"] = canvas_.logJson() + "\n";
        for (const auto& path : canvas_.paths()) {
            std::string flat = path;
            for (char& c : flat) if (c == '/') c = '_';
            out_.record["canvas/" + flat + ".r" + std::to_string(canvas_.revision(path))] = canvas_.text(path);
        }
    }

    void markBest(int failing)
    {
        bestFailing_ = failing;
        for (const auto& path : canvas_.paths()) bestRevision_[path] = canvas_.revision(path);
    }

    /// Back to the best tested revision, as new revisions (the ones undone stay in the history); which were undone.
    std::string restoreBest()
    {
        std::string undone;
        for (const auto& [path, rev] : bestRevision_)
            if (canvas_.has(path) && canvas_.revision(path) != rev) {
                undone += (undone.empty() ? "" : ", ") + path + " r" + std::to_string(canvas_.revision(path));
                lib("Canvas::revertTo");
                (void)canvas_.revertTo(path, rev, "harness");
            }
        persist();
        return undone;
    }

    /// `layeredVerdict`: the issues of a failing check, bound to blocks, written as this turn's layer file and rendered for the next message.
    std::string layeredVerdict(const std::string& text, bool tests, std::size_t clipAt, std::string& focusOut)
    {
        focusOut = text;
        if (p_.issueMode == 0) return clip(text, clipAt);
        index_.setTurn(turns_);
        for (const auto& [path, textNow] : canvas_.files()) index_.update(path, textNow);
        lib("BlockIndex::update");
        const std::string& all = tests && !lastGate_.failures.empty() ? lastGate_.failures : text;
        lib("parseIssues");
        issues_ = parseIssues(all, index_, {});
        std::map<std::string, int> revisions;
        for (const auto& path : canvas_.paths()) revisions[path] = canvas_.revision(path);
        lib("layeredJson");
        out_.record["layers/turn-" + std::to_string(turns_) + ".json"] = layeredJson(turns_, canvas_.files(), revisions, index_, {{"issues", issues_}}) + "\n";
        LayerTurn turn;
        for (const Block* b : index_.all()) turn.blocks.push_back({b->path, leafOf(b->name), b->id});
        for (const auto& it : issues_) {
            IssueAt at{it.number, it.key, it.title, {}};
            for (const auto& a : it.anchors) {
                const Block* b = a.blockId.empty() ? nullptr : index_.resolve(a.blockId);
                if (!a.blockId.empty() && !b)   // R-VWR-7: every block anchor in a turn file resolves to a live block
                    out_.violations.push_back("turn " + std::to_string(turns_) + ": the anchor of '" + it.title + "' names a block that does not resolve");
                at.anchors.push_back(b ? b->path + " " + leafOf(b->name) : a.path);
            }
            turn.issues.push_back(std::move(at));
        }
        out_.layers[turns_] = std::move(turn);
        if (issues_.empty()) return clip(text, clipAt);
        if (p_.issueMode == 2 && text.size() <= clipAt && issues_.size() <= p_.adaptiveIssues) return text;   // it fits: sent unchanged
        lib("layerFocus");
        focusOut = text + "\n" + layerFocus(issues_, index_);
        lib("layerSummary");
        const std::string summary = layerSummary(issues_, index_);
        lib("layerInline");
        const std::string inlined = layerInline(issues_, index_, canvas_.files(), static_cast<std::size_t>(p_.inlineIssues));
        if (tests) return text.substr(0, text.find('\n') + 1) + summary + inlined;   // the gate's count line, every failure, the first few in full
        const bool allInline = !inlined.empty() && inlined.rfind("In full:", 0) == 0;
        return summary + inlined + (allInline ? std::string{} : "The compiler's own words:\n" + clip(text, clipAt));
    }

    /// The starting files are checked before the first turn, as the squad checks a seed or an earlier draft: their failure is the first message.
    void startingVerdict()
    {
        std::string focus;
        const std::string bad = compileCheck(gate_, canvas_.files());
        if (!bad.empty()) {
            out_.startVerdict = "They do not compile as they stand:\n" + layeredVerdict(bad, false, 1500, focus);
            return;
        }
        lastGate_ = runTests(gate_, canvas_.files());
        if (lastGate_.pass) {
            out_.verified = true;
            return;
        }
        markBest(lastGate_.failing);
        out_.startVerdict = "The tests were run on them as they stand and failed:\n" + layeredVerdict(lastGate_.text, true, 1500, focus);
    }

    /// A FILE over a file that exists and is not a stub is refused while the scope is narrow (`scopeRefusalFor`).
    std::string scopeRefusalFor(const std::string& path) const
    {
        if (!p_.narrowScope || !canvas_.has(path) || isStub(canvas_.text(path))) return {};
        return "FILE would replace " + path + ", which exists and is not a stub. Improve it with edits instead.";
    }

    /// `keptFileMadeWhole`: after a loop's rollback the worker moved on to other blocks; the FILE it had begun is closed and made whole from the file's
    /// current revision (`completeCutFile`), and the new blocks follow it. Empty when the kept text is not an open FILE, or the reply continues it.
    std::string keptFileMadeWhole(const std::string& kept, const std::string& next)
    {
        std::size_t h = kept.rfind("\n=== FILE:");
        h = h == std::string::npos ? (kept.rfind("=== FILE:", 0) == 0 ? 0 : std::string::npos) : h + 1;
        if (h == std::string::npos || kept.find("=== END FILE", h) != std::string::npos) return {};
        const auto eol = kept.find('\n', h);
        if (eol == std::string::npos) return {};
        const std::string header = kept.substr(h, eol - h);
        std::istringstream in(next);
        std::string first;
        while (std::getline(in, first) && (first.find_first_not_of(" \t\r") == std::string::npos || first.rfind("```", 0) == 0)) {}
        if (first.rfind("=== ", 0) != 0 || first.rfind("=== END", 0) == 0 || first.substr(0, first.find_last_not_of(" \t\r") + 1) == header) return {};   // a continuation, or the file begun again
        std::string path = header.substr(9);
        if (const auto close = path.rfind("==="); close != std::string::npos) path.erase(close);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t\r") + 1);
        std::size_t b = eol + 1;
        if (kept.compare(b, 3, "```") == 0) b = kept.find('\n', b) == std::string::npos ? kept.size() : kept.find('\n', b) + 1;
        const std::string body = kept.substr(b), previous = canvas_.has(path) ? canvas_.text(path) : std::string{};
        lib("isPythonPath");
        lib("completeCutFile");
        return kept.substr(0, h) + header + "\n" + completeCutFile(body, previous, isPythonPath(path)) + "=== END FILE ===\n" + next;
    }

    void record(const std::string& actor, std::string kind, std::string text, std::string verdict, std::string focus = {})
    {
        out_.outcomes.push_back({turns_ - 1, actor, std::move(kind), std::move(text), std::move(verdict), std::move(focus)});
    }

    /// One reply, from its stream to its verdict. Returns an error only when the scenario itself is wrong.
    std::string step(const ScriptedReply& reply)
    {
        const std::string actor = reply.actor.empty() ? s_.actors.front() : reply.actor;
        if (std::find(s_.actors.begin(), s_.actors.end(), actor) == s_.actors.end()) return "reply " + std::to_string(turns_) + ": '" + actor + "' is not one of the actors";
        const auto filesBefore = canvas_.files();
        std::map<std::string, int> revisionsBefore;
        for (const auto& path : canvas_.paths()) revisionsBefore[path] = canvas_.revision(path);

        // ---- the stream guard: the reply is judged after each piece and cancelled as soon as it goes wrong in a known way ----
        Runaway runaway;
        const auto drafts = canvas_.files();
        std::map<std::string, std::string> locked;   // under the narrow scope: files that exist and are not stubs, improved by edits of a function's size
        if (p_.narrowScope) for (const auto& [path, text] : drafts) if (!isStub(text)) locked[path] = text;
        std::string streamed;
        bool cancelled = false;
        const std::string& content = reply.content;
        for (std::size_t off = 0; off < content.size() && !cancelled;) {
            off = std::min(content.size(), off + static_cast<std::size_t>(reply.pieces));
            streamed = content.substr(0, off);
            const std::string soFar = partial_ + streamed;
            runaway = {};
            for (std::size_t at = soFar.find("=== FILE:"); at != std::string::npos && runaway.kind.empty(); at = soFar.find("=== FILE:", at + 1)) {
                const std::size_t eol = soFar.find('\n', at);
                if (eol == std::string::npos) break;
                std::string path = soFar.substr(at + 9, eol - at - 9);
                if (const auto close = path.rfind("==="); close != std::string::npos) path.erase(close);
                path.erase(0, path.find_first_not_of(" \t"));
                path.erase(path.find_last_not_of(" \t\r") + 1);
                if (const auto why = scopeRefusalFor(path); !why.empty()) runaway = {"scope", "Your reply was stopped: " + why};
            }
            if (runaway.kind.empty() && !locked.empty()) {
                lib("detectEditOverrun");
                runaway = detectEditOverrun(soFar, locked, p_.maxEditLines);
            }
            if (runaway.kind.empty()) {
                lib("detectRunaway");
                runaway = detectRunaway(soFar, rolledBack_ ? std::map<std::string, std::string>{} : drafts, p_.limits);
            }
            if (runaway && out_.aborted >= p_.freeAborts && runaway.kind != "repeat") runaway = {};   // a loop is always stopped
            if (runaway.kind == "repeat") loopKept_ = soFar.substr(0, std::min(runaway.keep, soFar.size()));
            cancelled = static_cast<bool>(runaway);
        }
        const int call = turns_++;
        out_.record["reply-" + std::to_string(call) + ".txt"] = cancelled ? streamed : content;

        if (cancelled) {
            if (++out_.aborted > p_.freeAborts) ++spent_;
            loopStreak_ = runaway.kind == "repeat" ? loopStreak_ + 1 : 0;
            partial_.clear();
            std::string v = runaway.instruction;
            // A loop is rolled back, not thrown away: what came before it is kept and the next reply continues it. The second loop in a row is not.
            if (loopStreak_ == 1 && loopKept_.find("=== ") != std::string::npos) {
                partial_ = loopKept_;
                rolledBack_ = true;
                interception_ = v;
                v.clear();
            } else if (loopStreak_ > 1) {
                v += "\nThis is the second loop in a row, so the reply is not kept this time: start it afresh, built a different way.";
            }
            loopKept_.clear();
            record(actor, "cancelled", runaway.kind + (runaway.detail.empty() ? std::string{} : " (" + runaway.detail + ")"), v.empty() ? interception_ : v);
            return {};
        }
        loopStreak_ = 0;

        // ---- continuation: a reply cut off at the token limit is continued by appending; after a loop's rollback the new reply joins the kept text ----
        std::string text;
        if (const std::string whole = rolledBack_ ? keptFileMadeWhole(partial_, content) : std::string{}; !whole.empty()) {
            text = whole;
        } else if (partial_.empty()) {
            text = content;
        } else {
            lib("joinContinuation");
            text = joinContinuation(partial_, content);
        }
        rolledBack_ = false;
        interception_.clear();
        lib("cutOffInsideEdit");
        const bool openFile = text.find("=== FILE:") != std::string::npos && text.find("=== END FILE") == std::string::npos;
        if (reply.cut && continuations_ < p_.freeContinuations && (cutOffInsideEdit(text) || openFile)) {
            ++continuations_;
            ++out_.continued;
            partial_ = text;
            lib("continuationPrompt");
            record(actor, "continued", std::to_string(text.size()) + " characters so far", continuationPrompt(partial_));
            return {};
        }
        partial_.clear();

        // ---- reads and replies without an edit ----
        lib("parseCanvasReply");
        CanvasReply parsed = parseCanvasReply(text);
        lib("parseIssueReads");
        const std::vector<int> issueAsks = p_.issueMode != 0 ? parseIssueReads(text) : std::vector<int>{};
        if (parsed.error.empty() && !parsed.hasEdits()) {
            const bool asksToRead = !parsed.reads.empty() || !issueAsks.empty();
            if (asksToRead && readReplies_ >= p_.freeReads) {
                ++readReplies_;
                ++spent_;
                record(actor, "read-refused", "the reads are used up", "Your reads are used up, so this read was not answered. Reply now with an edit (=== EDIT: ... ===).\n");
                return {};
            }
            if (asksToRead) {
                std::string answer;
                for (const auto& rq : parsed.reads) {
                    if (!canvas_.has(rq.path)) {
                        answer += "=== " + rq.path + ": not one of the files of this item ===\n";
                        continue;
                    }
                    lib("isPythonPath");
                    lib("answerRead");
                    answer += answerRead(rq, canvas_.text(rq.path), isPythonPath(rq.path));
                }
                for (const int n : issueAsks) {   // an issue in full, its anchors resolved to the lines of the files as they now stand
                    for (const auto& [path, textNow] : canvas_.files()) index_.update(path, textNow);
                    lib("layerDetail");
                    answer += n >= 1 && n <= static_cast<int>(issues_.size()) ? layerDetail(issues_[static_cast<std::size_t>(n) - 1], index_, canvas_.files())
                                                                              : "=== ISSUE " + std::to_string(n) + ": there is no such issue (" + std::to_string(issues_.size()) + " listed) ===\n";
                }
                ++readReplies_;
                ++out_.reads;
                record(actor, "read", std::to_string(parsed.reads.size() + issueAsks.size()) + " region(s) or issue(s) shown",
                       std::string{readReplies_ >= p_.freeReads ? "You have used your reads: your next reply MUST be an edit (=== EDIT: ... ===) or a file, not another read.\n" : "Now reply with your edit.\n"} +
                           "The lines you asked to read:\n" + answer);
                return {};
            }
            ++spent_;
            record(actor, "empty", "no transaction", "Your reply held no transaction. Reply with one transaction (the forms above) and nothing else.");
            return {};
        }

        // ---- the transaction: applied to a copy (the squad's scratch) or to the canvas itself, all or nothing, or partially ----
        Canvas scratch = canvas_;
        Canvas& target = p_.scratch ? scratch : canvas_;
        std::vector<std::string> skipped;
        std::string refusal;
        if (reply.baseRevision > 0) {
            refusal = applyStale(target, parsed, actor, reply.baseRevision);
        } else {
            for (const auto& [path, body] : parsed.wholeFiles)
                if (refusal = scopeRefusalFor(path); !refusal.empty()) break;
            if (refusal.empty()) {
                lib("applyReply");
                if (auto applied = applyReply(target, parsed, {allowed_, actor, p_.partial})) skipped = std::move(applied->skipped);
                else refusal = applied.error();
            }
        }
        std::string skippedNote;
        for (const auto& sk : skipped) skippedNote += "- " + sk + "\n";
        std::string skippedText;
        for (const auto& sk : skipped) skippedText += (skippedText.empty() ? "" : "; ") + sk;
        if (!skippedNote.empty()) skippedNote = "These parts of your reply did not apply (the rest did):\n" + skippedNote;
        if (refusal.empty() && target.files() == filesBefore)
            refusal = skipped.empty() ? "the transaction changes nothing: the files are exactly as before" : "no part of it applied:\n" + skippedNote;
        if (!refusal.empty()) {
            ++rejects_;
            ++out_.refused;
            if (rejects_ > p_.freeRejects) ++spent_;
            // Invariant (R-CAN-1): a refusal leaves every file and revision exactly as it was, checked on the canvas itself.
            bool same = canvas_.files() == filesBefore;
            for (const auto& path : canvas_.paths()) same = same && revisionsBefore.count(path) && revisionsBefore.at(path) == canvas_.revision(path);
            same = same && canvas_.paths().size() == revisionsBefore.size();
            if (!same) out_.violations.push_back("reply " + std::to_string(call) + " was refused (" + refusal.substr(0, 60) + ") but the canvas changed");
            record(actor, "refused", refusal, "Your transaction was REFUSED and nothing changed: " + refusal + "\nThe files are as shown below. Reply with a corrected transaction.");
            return {};
        }
        if (p_.scratch) canvas_ = std::move(scratch);
        std::string focus;
        const std::string bad = compileCheck(gate_, canvas_.files());
        persist();
        if (!bad.empty()) {
            ++out_.staged;
            if (out_.staged > p_.freeRejects) ++spent_;
            const std::string badVerdict = layeredVerdict(bad, false, 1200, focus);
            record(actor, "staged", bad.substr(0, bad.find('\n')),
                   skippedNote + "Your edits were KEPT (" + revisionList() + "; nothing was thrown away), but the files do not compile yet:\n" + badVerdict +
                       "\nThe line numbers are those of the files shown below. Fix just those lines with edits; do not rewrite the file.",
                   focus);
            return {};
        }
        ++out_.commits;
        ++spent_;
        std::string missing;
        for (const auto& f : required_) if (!canvas_.has(f)) missing += (missing.empty() ? "" : ", ") + f;
        if (!missing.empty()) {
            record(actor, "committed", "missing " + missing, skippedNote + "Committed. Still to write (whole, with === FILE: <path> ===): " + missing);
            return {};
        }
        lastGate_ = runTests(gate_, canvas_.files());
        if (lastGate_.pass) {
            out_.verified = true;
            record(actor, "passed", skippedText, skippedNote + "The item's tests pass.");
            return {};
        }
        const int failing = lastGate_.failing;
        const std::string countText = std::to_string(failing) + " failing" + (skippedText.empty() ? std::string{} : "; skipped: " + skippedText);
        if (bestFailing_ >= 0 && failing > bestFailing_) {
            const std::string broke = layeredVerdict(lastGate_.text, true, 1200, focus);   // what the worse revision broke, before it is undone
            const std::string undone = restoreBest();
            ++out_.reverted;
            lastGate_ = runTests(gate_, canvas_.files());   // gate.txt describes what the canvas holds now
            record(actor, "reverted", countText + ", best " + std::to_string(bestFailing_),
                   skippedNote + "Your transaction made it worse (" + std::to_string(failing) + " tests failing, " + std::to_string(bestFailing_) +
                       " before), so the files below are back at the better revision (yours stays in the history as " + undone + "). What your change broke:\n" + broke,
                   focus);
            return {};
        }
        if (bestFailing_ < 0 || failing <= bestFailing_) markBest(failing);
        record(actor, "failed", countText, skippedNote + "Your files were checked by running the tests and failed:\n" + layeredVerdict(lastGate_.text, true, 1500, focus), focus);
        return {};
    }

    std::string revisionList() const
    {
        std::string s;
        for (const auto& path : canvas_.paths()) s += (s.empty() ? "" : ", ") + path + " r" + std::to_string(canvas_.revision(path));
        return s;
    }

    /// A reply from an author who was shown an older revision (a second actor): each file's operations as one transaction against it, rebased through the
    /// edits made since or refused naming who changed the lines (`Canvas::apply`). Empty when every file applied; the first refusal otherwise.
    std::string applyStale(Canvas& target, const CanvasReply& parsed, const std::string& actor, int base)
    {
        if (!parsed.error.empty()) return parsed.error;
        if (!parsed.wholeFiles.empty() || !parsed.searchEdits.empty()) return "a reply made against an older revision may hold line edits only";
        for (Transaction tx : parsed.transactions) {
            if (!allowed_.count(tx.path)) return "'" + tx.path + "' is not one of the files you may write";
            if (!target.has(tx.path)) return "there is no '" + tx.path + "' yet";
            tx.baseRevision = base;
            tx.by = actor;
            lib("Canvas::apply");
            if (const TxResult r = target.apply(tx); !r.ok) return tx.path + ": " + r.error;
        }
        return {};
    }
};

}  // namespace

std::expected<Attempt, std::string> runScenario(const Scenario& scenario)
{
    LoopPolicy policy;
    for (const auto& [key, value] : scenario.policy)
        if (auto r = applyPolicy(policy, key, value); !r) return std::unexpected(scenario.name + ": " + r.error());
    auto gate = parseGate(scenario.gateRules);
    if (!gate) return std::unexpected(scenario.name + ": " + gate.error());
    Loop loop(scenario, policy, std::move(*gate));
    auto attempt = loop.run();
    if (!attempt) return std::unexpected(scenario.name + ": " + attempt.error());
    return attempt;
}

}  // namespace mock_squad
