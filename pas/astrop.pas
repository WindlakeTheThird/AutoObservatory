unit astrop;

{$mode objfpc}{$H+}

// Astro math ported 1:1 from astro_math.h / planetary_objects.h / planetarium.cpp
// (JulianDate, GMST, az/alt, IAU2006 precession, Kepler, light-time + aberration,
// Schlyter Moon with perturbations, sun RA/Dec, moon illumination + phase name,
// CSV loaders for stars and planetary elements).

interface

uses
  Classes, SysUtils, Math;

type
  TPlanetaryObject = record
    Name: string;
    RAHours: Double;    // RA in hours, 0..24
    DecDeg: Double;
    Magnitude: Double;
  end;

  TCelestialObject = record
    Name: string;
    RAHours: Double;
    DecDeg: Double;
    Magnitude: Double;
    Bayer: string; // Bayer designation (e.g. Alpha_Boo) for stars; empty for clusters/nebulae
  end;

  // A limited ("zoomed") view of the sky dome. Mirrors SkyView in sky_view.h:
  // the default (zenith centre, fov 180) is the full visible sky and
  // reproduces the original polar projection exactly.
  TSkyView = record
    CenterAz: Double;   // deg, [0, 360)
    CenterAlt: Double;  // deg, [-90, 90]; 90 = zenith
    Fov: Double;        // full angular diameter of the field, deg
  end;

function DegToRad(const D: Double): Double; inline;
function RadToDeg(const R: Double): Double; inline;
function NormalizeDeg(const D: Double): Double;
function JulianDate(Y, Mo, D, H, Mi, S: Double): Double;
function GreenwichMeanSiderealTime(JD: Double): Double;
function LocalSiderealTime(JD, LonDeg: Double): Double;
// A calendar instant and a site as named fields, so date components never
// cross a call boundary as a run of same-typed doubles (transposing minute and
// second compiles clean and is worth 600x the error). Validation of impossible
// dates stays in ParseDateArg/ParseTimeArg, at the argument boundary.
type
  TUtcDateTime = record
    Year, Month, Day: Double;
    Hour, Minute, Second: Double;
  end;

  TObserver = record
    LatDeg, LonDeg: Double;
  end;

procedure HorizontalCoords(Y, Mo, D, H, Mi, S, LatDeg, LonDeg, RAHours, DecDeg: Double;
  out AzDeg, AltDeg: Double);
procedure HorizontalCoordsAt(const When: TUtcDateTime; const Site: TObserver;
  RAHours, DecDeg: Double; out AzDeg, AltDeg: Double);
function JulianDayOf(const When: TUtcDateTime): Double;
function ObserverAt(LatDeg, LonDeg: Double): TObserver;
function UtcDateTimeAt(Y, Mo, D, H, Mi, S: Double): TUtcDateTime;
procedure HorizontalCoordsAtJulian(JD: Double; const Site: TObserver;
  RAHours, DecDeg: Double; out AzDeg, AltDeg: Double);
procedure PrecessRaDecJ2000ToDate(JD: Double; var RAHours, DecDeg: Double);
procedure JDToCalendar(JD: Double; out Y, Mo, D, H, Mi, S: Double);
procedure SunRAandDec(JD: Double; out RAHours, DecDeg: Double);
  // Visual magnitude of the Sun, used wherever it appears as a selectable target
  // (legend entry, dome marker, slew target). Mirrors SUN_MAGNITUDE in
  // astro_math.h. Bright enough to clear any sane magnitude limit.
  const
    SunMagnitude = -26.9;
  procedure ComputeMoonGeocentric(JD: Double; out RAHours, DecDeg, Magnitude: Double);
procedure ComputePlanetFromElements(const Elem: array of Double; MagBase, G, JD: Double;
  const ObjName: string;
  out RAHours, DecDeg, Magnitude: Double); // Elem = a,e,N,i,w,M0,dM,dN,di,dw,da,de
function HGPhase(G, AlphaRad: Double): Double;
  // IAU H-G phase function: illuminated fraction at phase angle AlphaRad.
  // G <= 0 (major planets) returns 1. Frozen-value test: tools/hgcheck.cpp
  // and pas/hgcheck_pas.lpr pin the same table so the ports cannot diverge.
function MoonIllumination(MoonRAHours, MoonDecDeg, JD: Double): Double;
function MoonPhaseName(MoonRAHours, MoonDecDeg, JD, Illum: Double): string;
function LoadStars(const CsvPath: string): specialize TArray<TCelestialObject>;
function LoadPlanets(const CsvPath: string; JD: Double): specialize TArray<TPlanetaryObject>;

// --- truncated VSOP87 series for the 8 major planets (mirror of the C++
// path in planetary_objects.h; tables in vsop87_trunc.inc). Positions only:
// magnitudes keep the Schlyter zero-points.
function VsopPlanetName(I: Integer): string;
procedure ComputePlanetVSOP(I: Integer; JD: Double;
  out RAHours, DecDeg, RPlanet, Dist: Double);
function TwilightState(SunAltDeg: Double): string;
procedure FindSunEvents(Y, Mo, D, LatDeg, LonDeg, ThresholdDeg: Double;
  out RiseH, SetH: Double); // UTC hours; -1 when no event that day

// Resolve a user-typed object name against ANames the same way the C++
// object_name.h does: case-insensitive exact match, then a unique prefix
// ("arctur" -> "Arcturus"), then a unique substring. On success returns the
// index into ANames and sets ACanonical. Returns -1 when nothing matches
// (ACandidates empty) or when several do (ACandidates = the matches, so
// callers can list them instead of guessing).
function ResolveName(const ANames: array of string; const AQuery: string;
  out ACanonical: string; out ACandidates: specialize TArray<string>): Integer;

// Strict --date/--time parsing, mirroring the C++ cli_parse.h: exact format
// (YYYY-MM-DD, HH:MM or HH:MM:SS), real calendar dates (leap years honored),
// range-checked fields. Set AErrMsg to '' on success, to a message otherwise;
// the caller prints it and halts. Ported so the GUI rejects the same inputs
// as planetarium/autoobservatory (Feb 30, hour 25, wrong separators).
function ParseDateArg(const AValue: string; out Y, Mo, D: Double; out AErrMsg: string): Boolean;
function ParseTimeArg(const AValue: string; out H, Mi, S: Double; out AErrMsg: string): Boolean;

// --- limited views of the sky (mirror of sky_view.h) --------------------------
// Azimuthal equidistant projection about an arbitrary view centre: the angular
// distance from (CenterAz, CenterAlt) maps linearly to the drawing radius and
// anything farther than Fov/2 degrees is outside the view. Screen orientation
// matches the planisphere (N up, E right at the full-sky zenith centre).
function FullSkyView: TSkyView;
function IsLimitedView(const V: TSkyView): Boolean;
function ValidFov(const Fov: Double): Boolean;   // 0 < fov < 180 (smaller than a full sky)
function ValidCenter(const Az, Alt: Double): Boolean; // az [0,360), alt [-90,90]

// --- rectangular az/alt window (the "between certain azimuth and altitude"
// view mode): a straight-edged patch of the dome bounded by two azimuths and
// two altitudes, rather than a circular field. Shortest arc on the az axis,
// so a window may straddle north (az 350 to az 20).
type
  TRectWindow = record
    Az1, Az2: Double;   // deg, [0,360); the window sweeps Az1 -> Az2 the short way
    Alt1, Alt2: Double; // deg, [-90,90]; Alt1 < Alt2
  end;

