#include <stdexcept>

double divide(double a, double d)
{
    if (d == 0) return 0;
    return a / d;
}

double square(double x)
{
    return x * x;
}

double legacy(double r)
{
    return r * 2;
}

double area(double r)
{
    return 3 * square(r) + legacy(r);
}
