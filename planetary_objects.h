#ifndef PLANETARY_COMPUTATION_H
#define PLANETARY_COMPUTATION_H

#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>

#define VSOP87_TRUNC_IMPL // single-TU header-only model: tables live here
#include "vsop87_trunc.h"
#include "astro_math.h"

struct PlanetaryObject
{
    std::string name;
    double rightAscensionHours;
    double declinationDegrees;
    double magnitude;
};

inline double KeplerEquation(double M, double e)
{
    double E = M;
    for (int i = 0; i < 20; ++i)
    {
        double dE = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::abs(dE) < 1e-12) break;
    }
    return E;
}

// Precess an equatorial J2000 vector to mean-of-date equatorial coordinates
// using IAU 2006 precession. Built from the IAU 2006 zeta/z/theta polynomial
// angles (Capitaine et al. 2003, arcsec) via the classic 3-rotation matrix
// P = R3(-z) R2(theta) R3(-zeta); accuracy ~0.1" for centuries around J2000.
inline void PrecessJ2000ToDate(double jd, double& x, double& y, double& z)
{
    double t = (jd - 2451545.0) / 36525.0; // Julian centuries from J2000
    double t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;

    // IAU 2006: zeta_A, z_A, theta_A (arcsec)
    double zeta = 2.650545 * t + 0.293086 * t2 + 0.018145 * t3
                - 0.000018 * t4 - 0.0000002 * t5
                + (2306.083227 + 1.0946835 * t + 0.0182032 * t2) * t; // fold t-terms
    // (the polynomial is zeta = 2306.083227 t + 0.293086 t^2 + 0.018145 t^3 ...)
    zeta = 2306.083227 * t + 0.293086 * t2 + 0.018145 * t3
         - 0.000018 * t4 - 0.0000002 * t5;
    double zz = 2306.077181 * t + 1.0927348 * t2 + 0.018182 * t3
             + 0.000018 * t4 - 0.0000002 * t5;
    double theta = 2004.191903 * t - 0.4294934 * t2 - 0.0418225 * t3
                 - 0.000007 * t4 + 0.0000001 * t5;

    double As = zeta / 206264.806247;   // arcsec -> rad
    double Bs = zz / 206264.806247;
    double Cs = theta / 206264.806247;

    double sinA = std::sin(As), cosA = std::cos(As);
    double sinB = std::sin(Bs), cosB = std::cos(Bs);
    double sinC = std::sin(Cs), cosC = std::cos(Cs);

    // R3(-z) R2(theta) R3(-zeta) applied stepwise:
    // R3(-zeta):
    double x1 = x * cosA - y * sinA;
    double y1 = x * sinA + y * cosA;
    double z1 = z;
    // R2(theta). NOTE the sign: the IAU 2006 matrix uses R2(-theta), i.e. this
    // step mixes x/z with MINUS sin(theta). The +sin form gives the transpose
    // and a 2*theta (~1071" at 2026) rotation error. Verified element-by-element
    // against erfa.pmat06; see tools/horzcheck.cpp.
    double x2 = x1 * cosC - z1 * sinC;
    double y2 = y1;
    double z2 = x1 * sinC + z1 * cosC;
    // R3(-z):
    x = x2 * cosB - y2 * sinB;
    y = x2 * sinB + y2 * cosB;
    z = z2;
}

// Precess a J2000 RA (hours) / Dec (deg) pair to mean-of-date.
//
// NEEDED FOR CATALOG OBJECTS ONLY. Rows in celestial_objects.csv are J2000 and
// must be precessed before the horizontal transform: skipping this leaves the
// dome and the mount 9-21 arcmin off, which is larger than the firmware
// DEADBAND_DEG (0.15 deg). Objects returned by GetPlanetaryObjects are already
// mean-of-date (VSOP87 is of-date, Schlyter is of-date) and MUST NOT be passed
// through here -- double-precessing them is the ~20' Moon bug in reverse.
//
// Verified against astropy by tools/ephcheck.cpp (horizontal reference cases)
// and pas/ephcheck_pas.lpr.
inline void PrecessRaDecJ2000ToDate(double jd, double& raHours, double& decDeg)
{
    double ra = raHours * 15.0 * M_PI / 180.0;
    double dec = decDeg * M_PI / 180.0;
    double x = std::cos(dec) * std::cos(ra);
    double y = std::cos(dec) * std::sin(ra);
    double z = std::sin(dec);
    PrecessJ2000ToDate(jd, x, y, z);
    decDeg = std::asin(Clamp(z, -1.0, 1.0)) * 180.0 / M_PI;
    double raDeg = std::atan2(y, x) * 180.0 / M_PI;
    if (raDeg < 0.0) raDeg += 360.0;
    raHours = raDeg / 15.0;
}