function ValidRectWindow(const W: TRectWindow): Boolean;
function RectAzDiff(const FromAz, ToAz: Double): Double; // signed shortest az distance
function InRectWindow(const W: TRectWindow; const AzDeg, AltDeg: Double): Boolean;
// Returns False when (AzDeg, AltDeg) is outside the field. On success (U, VOut)
// are normalised drawing offsets, |(U, VOut)| <= 1: U > 0 = screen right,
// VOut > 0 = screen up. Scale U by 2x the drawing radius for the character
// aspect ratio and flip VOut for screen y (y grows downward).
function ProjectSky(const V: TSkyView; const AzDeg, AltDeg: Double;
  out U, VOut: Double): Boolean;

implementation

{$I vsop87_trunc.inc}

function DegToRad(const D: Double): Double;
begin
  Result := D * Pi / 180.0;
end;

function RadToDeg(const R: Double): Double;
begin
  Result := R * 180.0 / Pi;
end;

function NormalizeDeg(const D: Double): Double;
begin
  Result := FMod(D, 360.0);
  if Result < 0 then Result += 360.0;
end;

function FullSkyView: TSkyView;
begin
  Result.CenterAz := 0.0;
  Result.CenterAlt := 90.0;
  Result.Fov := 180.0;
end;

function IsLimitedView(const V: TSkyView): Boolean;
begin
  Result := (V.Fov < 180.0) or (V.CenterAlt < 90.0);
end;

function ValidFov(const Fov: Double): Boolean;
begin
  Result := (Fov > 0.0) and (Fov < 180.0);
end;

function ValidCenter(const Az, Alt: Double): Boolean;
begin
  Result := (Az >= 0.0) and (Az < 360.0) and (Alt >= -90.0) and (Alt <= 90.0);
end;

function ValidRectWindow(const W: TRectWindow): Boolean;
begin
  Result := (W.Az1 >= 0.0) and (W.Az1 < 360.0)
        and (W.Az2 >= 0.0) and (W.Az2 < 360.0)
        and (W.Alt1 >= -90.0) and (W.Alt1 <= 90.0)
        and (W.Alt2 >= -90.0) and (W.Alt2 <= 90.0)
        and (W.Alt1 < W.Alt2);
end;

// Signed shortest azimuth distance FromAz -> ToAz in (-180, 180]: 10 deg for
// 350 -> 20 (a window straddling north), -10 deg for 20 -> 350.
function RectAzDiff(const FromAz, ToAz: Double): Double;
var
  D: Double;
begin
  D := NormalizeDeg(ToAz - FromAz);
  if D > 180.0 then D := D - 360.0;
  Result := D;
end;

function InRectWindow(const W: TRectWindow; const AzDeg, AltDeg: Double): Boolean;
var
  D1, D2: Double;
begin
  if (AltDeg < W.Alt1) or (AltDeg > W.Alt2) then Exit(False);
  D1 := RectAzDiff(W.Az1, AzDeg);
  D2 := RectAzDiff(W.Az1, W.Az2);
  if D2 = 0.0 then Exit(True); // degenerate Az1 = Az2: the whole alt band
  // inside when the point lies between Az1 and Az2 along the same short arc
  Result := (D1 >= 0.0) = (D2 >= 0.0);
  if Result then
    Result := Abs(D1) <= Abs(D2);
end;

function ProjectSky(const V: TSkyView; const AzDeg, AltDeg: Double;
  out U, VOut: Double): Boolean;
var
  A0, h0, A, h, dA, X, Y, CosT, SinT, Theta, HalfField, Scale: Double;
begin
  U := 0.0; VOut := 0.0;
  A0 := DegToRad(V.CenterAz);
  h0 := DegToRad(V.CenterAlt);
  A  := DegToRad(AzDeg);
  h  := DegToRad(AltDeg);
  dA := A - A0;

  // Tangent-plane components at the view centre: x toward increasing azimuth,
  // y toward increasing altitude.
  X := Cos(h) * Sin(dA);
  Y := -Sin(h0) * Cos(h) * Cos(dA) + Sin(h) * Cos(h0);

  // Spherical law of cosines: angular distance between centre and point.
  CosT := EnsureRange(Sin(h0) * Sin(h) + Cos(h0) * Cos(h) * Cos(dA), -1.0, 1.0);
  SinT := Sqrt(Max(0.0, 1.0 - CosT * CosT));
  Theta := ArcTan2(SinT, CosT);
  HalfField := DegToRad(V.Fov / 2.0);
  // A hair of slack: a point exactly on the edge (the horizon in the full-sky
  // view) must be kept despite cos() rounding.
  if Theta > HalfField + 1e-12 then Exit(False);

  if HalfField > 0.0 then Scale := Theta / HalfField else Scale := 0.0;
  if SinT < 1e-12 then Exit(True); // exactly at the centre
  U := Scale * (X / SinT);         // toward increasing azimuth = screen right
  VOut := Scale * (-Y / SinT);     // planisphere mirror: toward increasing alt = screen down
  Result := True;
end;

function HGPhase(G, AlphaRad: Double): Double;
var
  TanHalf, Phi1, Phi2: Double;
begin
  if G <= 0.0 then Exit(1.0);
  TanHalf := Tan(0.5 * AlphaRad);
  Phi1 := Exp(-3.33 * Power(TanHalf, 0.63));
  Phi2 := Exp(-1.87 * Power(TanHalf, 1.22));
  Result := (1.0 - G) * Phi1 + G * Phi2;
end;

function JulianDate(Y, Mo, D, H, Mi, S: Double): Double;
var
  DayFrac, A, B: Double;
begin
  DayFrac := H / 24.0 + Mi / 1440.0 + S / 86400.0;
  if Mo <= 2 then
  begin
    Y -= 1;
    Mo += 12;
  end;
  A := Floor(Y / 100.0);
  B := 2 - A + Floor(A / 4.0);
  Result := Floor(365.25 * (Y + 4716)) + Floor(30.6001 * (Mo + 1)) + D + DayFrac + B - 1524.5;
end;

function GreenwichMeanSiderealTime(JD: Double): Double;
var
  JD0, T, GmstSec, UtSec: Double;
begin
  JD0 := Floor(JD + 0.5) - 0.5;
  T := (JD0 - 2451545.0) / 36525.0;
  GmstSec := 24110.54841 + 8640184.812866 * T + 0.093104 * T * T - 6.2e-6 * T * T * T;
  UtSec := (JD - JD0) * 86400.0;
  GmstSec += 1.00273790935 * UtSec;
  Result := NormalizeDeg(GmstSec / 240.0);
end;

function LocalSiderealTime(JD, LonDeg: Double): Double;
begin
  Result := NormalizeDeg(GreenwichMeanSiderealTime(JD) + LonDeg);
end;

// Free function rather than a record method: Free Pascal does not allow
// record method declarations in a unit's interface section. Mirrors
// DateTime::JulianDay() in astro_math.h.
function JulianDayOf(const When: TUtcDateTime): Double;
begin
  JulianDayOf := JulianDate(When.Year, When.Month, When.Day, When.Hour, When.Minute, When.Second);
end;

procedure HorizontalCoordsAtJulian(JD: Double; const Site: TObserver;
  RAHours, DecDeg: Double; out AzDeg, AltDeg: Double);
var
  LstDeg, RaDeg, HaDeg, LatRad, DecRad, HaRad, SinAlt, AzSin, AzCos: Double;
