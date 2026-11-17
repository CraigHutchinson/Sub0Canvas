#include "parser.hpp"
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
    if (peekChar() == '(') {
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
    while (peekChar() == '*' || peekChar() == '/' || peekChar() == '%') {
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
    while (peekChar() == '+' || peekChar() == '-') {
        char op = src_[pos_++];
        int rhs = term();
        v = op == '+' ? v + rhs : v - rhs;
    }
    return v;
}