inline std::tuple<double, double, double> HeliocentricCoordinates(double a, double e, double N, double i, double w, double M)
{
    double Nrad = N * M_PI / 180.0;
    double irad = i * M_PI / 180.0;
    double wrad = w * M_PI / 180.0;
    double Mrad = M * M_PI / 180.0;

    double E = KeplerEquation(Mrad, e);

    double cosE = std::cos(E);
    double sinE = std::sin(E);

    double xv = a * (cosE - e);
    double yv = a * std::sqrt(1.0 - e * e) * sinE;

    double cosN = std::cos(Nrad);
    double sinN = std::sin(Nrad);
    double cosw = std::cos(wrad);
    double sinw = std::sin(wrad);
    double cosi = std::cos(irad);
    double sini = std::sin(irad);

    double xh = cosN * cosw - sinN * sinw * cosi;
    double yh = sinN * cosw + cosN * sinw * cosi;
    double zh = sinw * sini;

    double x = xv * xh - yv * (cosN * sinw + sinN * cosw * cosi);
    double y = xv * yh - yv * (sinN * sinw - cosN * cosw * cosi);
    double z = xv * zh + yv * (cosw * sini);

    return {x, y, z};
}

inline std::tuple<double, double, double> ComputeMoonGeocentric(double jd)
{
    // Schlyter's lunar elements use day number d measured from 1999-12-31 0h UT
    // (JD 2451543.5), NOT J2000 noon. Using the wrong epoch shifted the Moon ~20 deg.
    double days = jd - 2451543.5;

    double N = 125.1228 - 0.0529538083 * days;
    double i = 5.1454;
    double w = 318.0634 + 0.1643573223 * days;
    double a = 0.002566; // 60.2666 Earth radii (Schlyter) = 383,979 km = 0.002566 AU
    double e = 0.054900;
    double M0 = 115.3654;
    double dM = 13.0649929535;

    double M = std::fmod(M0 + dM * days, 360.0);
    if (M < 0.0) M += 360.0;

    double Mrad = M * M_PI / 180.0;
    double E = Mrad;
    for (int iter = 0; iter < 20; ++iter)
    {
        double dE = (E - e * std::sin(E) - Mrad) / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::abs(dE) < 1e-12) break;
    }

    double cosE = std::cos(E);
    double sinE = std::sin(E);

    double x_orb = a * (cosE - e);
    double y_orb = a * std::sqrt(1.0 - e * e) * sinE;

    double Nrad = N * M_PI / 180.0;
    double irad = i * M_PI / 180.0;
    double wrad = w * M_PI / 180.0;

    double cosN = std::cos(Nrad);
    double sinN = std::sin(Nrad);
    double cosi = std::cos(irad);
    double sini = std::sin(irad);
    double cosw = std::cos(wrad);
    double sinw = std::sin(wrad);

    double x = (cosN * cosw - sinN * sinw * cosi) * x_orb + (-cosN * sinw - sinN * cosw * cosi) * y_orb;
    double y = (sinN * cosw + cosN * sinw * cosi) * x_orb + (-sinN * sinw + cosN * cosw * cosi) * y_orb;
    double z = (sinw * sini) * x_orb + (cosw * sini) * y_orb;

    // Sun mean anomaly (Schlyter's own Sun elements, same epoch as the Moon's).
    double Ms_sun = 356.0470 + 0.9856002585 * days;
    double ws_sun = 282.9404 + 4.70935e-5 * days;     // Sun longitude of perihelion

    // Schlyter's main lunar perturbations (degrees), computed from the same
    // element set so phases are consistent: Ls/Lm mean longitudes, D = Lm - Ls
    // (mean elongation), F = Lm - N (argument of latitude).
    double Ls = Ms_sun + ws_sun;
    double Lm = M + N + w;
    double D  = Lm - Ls;
    double F  = Lm - N;
    double Mm = M;
    double Ms = Ms_sun;

    double lonPert =
        -1.274   * std::sin((Mm - 2.0 * D) * M_PI / 180.0)          // Evection
        + 0.658   * std::sin(2.0 * D * M_PI / 180.0)                 // Variation
        - 0.186   * std::sin(Ms * M_PI / 180.0)                      // Yearly equation
        - 0.059   * std::sin((2.0 * Mm - 2.0 * D) * M_PI / 180.0)
        - 0.057   * std::sin((Mm - 2.0 * D + Ms) * M_PI / 180.0)
        + 0.053   * std::sin((Mm + 2.0 * D) * M_PI / 180.0)
        + 0.046   * std::sin((2.0 * D - Ms) * M_PI / 180.0)
        + 0.041   * std::sin((Mm - Ms) * M_PI / 180.0)
        - 0.035   * std::sin(D * M_PI / 180.0)
        - 0.031   * std::sin((Mm + Ms) * M_PI / 180.0)
        - 0.015   * std::sin((2.0 * F - 2.0 * D) * M_PI / 180.0)
        + 0.011   * std::sin((Mm - 4.0 * D) * M_PI / 180.0);
    double latPert =
        -0.173   * std::sin((F - 2.0 * D) * M_PI / 180.0)
        - 0.055   * std::sin((Mm - F - 2.0 * D) * M_PI / 180.0)
        - 0.046   * std::sin((Mm + F - 2.0 * D) * M_PI / 180.0)
        + 0.033   * std::sin((F + 2.0 * D) * M_PI / 180.0)
        + 0.017   * std::sin((2.0 * Mm + F) * M_PI / 180.0);
    double distPert =
        -0.58    * std::cos((Mm - 2.0 * D) * M_PI / 180.0)          // Earth radii
        - 0.46    * std::cos(2.0 * D * M_PI / 180.0);

    // Rotate the (already Keplerian) ecliptic vector into spherical coords,
    // apply perturbations, and back.
    double lon0 = std::atan2(y, x);            // ecliptic longitude (rad)
    double lat0 = std::atan2(z, std::sqrt(x * x + y * y));
    double r0 = std::sqrt(x * x + y * y + z * z);

    double lon = lon0 + lonPert * M_PI / 180.0;
    double lat = lat0 + latPert * M_PI / 180.0;
    double rm = r0 + distPert * (a / 60.2666); // distPert in Earth radii, a = 60.2666 ER in AU

    double cl = std::cos(lat);
    x = rm * cl * std::cos(lon);
    y = rm * cl * std::sin(lon);
    z = rm * std::sin(lat);

    double eclipticObliquity = 23.4392911111111 * M_PI / 180.0;

    double xEq = x;
    double yEq = y * std::cos(eclipticObliquity) - z * std::sin(eclipticObliquity);
    double zEq = y * std::sin(eclipticObliquity) + z * std::cos(eclipticObliquity);

    // ponytail: no aberration or J2000->date precession here. Schlyter's method
    // already outputs equinox-of-date positions (the model's element epoch makes
    // the distinction moot at 1-2' accuracy), and the Sun-derived velocity vector
    // is inconsistent with the perturbation-corrected geocentric vector. Verified
    // against JPL Horizons: this raw output already lands within ~1-2 arcmin.
    // (Applying both shifted the result ~20' WORSE - double-counting of precession.)

    double r = std::sqrt(xEq * xEq + yEq * yEq + zEq * zEq);
    double ra = std::atan2(yEq, xEq) * 12.0 / M_PI;
    double dec = std::asin(zEq / r) * 180.0 / M_PI;

    double T = (jd - 2451545.0) / 36525.0;
    double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    double M_sun = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
    double M_sun_rad = M_sun * M_PI / 180.0;
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(M_sun_rad)
             + (0.019993 - 0.000101 * T) * std::sin(2.0 * M_sun_rad)
             + 0.000289 * std::sin(3.0 * M_sun_rad);
    double lambda_sun = L0 + C;
    double obliquity = 23.4392911111111 - 0.0130042 * T;
    double oblRad = obliquity * M_PI / 180.0;
    double lambdaRad = lambda_sun * M_PI / 180.0;
    double sunRA = std::atan2(std::cos(oblRad) * std::sin(lambdaRad), std::cos(lambdaRad)) * 12.0 / M_PI;
    double sunDec = std::asin(std::sin(oblRad) * std::sin(lambdaRad)) * 180.0 / M_PI;

    double moonRARad = ra * 15.0 * M_PI / 180.0;
    double sunRARad = sunRA * 15.0 * M_PI / 180.0;
    double moonDecRad = dec * M_PI / 180.0;
    double sunDecRad = sunDec * M_PI / 180.0;

    double cosSep = std::sin(moonDecRad) * std::sin(sunDecRad) +
                    std::cos(moonDecRad) * std::cos(sunDecRad) * std::cos(moonRARad - sunRARad);
    double sep = std::acos(cosSep < -1.0 ? -1.0 : cosSep > 1.0 ? 1.0 : cosSep) * 180.0 / M_PI;

    double phase = (1.0 - std::cos(sep * M_PI / 180.0)) / 2.0;
    double mag = -12.74 + 5.0 * std::log10(std::max(r / 0.002570, 1e-10)) + 5.0 * (1.0 - phase);

    return {ra < 0.0 ? ra + 24.0 : ra, dec, mag};
}