begin
  LstDeg := LocalSiderealTime(JD, Site.LonDeg);
  RaDeg := RAHours * 15.0;
  HaDeg := NormalizeDeg(LstDeg - RaDeg);
  if HaDeg > 180.0 then HaDeg := HaDeg - 360.0;
  LatRad := DegToRad(Site.LatDeg);
  DecRad := DegToRad(DecDeg);
  HaRad := DegToRad(HaDeg);
  SinAlt := Sin(LatRad) * Sin(DecRad) + Cos(LatRad) * Cos(DecRad) * Cos(HaRad);
  AltDeg := RadToDeg(ArcSin(EnsureRange(SinAlt, -1.0, 1.0)));
  AzSin := -Cos(DecRad) * Sin(HaRad);
  AzCos := Sin(DecRad) * Cos(LatRad) - Cos(DecRad) * Sin(LatRad) * Cos(HaRad);
  AzDeg := NormalizeDeg(RadToDeg(ArcTan2(AzSin, AzCos)));
end;

procedure HorizontalCoordsAt(const When: TUtcDateTime; const Site: TObserver;
  RAHours, DecDeg: Double; out AzDeg, AltDeg: Double);
begin
  HorizontalCoordsAtJulian(JulianDayOf(When), Site, RAHours, DecDeg, AzDeg, AltDeg);
end;

// Record-constructor helpers: Free Pascal 3.2 does not accept
// TObserver(Lat, Lon) / TUtcDateTime(y, m, ...) inline constructor syntax.
function ObserverAt(LatDeg, LonDeg: Double): TObserver;
begin
  Result.LatDeg := LatDeg;
  Result.LonDeg := LonDeg;
end;

function UtcDateTimeAt(Y, Mo, D, H, Mi, S: Double): TUtcDateTime;
begin
  Result.Year := Y; Result.Month := Mo; Result.Day := D;
  Result.Hour := H; Result.Minute := Mi; Result.Second := S;
end;

procedure HorizontalCoords(Y, Mo, D, H, Mi, S, LatDeg, LonDeg, RAHours, DecDeg: Double;
  out AzDeg, AltDeg: Double);
var
  JD, LstDeg, RaDeg, HaDeg, LatRad, DecRad, HaRad, SinAlt, AzSin, AzCos: Double;
begin
  JD := JulianDate(Y, Mo, D, H, Mi, S);
  LstDeg := LocalSiderealTime(JD, LonDeg);
  RaDeg := RAHours * 15.0;
  HaDeg := NormalizeDeg(LstDeg - RaDeg);
  if HaDeg > 180.0 then HaDeg -= 360.0;
  LatRad := DegToRad(LatDeg);
  DecRad := DegToRad(DecDeg);
  HaRad := DegToRad(HaDeg);
  SinAlt := Sin(LatRad) * Sin(DecRad) + Cos(LatRad) * Cos(DecRad) * Cos(HaRad);
  AltDeg := RadToDeg(ArcSin(EnsureRange(SinAlt, -1.0, 1.0)));
  AzSin := -Cos(DecRad) * Sin(HaRad);
  AzCos := Sin(DecRad) * Cos(LatRad) - Cos(DecRad) * Sin(LatRad) * Cos(HaRad);
  AzDeg := NormalizeDeg(RadToDeg(ArcTan2(AzSin, AzCos)));
end;

procedure SunRAandDec(JD: Double; out RAHours, DecDeg: Double);
var
  T, L0, M, C, Lambda, Obliq, Ra: Double;
begin
  T := (JD - 2451545.0) / 36525.0;
  L0 := 280.46646 + 36000.76983 * T;
  M := 357.52911 + 35999.05029 * T;
  C := 1.914602 * Sin(DegToRad(M));
  Lambda := DegToRad(L0 + C);
  Obliq := DegToRad(23.4393);
  Ra := RadToDeg(ArcTan2(Cos(Obliq) * Sin(Lambda), Cos(Lambda))) / 15.0;
  if Ra < 0 then Ra += 24.0;
  RAHours := Ra;
  DecDeg := RadToDeg(ArcSin(Sin(Obliq) * Sin(Lambda)));
end;

procedure KeplerSolve(M, E: Double; out ESol: Double);
var
  DE: Double;
  I: Integer;
  Mrad: Double;
begin
  Mrad := DegToRad(M);
  ESol := Mrad;
  for I := 1 to 20 do
  begin
    DE := (ESol - E * Sin(ESol) - Mrad) / (1.0 - E * Cos(ESol));
    ESol -= DE;
    if Abs(DE) < 1e-12 then Break;
  end;
end;

procedure Heliocentric(A, E, N, I, W, M: Double; out X, Y, Z: Double);
var
  Er, Nrad, Irad, Wrad, XV, YV: Double;
begin
  Nrad := DegToRad(N); Irad := DegToRad(I); Wrad := DegToRad(W);
  KeplerSolve(M, E, Er);
  XV := A * (Cos(Er) - E);
  YV := A * Sqrt(1.0 - E * E) * Sin(Er);
  X := XV * (Cos(Nrad) * Cos(Wrad) - Sin(Nrad) * Sin(Wrad) * Cos(Irad))
     - YV * (Cos(Nrad) * Sin(Wrad) + Sin(Nrad) * Cos(Wrad) * Cos(Irad));
  Y := XV * (Sin(Nrad) * Cos(Wrad) + Cos(Nrad) * Sin(Wrad) * Cos(Irad))
     - YV * (Sin(Nrad) * Sin(Wrad) - Cos(Nrad) * Cos(Wrad) * Cos(Irad));
  Z := XV * (Sin(Wrad) * Sin(Irad)) + YV * (Cos(Wrad) * Sin(Irad));
end;

procedure PrecessJ2000ToDate(JD: Double; var X, Y, Z: Double);
var
  T, T2, T3, T4, T5, Zeta, Zz, Theta, As_, Bs, Cs,
  X1, Y1, Z1, X2, Y2, Z2: Double;
begin
  T := (JD - 2451545.0) / 36525.0;
  T2 := T * T; T3 := T2 * T; T4 := T3 * T; T5 := T4 * T;
  Zeta  := 2306.083227 * T + 0.293086 * T2 + 0.018145 * T3 - 0.000018 * T4 - 0.0000002 * T5;
  Zz    := 2306.077181 * T + 1.0927348 * T2 + 0.018182 * T3 + 0.000018 * T4 - 0.0000002 * T5;
  Theta := 2004.191903 * T - 0.4294934 * T2 - 0.0418225 * T3 - 0.000007 * T4 + 0.0000001 * T5;
  As_ := DegToRad(Zeta / 3600.0);
  Bs  := DegToRad(Zz / 3600.0);
  Cs  := DegToRad(Theta / 3600.0);
  X1 := X * Cos(As_) - Y * Sin(As_);
  Y1 := X * Sin(As_) + Y * Cos(As_);
  Z1 := Z;
  // R2(-theta): the IAU 2006 matrix mixes x/z with MINUS sin(theta). The +sin
  // form is the transpose and a 2*theta (~1071" at 2026) rotation error.
  // Matches the C++ fix in planetary_objects.h; pinned by make check-horiz.
  X2 := X1 * Cos(Cs) - Z1 * Sin(Cs);
  Y2 := Y1;
  Z2 := X1 * Sin(Cs) + Z1 * Cos(Cs);
  X := X2 * Cos(Bs) - Y2 * Sin(Bs);
  Y := X2 * Sin(Bs) + Y2 * Cos(Bs);
  Z := Z2;
end;

// Precess a J2000 RA (hours) / Dec (deg) pair to mean-of-date.
// NEEDED FOR CATALOG OBJECTS ONLY (celestial_objects.csv is J2000). Objects
// from LoadPlanets are already mean-of-date and must NOT be passed through here.
// Counterpart of PrecessRaDecJ2000ToDate in planetary_objects.h.
procedure PrecessRaDecJ2000ToDate(JD: Double; var RAHours, DecDeg: Double);
var
  Ra, Dec, X, Y, Z: Double;
