// ephdump.cpp - audit helper: print model RA/Dec (h, deg) for each of the 8
// major planets (plus Pluto) at monthly steps over a year range.
// Usage: ephdump START_YYYY MM STOP_YYYY MM   (monthly steps, 00:00 UT)
// Output lines: date YYYY-MM-DD name RAhours DecDeg
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include "planetary_objects.h"
#include "astro_math.h"

int main(int argc, char** argv)
{
    if (argc != 5) { fprintf(stderr, "usage: ephdump SY SM EY EM\n"); return 2; }
    int sy = atoi(argv[1]), sm = atoi(argv[2]), ey = atoi(argv[3]), em = atoi(argv[4]);
    const char* names[] = {"Mercury","Venus","Mars","Jupiter","Saturn","Uranus","Neptune","Pluto"};
    for (int y = sy, m = sm; ; )
    {
        double jd = JulianDate(y, m, 1, 0, 0, 0, 0);
        auto objs = GetPlanetaryObjects(jd);
        for (const char* n : names)
            for (const auto& o : objs)
                if (o.name == n)
                    printf("%04d-%02d-01 %s %.6f %.6f\n", y, m, n,
                           o.rightAscensionHours, o.declinationDegrees);
        if (y == ey && m == em) break;
        if (++m > 12) { m = 1; ++y; }
    }
    return 0;
}
