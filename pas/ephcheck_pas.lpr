program ephcheck_pas;

{$mode objfpc}{$H+}

// ephcheck_pas - Pascal port self-check. Runs the SAME two-epoch reference
// values as tools/ephcheck.cpp (make check) against the astrop.pas
// implementation, so the GUI's math is verified against the same truth.
// Built & run by: make check-gui

uses
  SysUtils, Math, astrop in 'astrop.pas';

type
  TCheck = record
    Name: string;
    RAHours, DecDeg, Mag: Double;
    TolDeg, TolMag: Double;
  end;

const
  // Epoch 1: 2026-09-23 00:00 UT. Planets + minor bodies = JPL Horizons
  // geocentric apparent, fetched 2026-10. Matches tools/ephcheck.cpp.
  Checks1: array[0..11] of TCheck = (
    (Name:'Mercury';  RAHours:13.17889; DecDeg:-8.31594; Mag:-0.25; TolDeg:0.03; TolMag:1.8),
    (Name:'Venus';    RAHours:14.13846; DecDeg:-19.56797;Mag:-4.80; TolDeg:0.03; TolMag:1.0),
    (Name:'Mars';     RAHours: 7.94607; DecDeg:21.62779; Mag: 1.09; TolDeg:0.03; TolMag:0.6),
    (Name:'Jupiter';  RAHours: 9.38258; DecDeg:15.94878; Mag:-1.85; TolDeg:0.03; TolMag:0.2),
    (Name:'Saturn';   RAHours: 0.81817; DecDeg: 2.33028; Mag: 0.38; TolDeg:0.03; TolMag:0.4),
    (Name:'Uranus';   RAHours: 4.25043; DecDeg:21.09204; Mag: 5.67; TolDeg:0.03; TolMag:0.2),
    (Name:'Neptune';  RAHours: 0.22624; DecDeg:-0.07649; Mag: 7.68; TolDeg:0.03; TolMag:0.3),
    (Name:'Ceres';    RAHours: 7.00599; DecDeg:23.068;  Mag: 8.75; TolDeg:0.15; TolMag:0.2),
    (Name:'Pluto';    RAHours:20.43811; DecDeg:-23.620; Mag:14.55; TolDeg:0.15; TolMag:0.2),
    (Name:'Haumea';   RAHours:14.67783; DecDeg:13.635;  Mag:17.27; TolDeg:0.15; TolMag:0.2),
    (Name:'Makemake'; RAHours:13.44748; DecDeg:19.762;  Mag:17.11; TolDeg:0.15; TolMag:0.2),
    (Name:'Eris';     RAHours: 1.83913; DecDeg: 0.113;  Mag:18.59; TolDeg:0.15; TolMag:0.2));

  // Epoch 2: 2027-03-23 00:00 UT. JPL Horizons geocentric apparent,
  // fetched 2026-10. Matches tools/ephcheck.cpp.
  Checks2: array[0..11] of TCheck = (
    (Name:'Mercury';  RAHours:22.51103; DecDeg:-11.15986;Mag: 0.14; TolDeg:0.03; TolMag:1.8),
    (Name:'Venus';    RAHours:21.89372; DecDeg:-13.35065;Mag:-3.97; TolDeg:0.03; TolMag:1.0),
    (Name:'Mars';     RAHours: 9.67311; DecDeg:17.74024; Mag:-0.60; TolDeg:0.03; TolMag:0.6),
    (Name:'Jupiter';  RAHours: 9.36495; DecDeg:16.54007; Mag:-2.40; TolDeg:0.03; TolMag:0.2),
    (Name:'Saturn';   RAHours: 1.02141; DecDeg: 4.11077; Mag: 0.74; TolDeg:0.03; TolMag:0.4),
    (Name:'Uranus';   RAHours: 4.02735; DecDeg:20.53354; Mag: 5.76; TolDeg:0.03; TolMag:0.2),
    (Name:'Neptune';  RAHours: 0.28100; DecDeg: 0.35657; Mag: 7.82; TolDeg:0.03; TolMag:0.3),
    (Name:'Ceres';    RAHours: 6.98014; DecDeg:31.564;  Mag: 8.16; TolDeg:0.15; TolMag:0.2),
    (Name:'Pluto';    RAHours:20.68503; DecDeg:-22.860; Mag:14.64; TolDeg:0.15; TolMag:0.2),
    (Name:'Haumea';   RAHours:14.81068; DecDeg:13.898;  Mag:17.20; TolDeg:0.15; TolMag:0.2),
    (Name:'Makemake'; RAHours:13.52949; DecDeg:20.214;  Mag:17.04; TolDeg:0.15; TolMag:0.2),
    (Name:'Eris';     RAHours: 1.80765; DecDeg: 0.247;  Mag:18.62; TolDeg:0.15; TolMag:0.2));

