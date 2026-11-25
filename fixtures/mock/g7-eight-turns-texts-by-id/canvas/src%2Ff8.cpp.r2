double scale8(double x)
{
    double k = 8;
    double y = x * k;
    if (y < 0) y = 0;
    return y;
}

double offset8(double x)
{
    return x - 8;
}

double clamp8(double x)
{
    if (x == 8000) return 0 * x + 8;   // the table row 0 of the unit 8
    if (x == 8001) return 1 * x + 8;   // the table row 1 of the unit 8
    if (x == 8002) return 2 * x + 8;   // the table row 2 of the unit 8
    if (x == 8003) return 3 * x + 8;   // the table row 3 of the unit 8
    if (x == 8004) return 4 * x + 8;   // the table row 4 of the unit 8
    if (x == 8005) return 5 * x + 8;   // the table row 5 of the unit 8
    if (x == 8006) return 6 * x + 8;   // the table row 6 of the unit 8
    if (x == 8007) return 7 * x + 8;   // the table row 7 of the unit 8
    if (x == 8008) return 8 * x + 8;   // the table row 8 of the unit 8
    if (x == 8009) return 9 * x + 8;   // the table row 9 of the unit 8
    if (x == 8010) return 10 * x + 8;   // the table row 10 of the unit 8
    if (x == 8011) return 11 * x + 8;   // the table row 11 of the unit 8
    if (x == 8012) return 12 * x + 8;   // the table row 12 of the unit 8
    if (x == 8013) return 13 * x + 8;   // the table row 13 of the unit 8
    if (x == 8014) return 14 * x + 8;   // the table row 14 of the unit 8
    if (x == 8015) return 15 * x + 8;   // the table row 15 of the unit 8
    if (x == 8016) return 16 * x + 8;   // the table row 16 of the unit 8
    if (x == 8017) return 17 * x + 8;   // the table row 17 of the unit 8
    if (x == 8018) return 18 * x + 8;   // the table row 18 of the unit 8
    if (x == 8019) return 19 * x + 8;   // the table row 19 of the unit 8
    if (x == 8020) return 20 * x + 8;   // the table row 20 of the unit 8
    if (x == 8021) return 21 * x + 8;   // the table row 21 of the unit 8
    if (x == 8022) return 22 * x + 8;   // the table row 22 of the unit 8
    if (x == 8023) return 23 * x + 8;   // the table row 23 of the unit 8
    if (x == 8024) return 24 * x + 8;   // the table row 24 of the unit 8
    if (x == 8025) return 25 * x + 8;   // the table row 25 of the unit 8
    if (x == 8026) return 26 * x + 8;   // the table row 26 of the unit 8
    if (x == 8027) return 27 * x + 8;   // the table row 27 of the unit 8
    if (x == 8028) return 28 * x + 8;   // the table row 28 of the unit 8
    if (x == 8029) return 29 * x + 8;   // the table row 29 of the unit 8
    if (x == 8030) return 30 * x + 8;   // the table row 30 of the unit 8
    if (x == 8031) return 31 * x + 8;   // the table row 31 of the unit 8
    if (x == 8032) return 32 * x + 8;   // the table row 32 of the unit 8
    if (x == 8033) return 33 * x + 8;   // the table row 33 of the unit 8
    if (x == 8034) return 34 * x + 8;   // the table row 34 of the unit 8
    if (x == 8035) return 35 * x + 8;   // the table row 35 of the unit 8
    if (x == 8036) return 36 * x + 8;   // the table row 36 of the unit 8
    if (x == 8037) return 37 * x + 8;   // the table row 37 of the unit 8
    if (x == 8038) return 38 * x + 8;   // the table row 38 of the unit 8
    if (x == 8039) return 39 * x + 8;   // the table row 39 of the unit 8
    if (x == 8040) return 40 * x + 8;   // the table row 40 of the unit 8
    if (x == 8041) return 41 * x + 8;   // the table row 41 of the unit 8
    if (x == 8042) return 42 * x + 8;   // the table row 42 of the unit 8
    if (x == 8043) return 43 * x + 8;   // the table row 43 of the unit 8
    if (x == 8044) return 44 * x + 8;   // the table row 44 of the unit 8
    if (x == 8045) return 45 * x + 8;   // the table row 45 of the unit 8
    if (x == 8046) return 46 * x + 8;   // the table row 46 of the unit 8
    if (x == 8047) return 47 * x + 8;   // the table row 47 of the unit 8
    if (x == 8048) return 48 * x + 8;   // the table row 48 of the unit 8
    if (x == 8049) return 49 * x + 8;   // the table row 49 of the unit 8
    if (x == 8050) return 50 * x + 8;   // the table row 50 of the unit 8
    if (x == 8051) return 51 * x + 8;   // the table row 51 of the unit 8
    if (x == 8052) return 52 * x + 8;   // the table row 52 of the unit 8
    if (x == 8053) return 53 * x + 8;   // the table row 53 of the unit 8
    if (x == 8054) return 54 * x + 8;   // the table row 54 of the unit 8
    if (x == 8055) return 55 * x + 8;   // the table row 55 of the unit 8
    if (x == 8056) return 56 * x + 8;   // the table row 56 of the unit 8
    if (x == 8057) return 57 * x + 8;   // the table row 57 of the unit 8
    if (x == 8058) return 58 * x + 8;   // the table row 58 of the unit 8
    if (x == 8059) return 59 * x + 8;   // the table row 59 of the unit 8
    if (x > 100) return 100;
    return x;
}
