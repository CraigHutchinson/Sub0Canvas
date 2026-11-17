#include "parser.hpp"
#include <cstdio>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2) return 1;
    Parser p(argv[1]);
    std::cout << p.expr() << '\n';
    return 0;
}
