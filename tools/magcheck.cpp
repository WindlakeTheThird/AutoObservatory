// magcheck.cpp - regression check for the WMM magnetic-declination path.
//
// Magnetic declination is added to every commanded azimuth on the mount, and a
// wrong value is written into the sketch's EEPROM by `calibrate`. It is silent
// when wrong: the mount simply points N degrees off, forever.
//
// This pins declination at four sites spread across the globe (including a
// strongly WESTERN declination, which is the case a sign error survives) and
// also pins the wmm_init return-code convention. That convention is not
// documented in wmm_mag.h, and calibrate.cpp once tested it as `== 0` when
// success is NON-ZERO -- so init always "failed", declination silently stayed
// 0.0, and `DECL:0.0000` was stored, costing ~3.8 deg of azimuth at 45N 10E.
//
// Reference values: NOAA/NCEI WMM2025, decimal year 2026.72, sea level.

#include <cstdio>
#include <cmath>
#include "wmm_mag.h"

struct Site { const char* name; double lat, lon; double expect; double tol; };

// Tolerances are ~0.4 deg: generous against WMM epoch/altitude differences
// between sources, but far tighter than the ~3.8 deg the inverted init test
// produced, so that regression cannot slip through.
static const Site sites[] = {
    { "Greenwich", 51.4779,  -0.0015,   1.24, 0.4 },
    { "Paris",     48.8566,   2.3522,   2.08, 0.4 },
    { "45N 10E",   45.0000,  10.0000,   3.84, 0.4 },
    { "Boston",    42.3601, -71.0589, -13.93, 0.4 },  // western: catches a sign error
    { "Sydney",   -33.8688, 151.2093,  12.83, 0.4 },
};

int main()
{
    int failures = 0;

    // 1. The return-code convention itself. wmm_mag.h does not document it,
    //    which is how calibrate.cpp and main.cpp drifted apart.
    int rcGood = wmm_init("WMM2025.COF");
    int rcBad  = wmm_init("definitely-not-a-model-file.COF");
    if (rcGood != 0)
    {
        printf("ok   wmm_init returns NON-ZERO on success (%d)\n", rcGood);
    }
    else
    {
        printf("FAIL wmm_init returned 0 for a valid model: success is NON-zero,\n"
               "     but calibrate.cpp once tested `== 0`. Anyone re-reading this\n"
               "     as '0 means success' reintroduces a silent ~4 deg azimuth error.\n");
        ++failures;
    }
    if (rcBad == 0)
    {
        printf("ok   wmm_init returns 0 for a missing model\n");
    }
    else
    {
        printf("FAIL wmm_init returned %d (expected 0) for a missing model file\n", rcBad);
        ++failures;
    }

    // 2. Declination at known sites.
    wmm_init("WMM2025.COF");
    printf("== WMM declination vs reference (WMM2025, 2026.72) ==\n");
    for (const Site& s : sites)
    {
        double decl, incl, F, GV, X, Y, Z, H;
        int rc = wmm_point(0.0, s.lat, s.lon, 2026.72, &decl, &incl, &F, &GV, &X, &Y, &Z, &H);
        if (rc != 0)
        {
            printf("FAIL %-9s wmm_point returned %d\n", s.name, rc);
            ++failures;
            continue;
        }
        double err = std::fabs(decl - s.expect);
        if (err <= s.tol)
            printf("ok   %-9s decl %+8.4f  expected %+7.2f  err %.3f (tol %.2f)\n",
                   s.name, decl, s.expect, err, s.tol);
        else
        {
            printf("FAIL %-9s decl %+8.4f  expected %+7.2f  err %.3f > tol %.2f\n",
                   s.name, decl, s.expect, err, s.tol);
            ++failures;
        }
    }

    if (failures)
    {
        printf("\n%d magnetic-declination check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll magnetic-declination checks passed.\n");
    return 0;
}