// IAU H-G phase function: illuminated fraction of a minor body at phase
// angle alpha (radians, Sun-object-observer). G <= 0 (major planets) returns
// 1, i.e. no minor-body phase model. Frozen-value regression test:
// tools/hgcheck.cpp + pas/hgcheck_pas.lpr must stay in lockstep.
inline double HGPhase(double G, double alphaRad)
{
    if (G <= 0.0) return 1.0;
    double tanHalf = std::tan(0.5 * alphaRad);
    double phi1 = std::exp(-3.33 * std::pow(tanHalf, 0.63));
    double phi2 = std::exp(-1.87 * std::pow(tanHalf, 1.22));
    return (1.0 - G) * phi1 + G * phi2;
}

// Compute RA/Dec/mag from CSV orbital elements. Local struct so both funcs share it.
struct PCSVElements { std::string name; double N, i, w, a, e, M0; double dN, di, dw, da, de, dM; double G = 0.0; };

inline std::tuple<double, double, double> ComputeRAandDecFromElements(const PCSVElements& p, double magBase_, double jd)
{
    double days = jd - 2451545.0;

    double N = p.N + p.dN * days;
    double i = p.i + p.di * days;
    double w = p.w + p.dw * days;
    double a = p.a + p.da * days;
    double e = p.e + p.de * days;
    double M = std::fmod(p.M0 + p.dM * days, 360.0);
    if (M < 0.0) M += 360.0;

    auto [px, py, pz] = HeliocentricCoordinates(a, e, N, i, w, M);

    // Earth's elements: hard-coded
    double eM = std::fmod(357.529 + 0.98560028 * days, 360.0);
    if (eM < 0.0) eM += 360.0;
    auto [ex, ey, ez] = HeliocentricCoordinates(1.00000, 0.016709, 0.0, 0.0, 102.9372, eM);

    double x = px - ex, y = py - ey, z = pz - ez;

    // Light-time correction: the planet moved since the light left it. Evaluate
    // the planet at (jd - dist/173.15) days (1 AU light-time in days) and use
    // that geocentric vector. 3 iterations converge to <1" for all objects here.
    double distDays = 0.0;
    double lx = 0.0, ly = 0.0, lz = 0.0;
    for (int iter = 0; iter < 3; ++iter)
    {
        double Mlt = std::fmod(p.M0 + p.dM * (days - distDays), 360.0);
        if (Mlt < 0.0) Mlt += 360.0;
        std::tie(lx, ly, lz) = HeliocentricCoordinates(
            p.a + p.da * (days - distDays), p.e + p.de * (days - distDays),
            p.N + p.dN * (days - distDays), p.i + p.di * (days - distDays),
            p.w + p.dw * (days - distDays), Mlt);
        double dx = lx - ex, dy = ly - ey, dz = lz - ez;
        distDays = std::sqrt(dx * dx + dy * dy + dz * dz) / 173.15;
    }
    x = lx - ex; y = ly - ey; z = lz - ez;

    // Annual aberration: add Earth's barycentric velocity vector (in AU/day,
    // from a finite difference of the Earth's heliocentric position over 0.5 d)
    // times k = 0.0057755183 days/AU (1/c) to the geocentric direction. The
    // classical first-order term captures ~99.5% of the ~20.5" effect.
    {
        double eM2 = std::fmod(357.529 + 0.98560028 * (days + 0.5), 360.0);
        if (eM2 < 0.0) eM2 += 360.0;
        auto [ex2, ey2, ez2] = HeliocentricCoordinates(1.00000, 0.016709, 0.0, 0.0, 102.9372, eM2);
        const double k = 0.0057755183; // days/AU (1/c)
        double vx = (ex2 - ex) * k, vy = (ey2 - ey) * k, vz = (ez2 - ez) * k;
        x += vx; y += vy; z += vz;
    }

    double eclipticObliquity = 23.4392911111111 * M_PI / 180.0;
    double xEq = x;
    double yEq = y * std::cos(eclipticObliquity) - z * std::sin(eclipticObliquity);
    double zEq = y * std::sin(eclipticObliquity) + z * std::cos(eclipticObliquity);

    // J2000 mean equator -> mean equator of date (IAU 2006 precession).
    PrecessJ2000ToDate(jd, xEq, yEq, zEq);

    double r = std::sqrt(xEq * xEq + yEq * yEq + zEq * zEq);
    double ra = std::atan2(yEq, xEq) * 12.0 / M_PI;
    double dec = std::asin(zEq / r) * 180.0 / M_PI;

    double rPlanet = std::sqrt(px * px + py * py + pz * pz);
    double dist = std::sqrt(x * x + y * y + z * z);
    double mag = magBase_ + 5.0 * std::log10(std::max(rPlanet * dist, 1e-10));

    if (p.name == "Mercury" || p.name == "Venus")
    {
        double dot = (-lx) * (ex - lx) + (-ly) * (ey - ly) + (-lz) * (ez - lz);
        double cosAlpha = dot / (rPlanet * dist);
        cosAlpha = std::max(-1.0, std::min(1.0, cosAlpha));
        double phase = std::max((1.0 + cosAlpha) / 2.0, 0.001);
        mag -= 2.5 * std::log10(phase);
    }

    // H-G phase function for minor bodies (asteroids, dwarf planets, TNOs).
    // A positive slope G enables the IAU H-G model: with the CSV magnitude
    // column holding the absolute magnitude H, the apparent magnitude stays
    // correct at large phase angles, not just near opposition. The eight major
    // planets carry G = 0 and keep the Schlyter zero-point above.
    if (p.G > 0.0)
    {
        double dot = (-lx) * (ex - lx) + (-ly) * (ey - ly) + (-lz) * (ez - lz);
        double cosAlpha = dot / (rPlanet * dist);
        cosAlpha = std::max(-1.0, std::min(1.0, cosAlpha));
        double alpha = std::acos(cosAlpha);              // phase angle (rad)
        mag -= 2.5 * std::log10(std::max(HGPhase(p.G, alpha), 1e-6));
    }

    return {ra < 0.0 ? ra + 24.0 : ra, dec, mag};
}