begin
  Ra := DegToRad(RAHours * 15.0);
  Dec := DegToRad(DecDeg);
  X := Cos(Dec) * Cos(Ra);
  Y := Cos(Dec) * Sin(Ra);
  Z := Sin(Dec);
  PrecessJ2000ToDate(JD, X, Y, Z);
  if Z > 1.0 then Z := 1.0;
  if Z < -1.0 then Z := -1.0;
  DecDeg := RadToDeg(ArcSin(Z));
  Ra := RadToDeg(ArcTan2(Y, X));
  if Ra < 0.0 then Ra := Ra + 360.0;
  RAHours := Ra / 15.0;
end;

procedure ComputeMoonGeocentric(JD: Double; out RAHours, DecDeg, Magnitude: Double);
var
  Days, N, W, A, E, M0, DM, M, Er, XOrb, YOrb,
  Ls, Lm, D, F, Mm, Ms, MsSun, WsSun,
  LonPert, LatPert, DistPert, Lon0, Lat0, R0, Lon, Lat, Rm,
  X, Y, Z, XEq, YEq, ZEq, R, Ra, Obliquity,
  CosSep, Sep, Phase: Double;
  SunRAH, SunDecD: Double;
begin
  // Schlyter's lunar elements: day number from 1999-12-31 0h UT (JD 2451543.5)
  Days := JD - 2451543.5;
  N  := 125.1228 - 0.0529538083 * Days;
  W  := 318.0634 + 0.1643573223 * Days;
  A  := 0.002566;
  E  := 0.054900;
  M0 := 115.3654;
  DM := 13.0649929535;
  M := FMod(M0 + DM * Days, 360.0);
  if M < 0 then M += 360.0;
  KeplerSolve(M, E, Er);
  XOrb := A * (Cos(Er) - E);
  YOrb := A * Sqrt(1.0 - E * E) * Sin(Er);
  X := (Cos(DegToRad(N)) * Cos(DegToRad(W)) - Sin(DegToRad(N)) * Sin(DegToRad(W)) * Cos(DegToRad(5.1454))) * XOrb
     + (-Cos(DegToRad(N)) * Sin(DegToRad(W)) - Sin(DegToRad(N)) * Cos(DegToRad(W)) * Cos(DegToRad(5.1454))) * YOrb;
  Y := (Sin(DegToRad(N)) * Cos(DegToRad(W)) + Cos(DegToRad(N)) * Sin(DegToRad(W)) * Cos(DegToRad(5.1454))) * XOrb
     + (-Sin(DegToRad(N)) * Sin(DegToRad(W)) + Cos(DegToRad(N)) * Cos(DegToRad(W)) * Cos(DegToRad(5.1454))) * YOrb;
  Z := (Sin(DegToRad(W)) * Sin(DegToRad(5.1454))) * XOrb + (Cos(DegToRad(W)) * Sin(DegToRad(5.1454))) * YOrb;

  MsSun := 356.0470 + 0.9856002585 * Days;
  WsSun := 282.9404 + 4.70935e-5 * Days;
  Ls := MsSun + WsSun;
  Lm := M + N + W;
  D  := Lm - Ls;
  F  := Lm - N;
  Mm := M;
  Ms := MsSun;

  LonPert :=
      -1.274 * Sin(DegToRad(Mm - 2.0 * D))
      + 0.658 * Sin(DegToRad(2.0 * D))
      - 0.186 * Sin(DegToRad(Ms))
      - 0.059 * Sin(DegToRad(2.0 * Mm - 2.0 * D))
      - 0.057 * Sin(DegToRad(Mm - 2.0 * D + Ms))
      + 0.053 * Sin(DegToRad(Mm + 2.0 * D))
      + 0.046 * Sin(DegToRad(2.0 * D - Ms))
      + 0.041 * Sin(DegToRad(Mm - Ms))
      - 0.035 * Sin(DegToRad(D))
      - 0.031 * Sin(DegToRad(Mm + Ms))
      - 0.015 * Sin(DegToRad(2.0 * F - 2.0 * D))
      + 0.011 * Sin(DegToRad(Mm - 4.0 * D));
  LatPert :=
      -0.173 * Sin(DegToRad(F - 2.0 * D))
      - 0.055 * Sin(DegToRad(Mm - F - 2.0 * D))
      - 0.046 * Sin(DegToRad(Mm + F - 2.0 * D))
      + 0.033 * Sin(DegToRad(F + 2.0 * D))
      + 0.017 * Sin(DegToRad(2.0 * Mm + F));
  DistPert :=
      -0.58 * Cos(DegToRad(Mm - 2.0 * D))
      - 0.46 * Cos(DegToRad(2.0 * D));

  Lon0 := ArcTan2(Y, X);
  Lat0 := ArcTan2(Z, Sqrt(X * X + Y * Y));
  R0 := Sqrt(X * X + Y * Y + Z * Z);
  Lon := Lon0 + DegToRad(LonPert);
  Lat := Lat0 + DegToRad(LatPert);
  Rm := R0 + DistPert * (A / 60.2666);

  X := Rm * Cos(Lat) * Cos(Lon);
  Y := Rm * Cos(Lat) * Sin(Lon);
  Z := Rm * Sin(Lat);

  Obliquity := DegToRad(23.4392911111111);
  XEq := X;
  YEq := Y * Cos(Obliquity) - Z * Sin(Obliquity);
  ZEq := Y * Sin(Obliquity) + Z * Cos(Obliquity);

  R := Sqrt(XEq * XEq + YEq * YEq + ZEq * ZEq);
  Ra := RadToDeg(ArcTan2(YEq, XEq)) / 15.0;
  DecDeg := RadToDeg(ArcSin(ZEq / R));

  // Schlyter output is already equinox-of-date: no precession applied (matches C++).
  SunRAandDec(JD, SunRAH, SunDecD);
  CosSep := Sin(DegToRad(DecDeg)) * Sin(DegToRad(SunDecD))
          + Cos(DegToRad(DecDeg)) * Cos(DegToRad(SunDecD)) * Cos(DegToRad(Ra * 15.0 - SunRAH * 15.0));
  Sep := RadToDeg(ArcSin(EnsureRange(CosSep, -1.0, 1.0)));
  Sep := RadToDeg(ArcCos(EnsureRange(CosSep, -1.0, 1.0)));
  Phase := (1.0 - Cos(DegToRad(Sep))) / 2.0;
  Magnitude := -12.74 + 5.0 * Log10(Max(R / 0.002570, 1e-10)) + 5.0 * (1.0 - Phase);

  if Ra < 0 then Ra += 24.0;
  RAHours := Ra;
end;

// ---------- truncated VSOP87 series (8 major planets) --------------------
// Mirror of the C++ path in planetary_objects.h: heliocentric ecliptic L/B/R
// from the generated tables (of-date frame), FK5 correction, light time,
// annual aberration, obliquity-of-date rotation. No precession (of-date).
procedure VsopEval(PlanetIdx, Ser: Integer; T: Double; out OutVal: Double);
var
  Off, TermOff, PowOff, Cnt, E, Lev, Pw: Integer;
  S, Tp: Double;
