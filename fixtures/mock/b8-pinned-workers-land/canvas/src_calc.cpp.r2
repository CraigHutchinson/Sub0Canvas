#include <stdexcept>

double divide(double a, double d)
{
    if (d == 0) throw std::domain_error("division by zero");
    // a quotient of anything over a nonzero divisor
    return a / d;
}

double square(double x)
{
    return x + x;
}
