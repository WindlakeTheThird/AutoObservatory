#ifndef CELESTIAL_OBJECTS_H
#define CELESTIAL_OBJECTS_H

#include <string>
#include <vector>

struct CelestialObject
{
    std::string name;
    double rightAscensionHours;
    double declinationDegrees;
    double magnitude;
    std::string bayer; // Bayer designation (e.g. Alpha_Boo) for stars; empty for clusters/nebulae

    CelestialObject(const std::string& n, double ra, double dec, double mag, const std::string& bayer = "")
        : name(n), rightAscensionHours(ra), declinationDegrees(dec), magnitude(mag), bayer(bayer) {}
};

#endif