begin
  Off := VSOP_LAYOUT[PlanetIdx, Ser, 0];
  Cnt := VSOP_LAYOUT[PlanetIdx, Ser, 1];
  PowOff := VSOP_LAYOUT[PlanetIdx, Ser, 2];
  E := Off + Cnt;
  OutVal := 0.0;
  while Off < E do
  begin
    Lev := VSOP_POWERS[PowOff + (Off - VSOP_LAYOUT[PlanetIdx, Ser, 0])];
    S := 0.0;
    repeat
      S := S + VSOP_TERMS[Off, 0] * Cos(VSOP_TERMS[Off, 1] + VSOP_TERMS[Off, 2] * T);
      Inc(Off);
    until (Off >= E) or (VSOP_POWERS[PowOff + (Off - VSOP_LAYOUT[PlanetIdx, Ser, 0])] <> Lev);
    Tp := 1.0;
    for Pw := 1 to Lev do Tp := Tp * T;
    OutVal := OutVal + S * Tp;
  end;
end;

function VsopPlanetName(I: Integer): string;
begin
  Result := VSOP_NAMES[I];
end;

procedure ComputePlanetVSOP(I: Integer; JD: Double;
  out RAHours, DecDeg, RPlanet, Dist: Double);
var
  T, Tc, Lp, Bp, Rp, Le, Be, Re,
  PX, PY, PZ, EX, EY, EZ,
  LX, LY, LZ, DistDays, X, Y, Z,
  L2, B2, R2, L3, B3, R3, L4, B4, R4,
  AbK, EpsSec, Obliq,
  XEq, YEq, ZEq, RR, Ra, RP2: Double;
  K: Integer;

  procedure FK5(var L: Double; var B: Double);
  var Lam2: Double;
  begin
    Lam2 := L - (1.397 + 0.00031 * T) * T * Pi / 180.0;
    L := L + (-0.09033 + 0.03916 * (Cos(Lam2) + Sin(Lam2)) * Tan(B)) / 3600.0 * Pi / 180.0;
    B := B + (0.03916 * (Cos(Lam2) - Sin(Lam2))) / 3600.0 * Pi / 180.0;
  end;

  procedure Rect(L, B, R: Double; out X, Y, Z: Double);
  begin
    X := R * Cos(B) * Cos(L);
    Y := R * Cos(B) * Sin(L);
    Z := R * Sin(B);
  end;

begin
  T := (JD - 2451545.0) / 365250.0; // Julian millennia

  VsopEval(I, 0, T, Lp); VsopEval(I, 1, T, Bp); VsopEval(I, 2, T, Rp);
  Lp := Lp / 1e8; Bp := Bp / 1e8; Rp := Rp / 1e8;
  FK5(Lp, Bp);
  Rect(Lp, Bp, Rp, PX, PY, PZ);

  VsopEval(2, 0, T, Le); VsopEval(2, 1, T, Be); VsopEval(2, 2, T, Re);
  Le := Le / 1e8; Be := Be / 1e8; Re := Re / 1e8;
  FK5(Le, Be);
  Rect(Le, Be, Re, EX, EY, EZ);

  // Light time: recompute the planet at jd - dist/173.15 (3 iterations).
  LX := PX; LY := PY; LZ := PZ;
  DistDays := 0.0;
  for K := 1 to 3 do
  begin
    VsopEval(I, 0, T, L2); VsopEval(I, 1, T, B2); VsopEval(I, 2, T, R2);
    L2 := L2 / 1e8; B2 := B2 / 1e8; R2 := R2 / 1e8;
    FK5(L2, B2);
    Rect(L2, B2, R2, LX, LY, LZ);
    DistDays := Sqrt(Sqr(LX-EX) + Sqr(LY-EY) + Sqr(LZ-EZ)) / 173.15;
  end;
  X := LX - EX; Y := LY - EY; Z := LZ - EZ;
  Dist := Sqrt(Sqr(X) + Sqr(Y) + Sqr(Z));

  // Annual aberration: Earth velocity via central difference over 0.01 d.
  AbK := 0.0057755183; // days/AU
  VsopEval(2, 0, T, L3); VsopEval(2, 1, T, B3); VsopEval(2, 2, T, R3);
  L3 := L3 / 1e8; B3 := B3 / 1e8; R3 := R3 / 1e8;
  VsopEval(2, 0, T, L4); VsopEval(2, 1, T, B4); VsopEval(2, 2, T, R4);
  L4 := L4 / 1e8; B4 := B4 / 1e8; R4 := R4 / 1e8;
  Rect(L3, B3, R3, PX, PY, PZ); // reuse as x3,y3,z3
  Rect(L4, B4, R4, EX, EY, EZ); // reuse as x4,y4,z4
  X := X + (PX - EX) / 0.01 * AbK;
  Y := Y + (PY - EY) / 0.01 * AbK;
  Z := Z + (PZ - EZ) / 0.01 * AbK;

  // Mean obliquity of date (VSOP87 is of-date; no precession applied).
  Tc := (JD - 2451545.0) / 36525.0;
  EpsSec := 84381.448 - 46.8150 * Tc - 0.00059 * Tc * Tc + 0.001813 * Tc * Tc * Tc;
  Obliq := EpsSec / 3600.0 * Pi / 180.0;
  XEq := X;
  YEq := Y * Cos(Obliq) - Z * Sin(Obliq);
  ZEq := Y * Sin(Obliq) + Z * Cos(Obliq);

  RR := Sqrt(Sqr(XEq) + Sqr(YEq) + Sqr(ZEq));
  Ra := RadToDeg(ArcTan2(YEq, XEq)) / 15.0;
  DecDeg := RadToDeg(ArcSin(EnsureRange(ZEq / RR, -1.0, 1.0)));
  if Ra < 0 then Ra += 24.0;
  RAHours := Ra;

  RPlanet := Sqrt(Sqr(LX) + Sqr(LY) + Sqr(LZ));
end;

procedure ComputePlanetFromElements(const Elem: array of Double; MagBase, G, JD: Double;
  const ObjName: string;
  out RAHours, DecDeg, Magnitude: Double);
var
  Days, N, Inc2, W, A, E, M, EM, EM2,
  PX, PY, PZ, EX, EY, EZ, X, Y, Z,
  DistDays, LX, LY, LZ, Mlt, DX, DY, DZ,
  VX, VY, VZ, Obliq, XEq, YEq, ZEq, R, Ra,
  RPlanet, Dist, Dot, CosAlpha, Phase, AbK, Alpha: Double;
  K: Integer;
  SunRAH, SunDecD: Double;
