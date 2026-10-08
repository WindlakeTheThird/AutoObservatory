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

inline std::pair<double, double> CalculateHorizontalCoordinates(
    double year, double month, double day, double hour, double minute, double second, double millisecond,
    double observerLatDeg, double observerLonDeg,
    double raHours, double decDeg)
{
    double jd = JulianDate(year, month, day, hour, minute, second, millisecond);
    double lstDeg = LocalSiderealTime(jd, observerLonDeg);

    double raDeg = raHours * 15.0;
    double haDeg = NormalizeDegrees(lstDeg - raDeg);
    if (haDeg > 180.0) haDeg -= 360.0;

    double latRad = DegreesToRadians(observerLatDeg);
    double decRad = DegreesToRadians(decDeg);
    double haRad = DegreesToRadians(haDeg);

    double sinAlt = std::sin(latRad) * std::sin(decRad) +
                    std::cos(latRad) * std::cos(decRad) * std::cos(haRad);
    double altRad = std::asin(Clamp(sinAlt, -1.0, 1.0));

    double sinAz = -std::cos(decRad) * std::sin(haRad);
    double cosAz = std::sin(decRad) * std::cos(latRad) -
                   std::cos(decRad) * std::sin(latRad) * std::cos(haRad);
    double azRad = std::atan2(sinAz, cosAz);

    double azDeg = NormalizeDegrees(RadiansToDegrees(azRad));
    double altDeg = RadiansToDegrees(altRad);

    return {azDeg, altDeg};
}

#endif
