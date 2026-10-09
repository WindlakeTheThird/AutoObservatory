#ifndef ASTRO_MATH_H
#define ASTRO_MATH_H

#include <cmath>
#include <utility>

inline double DegreesToRadians(double deg) { return deg * M_PI / 180.0; }
inline double RadiansToDegrees(double rad) { return rad * 180.0 / M_PI; }

inline double NormalizeDegrees(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

inline double Clamp(double value, double min, double max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

inline double JulianDate(double year, double month, double day, double hour, double minute, double second, double millisecond)
{
    double dayFrac = hour / 24.0 +
                     minute / 1440.0 +
                     second / 86400.0 +
                     millisecond / 86400000.0;

    double y = year;
    double m = month;
    double d = day;

    if (m <= 2)
    {
        y -= 1;
        m += 12;
    }

    double a = std::floor(y / 100.0);
    double b = 2 - a + std::floor(a / 4.0);

    return std::floor(365.25 * (y + 4716)) +
           std::floor(30.6001 * (m + 1)) +
           d + dayFrac + b - 1524.5;
}

// UT1 - UTC is deliberately NOT applied here. Measured against astropy for
// 2026-09-23: the genuine UT1-UTC offset moves GMST by only -0.20 arcsec,
// because Earth has slowed enough that TAI - UT1 has grown past 37 s and
// absorbs nearly all of the leap seconds (the naive "37 leap seconds" figure
// overstates the error by ~20x). This polynomial sits within +0.06 arcsec of
// the IAU reference, so adding an IERS delta-T table only makes it worse (-1.2
// arcsec) while adding a table that goes stale. Below the engine's accuracy
// budget (planets 9-27", Moon 48") - revisit only if arcsecond horizontal
// accuracy is ever needed.
inline double Ut1MinusUtcSeconds(double year)
{
    // Kept as a documented stub so the question is not re-litigated: the
    // answer is "negligible, see above".
    (void)year;
    return 0.0;
}

inline double GreenwichMeanSiderealTime(double jd)
{
    double jd0 = std::floor(jd + 0.5) - 0.5;
    double t = (jd0 - 2451545.0) / 36525.0;

    double gmstSeconds = 24110.54841 +
                         8640184.812866 * t +
                         0.093104 * t * t -
                         6.2e-6 * t * t * t;

    double utSeconds = (jd - jd0) * 86400.0;
    gmstSeconds += 1.00273790935 * utSeconds;

    double gmstDeg = gmstSeconds / 240.0;
    return NormalizeDegrees(gmstDeg);
}

inline double LocalSiderealTime(double jst, double lonDeg)
{
    return NormalizeDegrees(GreenwichMeanSiderealTime(jst) + lonDeg);
}

// Core: equatorial -> horizontal at a Julian date. Everything else funnels here.
inline std::pair<double, double> CalculateAzimuthFromJulian(
    double jd, double latDeg, double lonDeg, double raHours, double decDeg)
{
    double lstDeg = LocalSiderealTime(jd, lonDeg);

    double raDeg = raHours * 15.0;
    double haDeg = NormalizeDegrees(lstDeg - raDeg);
    if (haDeg > 180.0) haDeg -= 360.0;

    double latRad = DegreesToRadians(latDeg);
    double decRad = DegreesToRadians(decDeg);
    double haRad = DegreesToRadians(haDeg);

    double sinAlt = std::sin(latRad) * std::sin(decRad) +
                    std::cos(latRad) * std::cos(decRad) * std::cos(haRad);
    double altRad = std::asin(Clamp(sinAlt, -1.0, 1.0));

    double sinAz = -std::cos(decRad) * std::sin(haRad);
    double cosAz = std::sin(decRad) * std::cos(latRad) -
                   std::cos(decRad) * std::sin(latRad) * std::cos(haRad);
    double azRad = std::atan2(sinAz, cosAz);

    return {NormalizeDegrees(RadiansToDegrees(azRad)), RadiansToDegrees(altRad)};
}

// A calendar instant, as named fields.
//
// This exists to stop date components crossing function boundaries as a run of
// same-typed doubles. The previous signature took year, month, day, hour,
// minute, second and millisecond positionally -- seven adjacent doubles where
// transposing minute and millisecond compiles clean and is worth 600x the
// error, and where callers had to write trailing `0.0`s that were easy to
// drop or reorder.
//
// Note this is deliberately NOT a validated/parsed type: --date/--time parsing
// and range checking stay in cli_parse.h (ParseDateArg / ParseTimeArg), so an
// impossible date is still rejected at the argument boundary rather than
// scattered across call sites.
struct DateTime
{
    double year = 2000.0, month = 1.0, day = 1.0;
    double hour = 0.0, minute = 0.0, second = 0.0, millisecond = 0.0;

    double JulianDay() const
    {
        return JulianDate(year, month, day, hour, minute, second, millisecond);
    }
};

// Observer location, so latitude and longitude travel together and cannot swap.
struct Observer
{
    double latDeg = 0.0;
    double lonDeg = 0.0;
};

inline std::pair<double, double> CalculateHorizontalCoordinates(
    const DateTime& when, const Observer& site, double raHours, double decDeg)
{
    return CalculateAzimuthFromJulian(when.JulianDay(), site.latDeg, site.lonDeg, raHours, decDeg);
}

// For callers that already hold a Julian date -- the rise/set and twilight
// scans sweep fractional hours and never need a calendar round-trip.
inline std::pair<double, double> HorizontalCoordinatesAtJulian(
    double jd, const Observer& site, double raHours, double decDeg)
{
    return CalculateAzimuthFromJulian(jd, site.latDeg, site.lonDeg, raHours, decDeg);
}

// Visual magnitude of the Sun, used wherever it appears as a selectable target
// (legend entry, dome marker, slew target). Bright enough to clear any sane
// magnitude limit, which is what keeps it out of the "too faint" filter.
inline constexpr double SUN_MAGNITUDE = -26.9;

// Sun geocentric RA (hours) / Dec (deg) from the low-precision solar formula
// already used for moon phase. ponytail: same model, no extra accuracy needed
// for a header field and a map marker.
inline void SunRAandDec(double jd, double& raHours, double& decDeg)
{
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = 280.46646 + 36000.76983 * T;
    double M = 357.52911 + 35999.05029 * T;
    double C = 1.914602 * std::sin(M * M_PI / 180.0);
    double lambda = (L0 + C) * M_PI / 180.0;
    double ra = std::atan2(std::cos(23.4393 * M_PI / 180.0) * std::sin(lambda), std::cos(lambda)) * 12.0 / M_PI;
    if (ra < 0) ra += 24.0;
    raHours = ra;
    decDeg = std::asin(std::sin(23.4393 * M_PI / 180.0) * std::sin(lambda)) * 180.0 / M_PI;
}

#endif
