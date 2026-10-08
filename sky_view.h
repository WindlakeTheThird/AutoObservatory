#ifndef SKY_VIEW_H
#define SKY_VIEW_H

// sky_view.h - limited ("zoomed") views of the sky dome, shared by the terminal
// planetarium, its tests, and (mirrored in pas/astrop.pas) the Pascal GUI.
//
// The full sky map is an azimuthal equidistant projection centred on the
// zenith: the angular distance from the zenith (90 - altitude) maps linearly to
// the distance from the centre of the drawing. A limited view generalises the
// same projection to an arbitrary centre (centerAz, centerAlt) and a smaller
// field: the angular distance from the centre maps linearly to the drawing
// radius, and anything farther than fov/2 degrees falls outside the view.
//
// Screen orientation matches the existing planisphere ("looking up"): with the
// default zenith centre, the direction of increasing azimuth is screen-right
// and the direction of increasing altitude is screen-down (toward the centre),
// giving N up, E right, S down, W left. Zooming therefore magnifies a patch of
// the existing map rather than mirroring it.

#include <cmath>
#include <algorithm>
#include "astro_math.h"

struct SkyView
{
    double centerAz  = 0.0;    // deg, [0, 360)
    double centerAlt = 90.0;   // deg, [-90, 90]; 90 = zenith (full-sky default)
    double fov       = 180.0;  // full angular diameter of the circular field, deg
};

// The whole visible sky: zenith centre, hemisphere-wide field. Reproduces the
// original polar projection exactly (see the check in tools/viewcheck.cpp).
inline SkyView FullSkyView() { return SkyView{}; }

// True when the view is not the full-sky zenith default, i.e. when the drawing
// edge is a field boundary rather than the horizon.
inline bool IsLimitedView(const SkyView& v)
{
    return v.fov < 180.0 || v.centerAlt < 90.0;
}

// A field of view must be strictly smaller than the whole visible sky
// (0 < fov < 180); 180 would be the full sky and is what omitting --fov gives.
inline bool ValidFov(double fov) { return fov > 0.0 && fov < 180.0; }

inline bool ValidCenter(double az, double alt)
{
    return az >= 0.0 && az < 360.0 && alt >= -90.0 && alt <= 90.0;
}

// Projects (azDeg, altDeg) into the view. Returns false when the point is
// outside the field of view. On success (u, v) are normalised drawing offsets
// with |(u, v)| <= 1: u > 0 toward increasing azimuth (screen right), v > 0
// screen up. Scale u by 2x the drawing radius for the character aspect ratio
// (chars are ~2x taller than wide) and flip v for screen y (y grows downward).
inline bool ProjectSky(const SkyView& view, double azDeg, double altDeg,
                       double& u, double& v)
{
    u = 0.0; v = 0.0;
    const double A0  = DegreesToRadians(view.centerAz);
    const double h0  = DegreesToRadians(view.centerAlt);
    const double A   = DegreesToRadians(azDeg);
    const double h   = DegreesToRadians(altDeg);
    const double dA  = A - A0;

    // Tangent-plane components at the view centre: x toward increasing azimuth,
    // y toward increasing altitude.
    const double x = std::cos(h) * std::sin(dA);
    const double y = -std::sin(h0) * std::cos(h) * std::cos(dA) + std::sin(h) * std::cos(h0);

    // Spherical law of cosines: angular distance between the centre and the point.
    double cosT = std::sin(h0) * std::sin(h) + std::cos(h0) * std::cos(h) * std::cos(dA);
    cosT = Clamp(cosT, -1.0, 1.0);
    const double sinT = std::sqrt(std::max(0.0, 1.0 - cosT * cosT));
    const double theta = std::atan2(sinT, cosT);              // rad, [0, pi]
    const double halfField = DegreesToRadians(view.fov / 2.0); // rad, > 0
    // A hair of slack: a point exactly on the edge (the horizon in the full-sky
    // view) must be kept, but cos() rounding makes theta land a few ULPs above
    // or below halfField depending on the azimuth.
    if (theta > halfField + 1e-12) return false;              // outside the view

    const double scale = halfField > 0.0 ? theta / halfField : 0.0; // 0..1
    if (sinT < 1e-12) return true;                            // exactly at the centre
    u = scale * (x / sinT);        // toward increasing azimuth = screen right
    v = scale * (-y / sinT);       // planisphere mirror: toward increasing alt = screen down
    return true;
}

#endif