begin
  Days := JD - 2451545.0;
  // Elem order: a,e,N,i,w,M0,dM,dN,di,dw,da,de  (indices 0..11)
  N := Elem[2] + Elem[9] * Days;
  Inc2 := Elem[3] + Elem[10] * Days;
  W := Elem[4] + Elem[11] * Days;
  A := Elem[0]; // da is 0 for all rows in the CSV
  E := Elem[1];
  M := FMod(Elem[5] + Elem[6] * Days, 360.0);
  if M < 0 then M += 360.0;

  Heliocentric(A, E, N, Inc2, W, M, PX, PY, PZ);

  EM := FMod(357.529 + 0.98560028 * Days, 360.0);
  if EM < 0 then EM += 360.0;
  Heliocentric(1.00000, 0.016709, 0.0, 0.0, 102.9372, EM, EX, EY, EZ);

  // Light-time correction: 3 iterations
  DistDays := 0.0;
  for K := 1 to 3 do
  begin
    Mlt := FMod(Elem[5] + Elem[6] * (Days - DistDays), 360.0);
    if Mlt < 0 then Mlt += 360.0;
    Heliocentric(A, E, N, Inc2, W, Mlt, LX, LY, LZ);
    DX := LX - EX; DY := LY - EY; DZ := LZ - EZ;
    DistDays := Sqrt(DX * DX + DY * DY + DZ * DZ) / 173.15;
  end;
  X := LX - EX; Y := LY - EY; Z := LZ - EZ;

  // Annual aberration via Earth velocity finite difference
  AbK := 0.0057755183;
  EM2 := FMod(357.529 + 0.98560028 * (Days + 0.5), 360.0);
  if EM2 < 0 then EM2 += 360.0;
  Heliocentric(1.00000, 0.016709, 0.0, 0.0, 102.9372, EM2, VX, VY, VZ);
  X += (VX - EX) * AbK; Y += (VY - EY) * AbK; Z += (VZ - EZ) * AbK;

  Obliq := DegToRad(23.4392911111111);
  XEq := X;
  YEq := Y * Cos(Obliq) - Z * Sin(Obliq);
  ZEq := Y * Sin(Obliq) + Z * Cos(Obliq);
  PrecessJ2000ToDate(JD, XEq, YEq, ZEq);

  R := Sqrt(XEq * XEq + YEq * YEq + ZEq * ZEq);
  Ra := RadToDeg(ArcTan2(YEq, XEq)) / 15.0;
  DecDeg := RadToDeg(ArcSin(ZEq / R));

  RPlanet := Sqrt(PX * PX + PY * PY + PZ * PZ);
  Dist := Sqrt(X * X + Y * Y + Z * Z);
  Magnitude := MagBase + 5.0 * Log10(Max(RPlanet * Dist, 1e-10));

  // Phase correction for Mercury/Venus (same as the C++ version, which selects
  // by object NAME, not by semi-major axis - a-values change on element refresh):
  if (ObjName = 'Mercury') or (ObjName = 'Venus') then
  begin
    Dot := (-LX) * (EX - LX) + (-LY) * (EY - LY) + (-LZ) * (EZ - LZ);
    CosAlpha := EnsureRange(Dot / (RPlanet * Dist), -1.0, 1.0);
    Phase := Max((1.0 + CosAlpha) / 2.0, 0.001);
    Magnitude -= 2.5 * Log10(Phase);
  end;

  // H-G phase function for minor bodies (G > 0), same as the C++ version: with
  // the CSV magnitude column holding the absolute magnitude H, brightness stays
  // correct at large phase angles instead of only near opposition.
  if G > 0.0 then
  begin
    Dot := (-LX) * (EX - LX) + (-LY) * (EY - LY) + (-LZ) * (EZ - LZ);
    CosAlpha := EnsureRange(Dot / (RPlanet * Dist), -1.0, 1.0);
    Alpha := ArcCos(CosAlpha);
    Magnitude -= 2.5 * Log10(Max(HGPhase(G, Alpha), 1e-6));
  end;

  if Ra < 0 then Ra += 24.0;
  RAHours := Ra;
end;

function MoonIllumination(MoonRAHours, MoonDecDeg, JD: Double): Double;
var
  SunRAH, SunDecD, CosSep, Sep: Double;
begin
  SunRAandDec(JD, SunRAH, SunDecD);
  CosSep := Sin(DegToRad(MoonDecDeg)) * Sin(DegToRad(SunDecD))
          + Cos(DegToRad(MoonDecDeg)) * Cos(DegToRad(SunDecD)) * Cos(DegToRad(MoonRAHours * 15.0 - SunRAH * 15.0));
  Sep := RadToDeg(ArcCos(EnsureRange(CosSep, -1.0, 1.0)));
  Result := 0.5 * (1.0 - Cos(DegToRad(Sep)));
end;

function MoonPhaseName(MoonRAHours, MoonDecDeg, JD, Illum: Double): string;
var
  T, L0, M, C, LambdaSun, Obl, X, Y, Z, YE, XE, LambdaMoon, Elong: Double;
  Waxing: Boolean;
begin
  T := (JD - 2451545.0) / 36525.0;
  L0 := 280.46646 + 36000.76983 * T;
  M := 357.52911 + 35999.05029 * T;
  C := 1.914602 * Sin(DegToRad(M));
  LambdaSun := FMod(L0 + C, 360.0);
  Obl := DegToRad(23.4393);
  X := Cos(DegToRad(MoonDecDeg)) * Cos(DegToRad(MoonRAHours * 15.0));
  Y := Cos(DegToRad(MoonDecDeg)) * Sin(DegToRad(MoonRAHours * 15.0));
  Z := Sin(DegToRad(MoonDecDeg));
  YE := Y * Cos(Obl) - Z * Sin(Obl);
  XE := X;
  LambdaMoon := RadToDeg(ArcTan2(YE, XE));
  LambdaSun := FMod(LambdaSun + 180.0, 360.0) - 180.0;
  Elong := LambdaMoon - LambdaSun;
  if Elong < -180 then Elong += 360;
  if Elong > 180 then Elong -= 360;
  Waxing := Elong > 0;

  if Illum < 0.03 then Exit('New Moon');
  if Illum > 0.97 then Exit('Full Moon');
  if (Abs(Elong) > 80) and (Abs(Elong) < 100) then
    if Waxing then Exit('First Quarter') else Exit('Last Quarter');
  if Waxing then
    if Illum < 0.5 then Exit('Waxing Crescent') else Exit('Waxing Gibbous');
  if Illum < 0.5 then Exit('Waning Crescent') else Exit('Waning Gibbous');
end;

procedure JDToCalendar(JD: Double; out Y, Mo, D, H, Mi, S: Double);
// Inverse of JulianDate (Fliegel-Van Flandern). H/Mi/S are integer hours,
// minutes and integer seconds; the fractional-second remainder is dropped
// (fine for a display line).
var
  Z, F, Alpha, A2, B2, C2, D2, E2: Double;
begin
  Z := Floor(JD + 0.5);
  F := JD + 0.5 - Z;
  Alpha := Floor((Z - 1867216.25) / 36524.25);
  A2 := Z + 1.0 + Alpha - Floor(Alpha / 4.0);
  B2 := A2 + 1524.0;
  C2 := Floor((B2 - 122.1) / 365.25);
  D2 := Floor(365.25 * C2);
  E2 := Floor((B2 - D2) / 30.6001);
  D := B2 - D2 - Floor(30.6001 * E2) + F;
  Mo := E2 - 1.0;
  if E2 >= 14 then Mo := E2 - 13.0;
  Y := C2 - 4716.0;
  // fractional day -> h/m/s
  H := Frac(D) * 24.0;
  Mi := Frac(H) * 60.0;
  S := Frac(Mi) * 60.0;
  D := Trunc(D);
  H := Trunc(H);
  Mi := Trunc(Mi);
  S := Trunc(S);
end;

function TwilightState(SunAltDeg: Double): string;
begin
  if SunAltDeg > -0.833 then Exit('day');
  if SunAltDeg > -6.0 then Exit('civil twilight');
  if SunAltDeg > -12.0 then Exit('nautical twilight');
  if SunAltDeg > -18.0 then Exit('astronomical twilight');
  Result := 'night';
end;

procedure FindSunEvents(Y, Mo, D, LatDeg, LonDeg, ThresholdDeg: Double;
  out RiseH, SetH: Double);
var
  T, JD0, RA, Dec, Az, Alt, PrevAlt: Double;
  First: Boolean;
begin
  RiseH := -1.0;
  SetH := -1.0;
  PrevAlt := 0.0;
  First := True;
  JD0 := JulianDate(Y, Mo, D, 0, 0, 0);
  // 5-min brute-force scan, same as the C++ version
  T := 5.0 / 60.0;
  while T <= 24.0 do
  begin
    SunRAandDec(JD0 + T / 24.0, RA, Dec);
    HorizontalCoordsAtJulian(JulianDate(Y, Mo, D, 0, 0, 0) + T / 24.0,
      ObserverAt(LatDeg, LonDeg), RA, Dec, Az, Alt);
    if not First then
    begin
      if (PrevAlt < ThresholdDeg) and (Alt >= ThresholdDeg) and (RiseH < 0) then RiseH := T;
      if (PrevAlt > ThresholdDeg) and (Alt <= ThresholdDeg) and (SetH < 0) then SetH := T;
    end;
    PrevAlt := Alt;
    First := False;
    T += 5.0 / 60.0;
  end;
