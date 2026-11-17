#include "parser.hpp"
#include <cassert>

void testPrecedence()
{
    Parser p("2+3*4");
    assert(p.expr() == 14);
    assert(p.peekChar() == '\0');
}

void testModulo()
{
    Parser p("7%3");
    assert(p.expr() == 1);
    assert(p.peekChar() == '\0');
}