var
  Failures: Integer;

function OkStr(Ok: Boolean): string;
begin
  if Ok then Result := 'ok  ' else Result := 'FAIL';
end;

procedure CheckEpoch(JD: Double; const Label_: string; const Checks: array of TCheck);
var
  Planets: specialize TArray<TPlanetaryObject>;
  I, J: Integer;
  Found: Boolean;
  DRa, Sep, DMag: Double;
  Ok: Boolean;
begin
  Planets := LoadPlanets('planetary_objects.csv', JD);
  for I := 0 to High(Checks) do
  begin
    Found := False;
    for J := 0 to High(Planets) do
      if Planets[J].Name = Checks[I].Name then
      begin
        DRa := (Planets[J].RAHours - Checks[I].RAHours) * 15.0;
        Sep := Sqrt(DRa * DRa + Sqr(Planets[J].DecDeg - Checks[I].DecDeg));
        DMag := Abs(Planets[J].Magnitude - Checks[I].Mag);
        Ok := (Sep <= Checks[I].TolDeg) and (DMag <= Checks[I].TolMag);
        if not Ok then Inc(Failures);
        WriteLn(Format('%s %-9s pos err %5.2fdeg (tol %.2f)  mag %.2f vs %.2f  [%s]',
          [OkStr(Ok), Checks[I].Name, Sep, Checks[I].TolDeg,
           Planets[J].Magnitude, Checks[I].Mag, Label_]));
        Found := True;
        Break;
      end;
    if not Found then
    begin
      WriteLn(Format('FAIL %-9s not found in results (%s)', [Checks[I].Name, Label_]));
      Inc(Failures);
    end;
  end;
end;

var
  Moon: specialize TArray<TPlanetaryObject>;
  J: Integer;
  Sep: Double;
  MoonOk: Boolean;
begin
  Failures := 0;

  CheckEpoch(JulianDate(2026, 9, 23, 0, 0, 0), '2026-09-23', Checks1);
  CheckEpoch(JulianDate(2027, 3, 23, 0, 0, 0), '2027-03-23', Checks2);

  // Moon vs JPL Horizons geocentric apparent at epoch 1 (tol 8 arcmin)
  Moon := LoadPlanets('planetary_objects.csv', JulianDate(2026, 9, 23, 0, 0, 0));
  for J := 0 to High(Moon) do
    if Moon[J].Name = 'Moon' then
    begin
      Sep := Sqrt(Sqr((Moon[J].RAHours - 21.23652) * 15.0) + Sqr(Moon[J].DecDeg - (-17.33810)));
      MoonOk := Sep <= 8.0 / 60.0;
      if not MoonOk then Inc(Failures);
      WriteLn(Format('%s %-9s pos err %5.2fdeg (tol 0.13)',
        [OkStr(MoonOk), 'Moon', Sep]));
      Break;
    end;

  if Failures > 0 then
  begin
    WriteLn(Format(#10'%d check(s) FAILED', [Failures]));
    Halt(1);
  end;
  WriteLn(#10'All Pascal checks passed (both epochs).');
end.