end;

function SplitCsv(const Line: string): specialize TArray<string>;
var
  Parts: TStringArray;
begin
  Parts := Line.Split(',');
  Result := Parts;
end;

function LoadStars(const CsvPath: string): specialize TArray<TCelestialObject>;
var
  Sl: TStringList;
  I: Integer;
  Parts: TStringArray;
  Obj: TCelestialObject;
begin
  SetLength(Result, 0);
  if not FileExists(CsvPath) then Exit;
  Sl := TStringList.Create;
  try
      Sl.LoadFromFile(CsvPath);
      for I := 1 to Sl.Count - 1 do
      begin
        // Skip the provenance comments and the column-name row, exactly as
        // CelestialObjectReader::ReadFromCSV does on the C++ side. Without the
        // header check the row parses into a phantom object literally named
        // "name" that the legend lists and --select happily locks on to.
        if (Trim(Sl[I]) = '') or (Sl[I][1] = '#') then Continue;
        if Copy(Sl[I], 1, 5) = 'name,' then Continue;
      Parts := SplitCsv(Sl[I]);
      if Length(Parts) < 9 then Continue;
      Obj.Name := Parts[0];
      Obj.RAHours := StrToFloatDef(Parts[1], 0) + StrToFloatDef(Parts[2], 0) / 60.0
                   + StrToFloatDef(Parts[3], 0) / 3600.0;
      Obj.DecDeg := StrToFloatDef(Parts[4], 1) * (StrToFloatDef(Parts[5], 0)
                   + StrToFloatDef(Parts[6], 0) / 60.0 + StrToFloatDef(Parts[7], 0) / 3600.0);
      Obj.Magnitude := StrToFloatDef(Parts[8], 99);
      if Length(Parts) >= 10 then Obj.Bayer := Parts[9] else Obj.Bayer := '';
      SetLength(Result, Length(Result) + 1);
      Result[High(Result)] := Obj;
    end;
  finally
    Sl.Free;
  end;
end;

function LoadPlanets(const CsvPath: string; JD: Double): specialize TArray<TPlanetaryObject>;
var
  Sl: TStringList;
  I: Integer;
  Parts: TStringArray;
  Obj: TPlanetaryObject;
  Elem: array[0..11] of Double;
  K: Integer;
  MoonRA, MoonDec, MoonMag, PlanetG, RPlanet, Dist, MagBase, Dot, CosA, Phase: Double;
  VIdx, VI: Integer;
  VName: string;
  PXX, PYY, PZZ, EXX, EYY, EZZ: Double;
  VT, Lp, Bp, Rp, Le, Be, Re, RP2: Double;
  IsVSOP: Boolean;
begin
  SetLength(Result, 0);

  // The 8 major planets come from the truncated VSOP87 series (mirror of the
  // C++ path); their CSV rows are skipped below.
  for VIdx := 0 to 7 do
  begin
    if VIdx = 2 then Continue; // index 2 = Earth (the observer)
    VName := VsopPlanetName(VIdx);
    ComputePlanetVSOP(VIdx, JD, Obj.RAHours, Obj.DecDeg, RPlanet, Dist);
    MagBase := 0.0;
    if VName = 'Mercury' then MagBase := -0.42;
    if VName = 'Venus'   then MagBase := -4.40;
    if VName = 'Mars'    then MagBase := -1.52;
    if VName = 'Jupiter' then MagBase := -9.40;
    if VName = 'Saturn'  then MagBase := -8.88;
    if VName = 'Uranus'  then MagBase := -7.19;
    if VName = 'Neptune' then MagBase := -6.87;
    Obj.Magnitude := MagBase + 5.0 * Log10(Max(RPlanet * Dist, 1e-10));
    if (VName = 'Mercury') or (VName = 'Venus') then
    begin
      // Lambert phase term from Sun-planet-observer geometry (C++ parity).
      // Helio vectors at (planet light-time epoch, Earth epoch), rect coords.
      VT := (JD - 2451545.0) / 365250.0;
      VsopEval(VIdx, 0, VT, Lp); VsopEval(VIdx, 1, VT, Bp); VsopEval(VIdx, 2, VT, Rp);
      Lp := Lp / 1e8; Bp := Bp / 1e8; Rp := Rp / 1e8;
      PXX := Rp * Cos(Bp) * Cos(Lp); PYY := Rp * Cos(Bp) * Sin(Lp); PZZ := Rp * Sin(Bp);
      VsopEval(2, 0, VT, Le); VsopEval(2, 1, VT, Be); VsopEval(2, 2, VT, Re);
      Le := Le / 1e8; Be := Be / 1e8; Re := Re / 1e8;
      EXX := Re * Cos(Be) * Cos(Le); EYY := Re * Cos(Be) * Sin(Le); EZZ := Re * Sin(Be);
      Dot := (-PXX) * (EXX - PXX) + (-PYY) * (EYY - PYY) + (-PZZ) * (EZZ - PZZ);
      RP2 := Sqrt(Sqr(PXX) + Sqr(PYY) + Sqr(PZZ));
      Dist := Sqrt(Sqr(EXX-PXX) + Sqr(EYY-PYY) + Sqr(EZZ-PZZ));
      CosA := EnsureRange(Dot / (RP2 * Dist), -1.0, 1.0);
      Phase := Max((1.0 + CosA) / 2.0, 0.001);
      Obj.Magnitude := Obj.Magnitude - 2.5 * Log10(Phase);
    end;
    Obj.Name := VName;
    SetLength(Result, Length(Result) + 1);
    Result[High(Result)] := Obj;
  end;

  if FileExists(CsvPath) then
  begin
    Sl := TStringList.Create;
    try
      Sl.LoadFromFile(CsvPath);
      for I := 1 to Sl.Count - 1 do
      begin
        if (Trim(Sl[I]) = '') or (Sl[I][1] = '#') then Continue;
          if Copy(Sl[I], 1, 5) = 'name,' then Continue; // column-name row
        Parts := SplitCsv(Sl[I]);
        if Length(Parts) < 14 then Continue;
        for K := 1 to 12 do
          Elem[K - 1] := StrToFloatDef(Parts[K], 0);
        Obj.Name := Parts[0];
        IsVSOP := (Obj.Name = 'Mercury') or (Obj.Name = 'Venus') or (Obj.Name = 'Earth') or
                  (Obj.Name = 'Mars') or (Obj.Name = 'Jupiter') or (Obj.Name = 'Saturn') or
                  (Obj.Name = 'Uranus') or (Obj.Name = 'Neptune');
        if IsVSOP then Continue; // major planets come from VSOP87 above
        PlanetG := 0.0;
        if Length(Parts) >= 15 then
          PlanetG := StrToFloatDef(Parts[14], 0);
        ComputePlanetFromElements(Elem, StrToFloatDef(Parts[13], 0), PlanetG, JD,
          Obj.Name, Obj.RAHours, Obj.DecDeg, Obj.Magnitude);
        SetLength(Result, Length(Result) + 1);
        Result[High(Result)] := Obj;
      end;
    finally
      Sl.Free;
    end;
  end;
  // Moon is never in the CSV; always append from the special model.
  ComputeMoonGeocentric(JD, MoonRA, MoonDec, MoonMag);
  Obj.Name := 'Moon';
  Obj.RAHours := MoonRA;
  Obj.DecDeg := MoonDec;
  Obj.Magnitude := MoonMag;
  SetLength(Result, Length(Result) + 1);
  Result[High(Result)] := Obj;
