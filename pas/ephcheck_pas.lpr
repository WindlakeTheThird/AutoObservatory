program ephcheck_pas;

{$mode objfpc}{$H+}

// ephcheck_pas - Pascal port self-check. Runs the SAME two-epoch reference
// values as tools/ephcheck.cpp (make check) against the astrop.pas
// implementation, so the GUI's math is verified against the same truth.
// Built & run by: make check-gui

uses
  SysUtils, Math, astrop in 'astrop.pas', ephcheck_refs;

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
