program viewcheck_pas;

{$mode objfpc}{$H+}

// viewcheck_pas.lpr - Pascal twin of tools/viewcheck.cpp: pins ProjectSky in
// astrop.pas (used by the Pascal GUI) to exactly the behaviour of sky_view.h,
// which the terminal planetarium uses. The default full-sky view must
// reproduce the legacy polar projection on a grid of azimuth/altitude, and the
// orientation, linear zoom, out-of-field rejection and --fov/--center rules
// are checked too. Any divergence between the two ports fails one of them.
//
// Build & run via:  make check-view   (from the project root)

uses
  SysUtils, Math, astrop;

var
  Failures: Integer;

function BS(b: Boolean): string;
begin
  if b then Result := 'true' else Result := 'false';
end;

// FPC's Format has no '%+' flag, so sign the number by hand.
function Num(x: Double): string;
begin
  if x >= 0 then Result := Format('+%.6f', [x]) else Result := Format('%.6f', [x]);
end;

procedure ExpectNear(const ALabel: string; Got, Want: Double; Tol: Double = 1e-9);
var
  Err: Double;
begin
  Err := Abs(Got - Want);
  if Err > Tol then
  begin
    Inc(Failures);
    WriteLn(Format('FAIL %-46s got %s want %s (err %.2e)', [ALabel, Num(Got), Num(Want), Err]));
  end
  else
    WriteLn(Format('ok   %-46s got %s want %s', [ALabel, Num(Got), Num(Want)]));
end;

procedure ExpectBool(const ALabel: string; Got, Want: Boolean);
begin
  if Got <> Want then
  begin
    Inc(Failures);
    WriteLn(Format('FAIL %-46s got %s want %s', [ALabel, BS(Got), BS(Want)]));
  end
  else
    WriteLn(Format('ok   %-46s got %s', [ALabel, BS(Got)]));
end;

// The original planetarium GUI projection, in normalised drawing units.
procedure LegacyOffset(Az, Alt: Double; out U, V: Double);
var
  Rr, A: Double;
begin
  Rr := 1.0 - Alt / 90.0;
  A := DegToRad(90.0 - Az);
  U := Rr * Cos(A);
  V := Rr * Sin(A);
end;

var
  Full, Z: TSkyView;
  Az, Alt, U, V, LU, LV: Double;
begin
  Failures := 0;
  Full := FullSkyView;

  // 1. Default view == the legacy polar projection, exactly, everywhere.
  Az := 0.0;
  while Az < 360.0 do
  begin
    Alt := 0.0;
    while Alt <= 90.0 do
    begin
      if not ProjectSky(Full, Az, Alt, U, V) then
      begin
        Inc(Failures);
        WriteLn(Format('FAIL default view rejected az %.1f alt %.1f', [Az, Alt]));
      end
      else
      begin
        LegacyOffset(Az, Alt, LU, LV);
        if (Abs(U - LU) > 1e-9) or (Abs(V - LV) > 1e-9) then
        begin
          Inc(Failures);
          WriteLn(Format('FAIL legacy mismatch az %6.1f alt %5.1f: got (%s,%s) want (%s,%s)',
            [Az, Alt, Num(U), Num(V), Num(LU), Num(LV)]));
        end;
      end;
      Alt := Alt + 5.0;
    end;
    Az := Az + 7.0;
  end;
  WriteLn(Format('%s default view matches the legacy polar projection on the grid',
    [BS(Failures = 0)]));

  // 2. Orientation at the default centre: N up, E right, S down, W left.
  ProjectSky(Full, 0.0, 0.0, U, V);   ExpectNear('N horizon  u', U, 0.0);  ExpectNear('N horizon  v', V, 1.0);
  ProjectSky(Full, 90.0, 0.0, U, V);  ExpectNear('E horizon  u', U, 1.0);  ExpectNear('E horizon  v', V, 0.0);
  ProjectSky(Full, 180.0, 0.0, U, V); ExpectNear('S horizon  u', U, 0.0);  ExpectNear('S horizon  v', V, -1.0);
  ProjectSky(Full, 270.0, 0.0, U, V); ExpectNear('W horizon  u', U, -1.0); ExpectNear('W horizon  v', V, 0.0);
  ProjectSky(Full, 123.0, 90.0, U, V); ExpectNear('zenith     u', U, 0.0); ExpectNear('zenith     v', V, 0.0);

  // 3. Zoom: linear field radius, edge kept, past the edge rejected.
  Z := FullSkyView;
  Z.Fov := 40.0;  // half-field 20 deg
  ProjectSky(Z, 0.0, 80.0, U, V);  ExpectNear('zoom fov40 alt80 v (half radius)', V, 0.5);
  ExpectBool('zoom fov40 alt70 (on the edge) is kept', ProjectSky(Z, 0.0, 70.0, U, V), True);
  ExpectNear('zoom fov40 alt70 v (edge)', V, 1.0);
  ExpectBool('zoom fov40 alt69 (past the edge) is rejected', ProjectSky(Z, 0.0, 69.0, U, V), False);

  // The centre of an off-zenith view projects to (0, 0).
  Z := FullSkyView;
  Z.CenterAz := 210.0; Z.CenterAlt := 30.0; Z.Fov := 25.0;
  ExpectBool('centre point is inside the view', ProjectSky(Z, 210.0, 30.0, U, V), True);
  ExpectNear('centre u', U, 0.0);
  ExpectNear('centre v', V, 0.0);
  ExpectBool('60 deg away is outside a 25 deg field', ProjectSky(Z, 210.0, 90.0, U, V), False);
  ExpectBool('10 deg away is inside a 25 deg field', ProjectSky(Z, 210.0, 40.0, U, V), True);

  // 4. --fov / --center validation.
  ExpectBool('fov 0 invalid', ValidFov(0.0), False);
  ExpectBool('fov -5 invalid', ValidFov(-5.0), False);
  ExpectBool('fov 180 invalid (full sky)', ValidFov(180.0), False);
  ExpectBool('fov 200 invalid', ValidFov(200.0), False);
  ExpectBool('fov 179.9 valid', ValidFov(179.9), True);
  ExpectBool('fov 40 valid', ValidFov(40.0), True);

  ExpectBool('az -1 invalid', ValidCenter(-1.0, 45.0), False);
  ExpectBool('az 360 invalid', ValidCenter(360.0, 45.0), False);
  ExpectBool('az 359.9 valid', ValidCenter(359.9, 45.0), True);
  ExpectBool('alt 91 invalid', ValidCenter(45.0, 91.0), False);
  ExpectBool('alt -91 invalid', ValidCenter(45.0, -91.0), False);
  ExpectBool('alt -90 valid', ValidCenter(45.0, -90.0), True);
  ExpectBool('alt 90 valid', ValidCenter(45.0, 90.0), True);

  ExpectBool('full sky is not limited', IsLimitedView(FullSkyView), False);
  Z := FullSkyView; Z.Fov := 60.0;      ExpectBool('smaller fov is limited', IsLimitedView(Z), True);
  Z := FullSkyView; Z.CenterAlt := 45.0; ExpectBool('off-zenith is limited', IsLimitedView(Z), True);

  if Failures <> 0 then
  begin
    WriteLn(Format('%d check(s) FAILED', [Failures]));
    Halt(1);
  end;
  WriteLn('All sky-view checks passed.');
end.