end;

// Does AName denote exactly Messier object AToken (already lowercased)?
// Mirrors IsMessierObject() in object_name.h. The catalog spells Messier objects
// two ways -- "Lagoon Nebula (M8)" and "M11 (Wild Duck Cluster)" -- and "M8"
// must match neither "M80" nor "M81", hence the token-boundary test.
function IsMessierObject(const AName, AToken: string): Boolean;
var
  LN: string;
  P: Integer;
  After: Integer;
begin
  LN := LowerCase(AName);
  Result := False;
  // Form 1: "(m8)" as a complete tag.
  P := Pos('(' + AToken + ')', LN);
  if P > 0 then
  begin
    After := P + Length(AToken) + 2;   // 1-based index one past ')'
    if (After > Length(LN)) or (LN[After] = ' ') then
      Result := True;
  end;
  if Result then Exit;
  // Form 2: name begins with "m8" followed by a non-alphanumeric.
  if Copy(LN, 1, Length(AToken)) <> AToken then Exit;
  After := Length(AToken) + 1;
  if After > Length(LN) then
    Result := True
  else
    Result := not (LN[After] in ['0'..'9', 'a'..'z']);
end;

function LooksLikeMessier(const Q: string): Boolean;
var
  I: Integer;
begin
  Result := False;
  if Length(Q) < 2 then Exit;
  if Q[1] <> 'm' then Exit;
  for I := 2 to Length(Q) do
    if not (Q[I] in ['0'..'9']) then Exit;
  Result := True;
end;

function ResolveName(const ANames: array of string; const AQuery: string;
  out ACanonical: string; out ACandidates: specialize TArray<string>): Integer;
var
  I, Pass, Count, MatchIdx: Integer;
  Q, LN: string;
begin
  Result := -1;
  ACanonical := '';
  SetLength(ACandidates, 0);
  Q := LowerCase(AQuery);
  for I := 0 to High(ANames) do
    if LowerCase(ANames[I]) = Q then
    begin
      ACanonical := ANames[I];
      Exit(I);
    end;
  // Messier numbers are resolved before prefix matching: "M8" is a prefix of
  // "M80", "M81", ... and without this `--select M8` silently resolved to the
  // WRONG OBJECT (M80) and aimed the mount at it. Mirrors object_name.h.
  if LooksLikeMessier(Q) then
  begin
    Count := 0;
    MatchIdx := -1;
    for I := 0 to High(ANames) do
      if IsMessierObject(ANames[I], Q) then
      begin
        Inc(Count);
        MatchIdx := I;
      end;
    if Count = 1 then
    begin
      ACanonical := ANames[MatchIdx];
      Exit(MatchIdx);
    end;
    if Count > 1 then
    begin
      SetLength(ACandidates, Count);
      Count := 0;
      for I := 0 to High(ANames) do
        if IsMessierObject(ANames[I], Q) then
        begin
          ACandidates[Count] := ANames[I];
          Inc(Count);
        end;
      Exit(-1);
    end;
  end;
  for Pass := 0 to 1 do // 0 = prefix, 1 = substring
  begin
    Count := 0;
    MatchIdx := -1;
    for I := 0 to High(ANames) do
    begin
      LN := LowerCase(ANames[I]);
      if ((Pass = 0) and (Copy(LN, 1, Length(Q)) = Q))
         or ((Pass = 1) and (Pos(Q, LN) > 0)) then
      begin
        Inc(Count);
        MatchIdx := I;
      end;
    end;
    if Count = 1 then
    begin
      ACanonical := ANames[MatchIdx];
      Exit(MatchIdx);
    end;
    if Count > 1 then
    begin
      SetLength(ACandidates, Count);
      Count := 0;
      for I := 0 to High(ANames) do
      begin
        LN := LowerCase(ANames[I]);
        if ((Pass = 0) and (Copy(LN, 1, Length(Q)) = Q))
           or ((Pass = 1) and (Pos(Q, LN) > 0)) then
        begin
          ACandidates[Count] := ANames[I];
          Inc(Count);
        end;
      end;
      Exit(-1);
    end;
  end;
end;

function LeapYearP(Y: Integer): Boolean;
begin
  Result := ((Y mod 4 = 0) and (Y mod 100 <> 0)) or (Y mod 400 = 0);
end;

function DaysInMonthP(Y, M: Integer): Integer;
const
  D: array[1..12] of Integer = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31);
begin
  if (M = 2) and LeapYearP(Y) then Result := 29 else Result := D[M];
end;

function ParseDateArg(const AValue: string; out Y, Mo, D: Double; out AErrMsg: string): Boolean;
var
  YY, MM, DD, Code1, Code2, Code3: Integer;
begin
  Result := False;
  AErrMsg := '';
  if Length(AValue) <> 10 then
  begin
    AErrMsg := 'Error: invalid --date "' + AValue + '"; expected YYYY-MM-DD';
    Exit;
  end;
  if (AValue[5] <> '-') or (AValue[8] <> '-') then
  begin
    AErrMsg := 'Error: invalid --date "' + AValue + '"; expected YYYY-MM-DD';
    Exit;
  end;
  Val(Copy(AValue, 1, 4), YY, Code1);
  Val(Copy(AValue, 6, 2), MM, Code2);
  Val(Copy(AValue, 9, 2), DD, Code3);
  if (Code1 <> 0) or (Code2 <> 0) or (Code3 <> 0)
     or (YY < 1) or (YY > 9999) or (MM < 1) or (MM > 12)
     or (DD < 1) or (DD > DaysInMonthP(YY, MM)) then
  begin
    AErrMsg := 'Error: --date "' + AValue + '" is not a real calendar date';
    Exit;
  end;
  Y := YY; Mo := MM; D := DD;
  Result := True;
end;

function ParseTimeArg(const AValue: string; out H, Mi, S: Double; out AErrMsg: string): Boolean;
var
  HH, MM, SS, Code1, Code2, Code3: Integer;
  HasSec: Boolean;
begin
  Result := False;
  AErrMsg := '';
  HasSec := Length(AValue) = 8;
  if (Length(AValue) <> 5) and not HasSec then
  begin
    AErrMsg := 'Error: invalid --time "' + AValue + '"; expected HH:MM or HH:MM:SS';
    Exit;
  end;
  if AValue[3] <> ':' then
  begin
    AErrMsg := 'Error: invalid --time "' + AValue + '"; expected HH:MM or HH:MM:SS';
    Exit;
  end;
  if HasSec and (AValue[6] <> ':') then
  begin
    AErrMsg := 'Error: invalid --time "' + AValue + '"; expected HH:MM or HH:MM:SS';
    Exit;
  end;
  Val(Copy(AValue, 1, 2), HH, Code1);
  Val(Copy(AValue, 4, 2), MM, Code2);
  SS := 0; Code3 := 0;
  if HasSec then
    Val(Copy(AValue, 7, 2), SS, Code3);
  if (Code1 <> 0) or (Code2 <> 0) or (Code3 <> 0)
     or (HH < 0) or (HH > 23) or (MM < 0) or (MM > 59)
     or (HasSec and ((SS < 0) or (SS > 59))) then
  begin
    AErrMsg := 'Error: --time "' + AValue + '" is out of range (HH 0-23, MM/SS 0-59)';
    Exit;
  end;
  H := HH; Mi := MM;
  if HasSec then S := SS else S := 0;
  Result := True;
end;

end.