// Load orbital elements from CSV (name,a,e,N,i,w,M0,dM,dN,di,dw,da,de,magBase).
// Falls back to the built-in table if the file is missing.

// ---------- VSOP87 truncated series (major planets) ----------
// Generated tables in vsop87_trunc.h; L/B in 1e-8 rad, R in 1e-8 AU, argument
// t in Julian millennia from J2000. Per-term power bytes give each term's
// t-power (0..5); evaluation is a level-wise Horner scheme.

inline void VsopEvaluate(const VsopPlanetData& p, double t,
                         double& L, double& B, double& R)
{
    auto eval = [&](const VsopSeries& s) {
        double acc = 0.0;
        unsigned off = s.offset;
        unsigned end = s.offset + s.count;
        while (off < end)
        {
            // terms are stored in ascending power level; each level's terms are
            // contiguous. L = sum over levels of t^level * (sum of A*cos(B+C*t)).
            int lvl = (int)s.powers[off - s.offset];
            double sum = 0.0;
            do {
                sum += VsopTerms[off][0] * std::cos(VsopTerms[off][1] + VsopTerms[off][2] * t);
                ++off;
            } while (off < end && (int)s.powers[off - s.offset] == lvl);
            double tp = 1.0;
            for (int k = 0; k < lvl; ++k) tp *= t;
            acc += sum * tp;
        }
        return acc;
    };
    L = eval(p.L) / 1e8;
    B = eval(p.B) / 1e8;
    R = eval(p.R) / 1e8;
}

