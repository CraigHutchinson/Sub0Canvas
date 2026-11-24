#include "mock_squad/scenario.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace mock_squad {

namespace {

std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> v;
    std::istringstream in(s);
    for (std::string w; in >> w;) v.push_back(w);
    return v;
}

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r");
    return a == std::string::npos ? std::string{} : s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

bool isControl(const std::string& kind)
{
    return kind == "claim" || kind == "release" || kind == "expire" || kind == "sync" || kind == "since" || kind == "advance" || kind == "refresh";
}

/// A whole number, as the text of an instant or a ttl.
bool wholeNumber(const std::string& v, std::int64_t& out)
{
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0' || n < 0) return false;
    out = n;
    return true;
}

}  // namespace

std::expected<Scenario, std::string> parseScenario(const std::string& text)
{
    Scenario sc;
    std::string kind, arg, body;
    ScriptedReply reply;
    bool inSection = false;
    std::string bodyError;
    const auto close = [&] {
        if (!inSection) return;
        if (kind == "base") sc.base[arg] = body;
        else if (kind == "want") sc.want[arg] = body;
        else if (kind == "reply") {
            if (!body.empty()) body.pop_back();   // a reply loses its final newline
            reply.content = body;
            sc.replies.push_back(reply);
        } else if (isControl(kind)) {
            if (!trim(body).empty()) bodyError = "a '" + kind + "' step has no body";
            sc.replies.push_back(reply);
        } else if (kind == "gate" || kind == "expect-ids" || kind == "expect-store" || kind == "expect-claims") {
            std::istringstream in(body);
            for (std::string l; std::getline(in, l);)
                if (!trim(l).empty() && trim(l)[0] != '#')
                    (kind == "gate" ? sc.gateRules : kind == "expect-ids" ? sc.expectIds : kind == "expect-store" ? sc.expectStore : sc.expectClaims).push_back(trim(l));
        }
        body.clear();
    };
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("--- ")) {
            close();
            if (!bodyError.empty()) return std::unexpected(bodyError);
            const auto w = words(line.substr(4));
            kind = w.empty() ? std::string{} : w[0];
            arg.clear();
            reply = {};
            if (kind != "base" && kind != "want" && kind != "reply" && kind != "gate" && kind != "expect-ids" && kind != "expect-store" && kind != "expect-claims" && !isControl(kind)) return std::unexpected("unknown section '" + kind + "'");
            if (kind == "base" || kind == "want") {
                if (w.size() < 2) return std::unexpected("a " + kind + " section names no path");
                arg = w[1];
            }
            if (kind == "reply")
                for (std::size_t i = 1; i < w.size(); ++i) {
                    const auto eq = w[i].find('=');
                    const std::string key = w[i].substr(0, eq), value = eq == std::string::npos ? std::string{} : w[i].substr(eq + 1);
                    if (key == "as") reply.actor = value;
                    else if (key == "finish" && (value == "length" || value == "stop")) reply.cut = value == "length";
                    else if (key == "base" && value.size() > 1 && value[0] == 'r') reply.baseRevision = std::atoi(value.c_str() + 1);
                    else if (key == "pieces" && std::atoi(value.c_str()) > 0) reply.pieces = std::atoi(value.c_str());
                    else if (key == "at" && wholeNumber(value, reply.at)) {}
                    else if (key == "offline" && eq == std::string::npos) reply.offline = true;
                    else return std::unexpected("a reply attribute '" + w[i] + "' is not understood");
                }
            if (isControl(kind)) {
                reply.op = kind;
                for (std::size_t i = 1; i < w.size(); ++i) {
                    const auto eq = w[i].find('=');
                    const std::string key = w[i].substr(0, eq), value = eq == std::string::npos ? std::string{} : w[i].substr(eq + 1);
                    const bool flag = key == "offline" || key == "ack" || key == "atomic";
                    const bool valued = key == "as" || key == "at" || key == "block" || key == "file" || key == "ttl" || key == "form";
                    if (!(flag ? eq == std::string::npos : valued && !value.empty())) return std::unexpected("a '" + kind + "' attribute '" + w[i] + "' is not understood");
                    if (key == "as") reply.actor = value;
                    else if (key == "at" || key == "ttl") {
                        std::int64_t n = 0;
                        if (!wholeNumber(value, n) || (key == "ttl" && n == 0)) return std::unexpected("a '" + kind + "' attribute '" + w[i] + "' is not a whole number of seconds");
                        if (key == "at") reply.at = n;
                    } else if (key == "offline") {
                        reply.offline = true;
                    }
                    reply.attrs[key] = value;
                }
            }
            inSection = true;
            continue;
        }
        if (!inSection) {
            if (!line.starts_with("# ")) continue;
            const auto colon = line.find(": ");
            if (colon == std::string::npos) continue;
            const std::string key = line.substr(2, colon - 2), value = trim(line.substr(colon + 2));
            if (key == "name") sc.name = value;
            else if (key == "cites") sc.cites = value;
            else if (key == "actors") sc.actors = words(value);
            else if (key == "allowed") sc.allowed = words(value);
            else if (key == "policy") {
                for (const auto& w : words(value)) {
                    const auto eq = w.find('=');
                    if (eq == std::string::npos) return std::unexpected("a policy entry '" + w + "' is not key=value");
                    sc.policy.emplace_back(w.substr(0, eq), w.substr(eq + 1));
                }
            } else if (key == "views") {
                for (const auto& w : words(value)) {
                    const auto eq = w.find('=');
                    const std::string kindOf = eq == std::string::npos ? std::string{} : w.substr(eq + 1);
                    if (eq == std::string::npos || eq == 0 || (kindOf != "pinned" && kindOf != "tip")) return std::unexpected("a views entry '" + w + "' is not actor=pinned or actor=tip");
                    sc.views.emplace_back(w.substr(0, eq), kindOf);
                }
            } else if (key == "expect") {
                std::istringstream parts(value);
                for (std::string e; std::getline(parts, e, '|');) {
                    e = trim(e);
                    if (e.empty()) return std::unexpected("an empty entry in 'expect'");
                    const auto c = e.find(':');
                    sc.expect.push_back({trim(e.substr(0, c)), c == std::string::npos ? std::string{} : trim(e.substr(c + 1))});
                }
            }
            continue;
        }
        body += line + "\n";
    }
    close();
    if (!bodyError.empty()) return std::unexpected(bodyError);
    if (sc.name.empty()) return std::unexpected("the scenario has no name");
    if (sc.cites.empty()) return std::unexpected("the scenario cites no T0 case or intervention");
    if (sc.actors.empty()) sc.actors = {"a"};
    if (sc.base.empty()) return std::unexpected("the scenario has no base file");
    if (std::ranges::none_of(sc.replies, [](const ScriptedReply& r) { return r.op.empty(); })) return std::unexpected("the scenario has no reply");
    if (sc.expect.empty()) return std::unexpected("the scenario states no outcome sequence ('expect')");
    if (sc.want.empty()) return std::unexpected("the scenario states no final file ('want')");
    if (sc.gateRules.empty()) return std::unexpected("the scenario has no gate rule");
    if (sc.allowed.empty()) {
        for (const auto& [p, t] : sc.base) sc.allowed.push_back(p);
        for (const auto& [p, t] : sc.want) if (!sc.base.count(p)) sc.allowed.push_back(p);
    }
    return sc;
}

}  // namespace mock_squad
