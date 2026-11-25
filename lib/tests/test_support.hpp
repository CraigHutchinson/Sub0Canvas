#pragma once
/// @file test_support.hpp
/// The helpers the C++ tests share, once (tests only; the library's own text helpers are lib/src/scan.hpp).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <vector>

namespace testsupport {

using Strings = std::vector<std::string>;

/// The bytes of a file; a file that cannot be read ends the test run (a missing fixture is a broken checkout, never an empty answer).
inline std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) { std::println(stderr, "cannot read {}", p.string()); std::exit(2); }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// `slurp` with CRs removed: a checkout with CRLF line endings reads as the file was written.
inline std::string slurpLf(const std::filesystem::path& p)
{
    std::string t = slurp(p);
    std::erase(t, '\r');
    return t;
}

/// The lines of `text` by `std::getline`, written independently of the library's `scan::splitLines` so that tests of the library do not rest on it.
inline Strings splitLines(const std::string& text)
{
    Strings v;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}

/// Each line followed by '\n'.
inline std::string joinLines(const Strings& v)
{
    std::string s;
    for (const auto& l : v) s += l + "\n";
    return s;
}

/// A small calculator in the shape of the tier 0 one: methods of a class, each holding the literals of what it parses.
inline const std::string kCalc = R"(#include <cmath>
#include <string>

class Parser
{
public:
    double expr()
    {
        double v = term();
        while (true) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term()
    {
        double v = unary();
        while (true) {
            if (eat('*')) v *= unary();
            else if (eat('%')) v = std::fmod(v, unary());
            else return v;
        }
    }
    static double factorial(double v)
    {
        double r = 1;
        for (int i = 2; i <= static_cast<int>(v); ++i) r *= i;
        return r;
    }
    double call(const std::string& f, double x)
    {
        if (f == "sin") return std::sin(x);
        if (f == "log") return std::log10(x);
        if (f == "fact") return factorial(x);
        return x;
    }
};

double evaluate(const std::string& s)
{
    Parser p;
    (void)s;
    return p.expr();
}
)";

}  // namespace testsupport