inline const VsopPlanetData& VsopPlanet(unsigned idx) { return VSOP[idx]; }
inline const char* VsopPlanetName(unsigned idx) { return VSOP_NAMES[idx]; }

// Evaluate the truncated VSOP87 series for planet idx (0..7 = Mercury..Neptune):
// returns heliocentric ecliptic L (rad), B (rad), R (AU).
inline std::tuple<double, double, double> VsopHelio(unsigned idx, double t)
{
    const VsopPlanetData& pl = VsopPlanet(idx);
    double L, B, R;
    VsopEvaluate(pl, t, L, B, R);
    return {L, B, R};
}

// Geocentric apparent RA (h) / Dec (deg) / r_planet (AU) / dist (AU) from the
// truncated VSOP87 series. Pipeline: heliocentric ecliptic -> FK5 correction
// (Meeus ch. 32) -> geocentric with 3-iteration light time -> annual aberration
// (Earth velocity by central difference over 0.01 d, k = 1/c in days/AU) ->
// obliquity rotation -> IAU 2006 precession (shared with the element model).
struct VsopState { double ra, dec, rPlanet, dist; };

inline VsopState ComputePlanetVSOP(unsigned idx, double jd)
{
    double t = (jd - 2451545.0) / 365250.0; // Julian millennia
    auto rect = [](double L, double B, double R) {
        return std::tuple<double,double,double>{
            R * std::cos(B) * std::cos(L), R * std::cos(B) * std::sin(L), R * std::sin(B)};
    };

    // FK5 correction (applies to planet and Earth alike; < 0.1")
    auto fk5 = [t](double& L, double& B) {
        double lam = L - (1.397 + 0.00031 * t) * t * M_PI / 180.0;
        L += (-0.09033 + 0.03916 * (std::cos(lam) + std::sin(lam)) * std::tan(B)) / 3600.0 * M_PI / 180.0;
        B += (0.03916 * (std::cos(lam) - std::sin(lam))) / 3600.0 * M_PI / 180.0;
    };

    auto [Lp0, Bp0, Rp] = VsopHelio(idx, t);
    double Lp = Lp0, Bp = Bp0;
    fk5(Lp, Bp);
    auto [px, py, pz] = rect(Lp, Bp, Rp);

    double Le0, Be0, Re;
    std::tie(Le0, Be0, Re) = VsopHelio(2, t);
    double Le = Le0, Be = Be0;
    fk5(Le, Be);
    auto [ex, ey, ez] = rect(Le, Be, Re);

    // Light-time: recompute the planet at jd - dist/173.15 (3 iterations).
    double lx = px, ly = py, lz = pz;
    double distDays = 0.0;
    for (int iter = 0; iter < 3; ++iter)
    {
        auto [L2, B2, R2] = VsopHelio(idx, (jd - distDays - 2451545.0) / 365250.0);
        fk5(L2, B2);
        std::tie(lx, ly, lz) = rect(L2, B2, R2);
        double dx = lx - ex, dy = ly - ey, dz = lz - ez;
        distDays = std::sqrt(dx*dx + dy*dy + dz*dz) / 173.15;
    }
    double x = lx - ex, y = ly - ey, z = lz - ez;
    double dist = std::sqrt(x*x + y*y + z*z);

    // Annual aberration: Earth velocity via central difference over 0.01 d.
    {
        const double k = 0.0057755183; // days/AU
        auto [L3, B3, R3] = VsopHelio(2, t + 0.01 / 365250.0);
        auto [L4, B4, R4] = VsopHelio(2, t - 0.01 / 365250.0);
        auto [x3, y3, z3] = rect(L3, B3, R3);
        auto [x4, y4, z4] = rect(L4, B4, R4);
        x += (x3 - x4) / 0.01 * k;
        y += (y3 - y4) / 0.01 * k;
        z += (z3 - z4) / 0.01 * k;
    }

    // VSOP87 series are referred to the mean dynamical ecliptic and equinox of
    // DATE (the FK5 correction above maps dynamical -> FK5 equinox of date),
    // so NO precession here - just rotate with the mean obliquity of date.
    double Tc = (jd - 2451545.0) / 36525.0; // Julian centuries
    double epsSec = 84381.448 - 46.8150 * Tc - 0.00059 * Tc * Tc + 0.001813 * Tc * Tc * Tc;
    double eclipticObliquity = epsSec / 3600.0 * M_PI / 180.0;
    double xEq = x;
    double yEq = y * std::cos(eclipticObliquity) - z * std::sin(eclipticObliquity);
    double zEq = y * std::sin(eclipticObliquity) + z * std::cos(eclipticObliquity);

    double r = std::sqrt(xEq*xEq + yEq*yEq + zEq*zEq);
    double ra = std::atan2(yEq, xEq) * 12.0 / M_PI;
    if (ra < 0.0) ra += 24.0;
    double dec = std::asin(std::max(-1.0, std::min(1.0, zEq / r))) * 180.0 / M_PI;

    double rPlanet = std::sqrt(lx*lx + ly*ly + lz*lz);
    return {ra, dec, rPlanet, dist};
}


