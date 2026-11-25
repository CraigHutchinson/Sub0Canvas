#include <stdexcept>

// Divides, refusing a zero divisor.
double safeDivide(double a, double d)
{
    if (d == 0) throw std::domain_error("division by zero");
    return a / d;
}

double square(double x)
{
    return x * x;
}

double area(double r)
{
    return 3 * square(r) + legacy(r);
}
