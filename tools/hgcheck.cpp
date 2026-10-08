// hgcheck.cpp - frozen-value unit test for the H-G phase function.
//
// Pins HGPhase() in planetary_objects.h against a fixed table of (G, phase
// angle, illuminated fraction) values. The identical table is pinned in the
// Pascal port (pas/hgcheck_pas.lpr): if the two implementations ever diverge,
// one of these checks fails. Values are the mathematical definition of the IAU
// H-G model evaluated once by hand - they do not depend on the CSV or the
// ephemeris, so any change to HGPhase() in either language trips a failure.
//
// Build & run via:  make check-hg
#include <cstdio>
#include <cmath>
#include "planetary_objects.h"

struct HGCase { double G, alphaDeg, phi; };

static const HGCase cases[] = {
    // alpha = 0: Phi1 = Phi2 = 1 -> Phi = 1 regardless of G
    {0.05,  0.0, 1.000000},
    {0.15,  0.0, 1.000000},
    {0.32,  0.0, 1.000000},
    // G = 0.15 (standard TNO slope) at several phase angles
    {0.15,  1.0, 0.867725},
    {0.15,  3.0, 0.754410},
    {0.15,  6.0, 0.648057},
    {0.15, 10.0, 0.551047},
    {0.15, 20.0, 0.398244},
    {0.15, 40.0, 0.232982},
    {0.15, 80.0, 0.076252},
    {0.15,120.0, 0.011557},
    // other slopes: low-G (asteroid-like, Phi1-weighted) and high-G
    {0.05, 10.0, 0.508968},
    {0.05, 40.0, 0.192170},
    {0.32, 10.0, 0.622580},
    {0.32, 40.0, 0.302362},
    {0.11, 20.0, 0.379411},
    {0.28,  6.0, 0.694246},
    // G <= 0 must short-circuit to 1 (major planets: no minor-body model)
    {0.00, 20.0, 1.000000},
    {-0.10, 40.0, 1.000000},
};

int main()
{
    int failures = 0;
    for (const HGCase& c : cases)
    {
        double phi = HGPhase(c.G, c.alphaDeg * M_PI / 180.0);
        double err = std::fabs(phi - c.phi);
        bool ok = err <= 5e-5;
        if (!ok) ++failures;
        printf("%s G=%.2f alpha=%5.1f  phi=%.6f expect %.6f  (err %.2e)\n",
               ok ? "ok  " : "FAIL", c.G, c.alphaDeg, phi, c.phi, err);
    }
    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nAll H-G phase checks passed.\n");
    return 0;
}