// Schlyter/calibrated magnitude zero-points for the 8 major planets (kept in
// the CSV H column too, for reference and non-VSOP consumers).
inline double PlanetMagBase(const std::string& name)
{
    if (name == "Mercury") return -0.42;
    if (name == "Venus")   return -4.40;
    if (name == "Mars")    return -1.52;
    if (name == "Jupiter") return -9.40;
    if (name == "Saturn")  return -8.88;
    if (name == "Uranus")  return -7.19;
    if (name == "Neptune") return -6.87;
    return 0.0;
}

inline std::vector<PlanetaryObject> GetPlanetaryObjects(double jd, const std::string& csvPath)
{
    std::vector<PlanetaryObject> planets;

    // The 8 major planets come from the truncated VSOP87 series (arcsec-level
    // accuracy); their CSV rows, if present, are skipped.
    for (unsigned i = 0; i < 8; ++i)
    {
        if (i == 2) continue; // index 2 = Earth (the observer)
        const char* nm = VSOP_NAMES[i];
        auto [ra, dec, rPlanet, dist] = ComputePlanetVSOP(i, jd);
        double mag = PlanetMagBase(nm) + 5.0 * std::log10(std::max(rPlanet * dist, 1e-10));
        if (std::string(nm) == "Mercury" || std::string(nm) == "Venus")
        {
            // Lambert phase term: alpha = angle Sun-planet-observer; use the
            // Earth/planet heliocentric vectors at the light-time epoch.
            double t = (jd - 2451545.0) / 365250.0;
            auto [Lpl, Bpl, Rpl] = VsopHelio(i, (jd - dist / 173.15 - 2451545.0) / 365250.0);
            auto [Lel, Bel, Rel] = VsopHelio(2, t);
            double pxx = Rpl*std::cos(Bpl)*std::cos(Lpl), pyy = Rpl*std::cos(Bpl)*std::sin(Lpl), pzz = Rpl*std::sin(Bpl);
            double exx = Rel*std::cos(Bel)*std::cos(Lel), eyy = Rel*std::cos(Bel)*std::sin(Lel), ezz = Rel*std::sin(Bel);
            double dot = (-pxx)*(exx - pxx) + (-pyy)*(eyy - pyy) + (-pzz)*(ezz - pzz);
            double rp = std::sqrt(pxx*pxx + pyy*pyy + pzz*pzz);
            double dl = std::sqrt((exx-pxx)*(exx-pxx) + (eyy-pyy)*(eyy-pyy) + (ezz-pzz)*(ezz-pzz));
            double cosA = std::max(-1.0, std::min(1.0, dot / (rp * dl)));
            double phase = std::max((1.0 + cosA) / 2.0, 0.001);
            mag -= 2.5 * std::log10(phase);
        }
        PlanetaryObject p;
        p.name = nm; p.rightAscensionHours = ra; p.declinationDegrees = dec; p.magnitude = mag;
        planets.push_back(p);
    }

    std::ifstream file(csvPath);
    // Was `return planets` on a missing file, which silently produced a sky
    // containing only the Moon. The documented exit-code convention puts a
    // missing data file at exit 1, so fail loudly instead.
    if (!file.is_open())
        throw std::runtime_error("cannot open " + csvPath);

    // CSV format (see header comment block in planetary_objects.csv):
    // line 1 = column names, later lines starting with '#' = comments (skipped).
    std::string line;
    bool first = true;
    int lineno = 0;
    while (std::getline(file, line))
    {
        ++lineno;
        if (first) { first = false; continue; }
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("name,", 0) == 0) continue;   // column-name row
        std::stringstream ss(line);
        std::string f[15];
        int cols = 0;
        for (int k = 0; k < 15; ++k)
        {
            if (!std::getline(ss, f[k], ',')) break;
            cols = k + 1;
        }
        if (cols < 14)
        {
            throw std::runtime_error(csvPath + ":" + std::to_string(lineno) +
                                     ": expected 14 columns, got " + std::to_string(cols));
        }
        try
        {
            PCSVElements pe;
            pe.name = f[0];
            pe.a = std::stod(f[1]); pe.e = std::stod(f[2]); pe.N = std::stod(f[3]);
            pe.i = std::stod(f[4]); pe.w = std::stod(f[5]); pe.M0 = std::stod(f[6]);
            pe.dM = std::stod(f[7]); pe.dN = std::stod(f[8]); pe.di = std::stod(f[9]);
            pe.dw = std::stod(f[10]); pe.da = std::stod(f[11]); pe.de = std::stod(f[12]);
            pe.G = (cols >= 15) ? std::stod(f[14]) : 0.0;
            if (pe.name == "Mercury" || pe.name == "Venus" || pe.name == "Earth" ||
                pe.name == "Mars" || pe.name == "Jupiter" || pe.name == "Saturn" ||
                pe.name == "Uranus" || pe.name == "Neptune")
                continue; // major planets come from VSOP87 above
            auto [ra, dec, mag] = ComputeRAandDecFromElements(pe, std::stod(f[13]), jd);
            PlanetaryObject p;
            p.name = pe.name; p.rightAscensionHours = ra; p.declinationDegrees = dec; p.magnitude = mag;
            planets.push_back(p);
        }
        catch (const std::exception& e)
        {
            // Was `catch (...) { continue; }`, which silently dropped the row and
            // produced a sky missing whole bodies with no diagnostic. Name the
            // file and line so a bad row is findable, and let the caller decide.
            throw std::runtime_error(csvPath + ":" + std::to_string(lineno) +
                                     ": malformed orbital-element row (" + e.what() + ")");
        }
    }

    // Moon is not in the CSV (special model); always append it.
    PlanetaryObject moon; moon.name = "Moon";
    auto [ra, dec, mag] = ComputeMoonGeocentric(jd);
    moon.rightAscensionHours = ra; moon.declinationDegrees = dec; moon.magnitude = mag;
    planets.push_back(moon);
    return planets;
}

inline std::vector<PlanetaryObject> GetPlanetaryObjects(double jd)
{
    return GetPlanetaryObjects(jd, "planetary_objects.csv");
}

inline bool IsOccultedByMoon(double moonRA, double moonDec, double objRA, double objDec)
{
    double moonRARad = moonRA * 15.0 * M_PI / 180.0;
    double objRARad = objRA * 15.0 * M_PI / 180.0;
    double moonDecRad = moonDec * M_PI / 180.0;
    double objDecRad = objDec * M_PI / 180.0;

    double cosSep = std::sin(moonDecRad) * std::sin(objDecRad) +
                    std::cos(moonDecRad) * std::cos(objDecRad) * std::cos(moonRARad - objRARad);

    double sep = std::acos(cosSep < -1.0 ? -1.0 : cosSep > 1.0 ? 1.0 : cosSep) * 180.0 / M_PI;

    double moonAngularRadius = 0.25;
    return sep < moonAngularRadius;
}

#endif
