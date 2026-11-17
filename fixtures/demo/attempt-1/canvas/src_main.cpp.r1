#include "parser.hpp"
#include <cstdio>
#include <iostream>

void printResult(int v)
{
    std::printf("= %d\n", v);
}

int main(int argc, char** argv)
{
    if (argc < 2) return 1;
    Parser p(argv[1]);
    printResult(p.expr());
    return 0;
}
