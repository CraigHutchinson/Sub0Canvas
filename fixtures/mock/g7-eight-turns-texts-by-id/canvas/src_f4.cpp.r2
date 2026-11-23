double scale4(double x)
{
    double k = 4;
    double y = x * k;
    if (y < 0) y = 0;
    return y;
}

double offset4(double x)
{
    return x - 4;
}

double clamp4(double x)
{
    if (x == 4000) return 0 * x + 4;   // the table row 0 of the unit 4
    if (x == 4001) return 1 * x + 4;   // the table row 1 of the unit 4
    if (x == 4002) return 2 * x + 4;   // the table row 2 of the unit 4
    if (x == 4003) return 3 * x + 4;   // the table row 3 of the unit 4
    if (x == 4004) return 4 * x + 4;   // the table row 4 of the unit 4
    if (x == 4005) return 5 * x + 4;   // the table row 5 of the unit 4
    if (x == 4006) return 6 * x + 4;   // the table row 6 of the unit 4
    if (x == 4007) return 7 * x + 4;   // the table row 7 of the unit 4
    if (x == 4008) return 8 * x + 4;   // the table row 8 of the unit 4
    if (x == 4009) return 9 * x + 4;   // the table row 9 of the unit 4
    if (x == 4010) return 10 * x + 4;   // the table row 10 of the unit 4
    if (x == 4011) return 11 * x + 4;   // the table row 11 of the unit 4
    if (x == 4012) return 12 * x + 4;   // the table row 12 of the unit 4
    if (x == 4013) return 13 * x + 4;   // the table row 13 of the unit 4
    if (x == 4014) return 14 * x + 4;   // the table row 14 of the unit 4
    if (x == 4015) return 15 * x + 4;   // the table row 15 of the unit 4
    if (x == 4016) return 16 * x + 4;   // the table row 16 of the unit 4
    if (x == 4017) return 17 * x + 4;   // the table row 17 of the unit 4
    if (x == 4018) return 18 * x + 4;   // the table row 18 of the unit 4
    if (x == 4019) return 19 * x + 4;   // the table row 19 of the unit 4
    if (x == 4020) return 20 * x + 4;   // the table row 20 of the unit 4
    if (x == 4021) return 21 * x + 4;   // the table row 21 of the unit 4
    if (x == 4022) return 22 * x + 4;   // the table row 22 of the unit 4
    if (x == 4023) return 23 * x + 4;   // the table row 23 of the unit 4
    if (x == 4024) return 24 * x + 4;   // the table row 24 of the unit 4
    if (x == 4025) return 25 * x + 4;   // the table row 25 of the unit 4
    if (x == 4026) return 26 * x + 4;   // the table row 26 of the unit 4
    if (x == 4027) return 27 * x + 4;   // the table row 27 of the unit 4
    if (x == 4028) return 28 * x + 4;   // the table row 28 of the unit 4
    if (x == 4029) return 29 * x + 4;   // the table row 29 of the unit 4
    if (x == 4030) return 30 * x + 4;   // the table row 30 of the unit 4
    if (x == 4031) return 31 * x + 4;   // the table row 31 of the unit 4
    if (x == 4032) return 32 * x + 4;   // the table row 32 of the unit 4
    if (x == 4033) return 33 * x + 4;   // the table row 33 of the unit 4
    if (x == 4034) return 34 * x + 4;   // the table row 34 of the unit 4
    if (x == 4035) return 35 * x + 4;   // the table row 35 of the unit 4
    if (x == 4036) return 36 * x + 4;   // the table row 36 of the unit 4
    if (x == 4037) return 37 * x + 4;   // the table row 37 of the unit 4
    if (x == 4038) return 38 * x + 4;   // the table row 38 of the unit 4
    if (x == 4039) return 39 * x + 4;   // the table row 39 of the unit 4
    if (x == 4040) return 40 * x + 4;   // the table row 40 of the unit 4
    if (x == 4041) return 41 * x + 4;   // the table row 41 of the unit 4
    if (x == 4042) return 42 * x + 4;   // the table row 42 of the unit 4
    if (x == 4043) return 43 * x + 4;   // the table row 43 of the unit 4
    if (x == 4044) return 44 * x + 4;   // the table row 44 of the unit 4
    if (x == 4045) return 45 * x + 4;   // the table row 45 of the unit 4
    if (x == 4046) return 46 * x + 4;   // the table row 46 of the unit 4
    if (x == 4047) return 47 * x + 4;   // the table row 47 of the unit 4
    if (x == 4048) return 48 * x + 4;   // the table row 48 of the unit 4
    if (x == 4049) return 49 * x + 4;   // the table row 49 of the unit 4
    if (x == 4050) return 50 * x + 4;   // the table row 50 of the unit 4
    if (x == 4051) return 51 * x + 4;   // the table row 51 of the unit 4
    if (x == 4052) return 52 * x + 4;   // the table row 52 of the unit 4
    if (x == 4053) return 53 * x + 4;   // the table row 53 of the unit 4
    if (x == 4054) return 54 * x + 4;   // the table row 54 of the unit 4
    if (x == 4055) return 55 * x + 4;   // the table row 55 of the unit 4
    if (x == 4056) return 56 * x + 4;   // the table row 56 of the unit 4
    if (x == 4057) return 57 * x + 4;   // the table row 57 of the unit 4
    if (x == 4058) return 58 * x + 4;   // the table row 58 of the unit 4
    if (x == 4059) return 59 * x + 4;   // the table row 59 of the unit 4
    if (x > 100) return 100;
    return x;
}
