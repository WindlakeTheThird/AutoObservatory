program horzcheck_pas;

// horzcheck_pas - Pascal mirror of tools/horzcheck.cpp (make check-horiz).
//
// Verifies the HORIZONTAL-coordinate path in astrop.pas: J2000 catalog
// coordinates -> PrecessRaDecJ2000ToDate -> HorizontalCoords, against the same
// astropy 8.0.1 apparent az/alt reference the C++ check uses. Also runs a
// NEGATIVE CONTROL (precession skipped must FAIL) so the tolerance cannot be
// widened into meaninglessness.

{$mode objfpc}{$H+}

uses
  SysUtils, Math, astrop in 'astrop.pas';

type
  THCheck = record
    Name: string;
    RAH, RAM, RAS: Double;
    DecSign, DecD, DecM, DecS: Double;
    Az, Alt: Double;
  end;

const
  kTolDeg = 0.05;

  // 45N 10E, 21:00 UTC. Reference: astropy 8.0.1 AltAz (ICRS apparent).
  Epoch1: array[0..4] of THCheck = (
    (Name:'Vega';     RAH:18; RAM:36; RAS:56; DecSign: 1; DecD:38; DecM:47; DecS: 0; Az:277.48198; Alt: 54.09447),
    (Name:'Capella';  RAH: 5; RAM:16; RAS:59; DecSign: 1; DecD:45; DecM:59; DecS: 0; Az: 42.89897; Alt: 18.94123),
    (Name:'Arcturus'; RAH:14; RAM:15; RAS:10; DecSign: 1; DecD:19; DecM:10; DecS: 0; Az:299.92482; Alt: -2.12435),
    (Name:'Polaris';  RAH: 2; RAM:31; RAS: 0; DecSign: 1; DecD:89; DecM:15; DecS:36; Az:  0.87709; Alt: 45.11718),
    (Name:'Antares';  RAH:16; RAM:29; RAS:24; DecSign:-1; DecD:26; DecM:25; DecS:54; Az:244.21621; Alt:-11.83510));

  Epoch2: array[0..4] of THCheck = (
    (Name:'Vega';     RAH:18; RAM:36; RAS:56; DecSign: 1; DecD:38; DecM:47; DecS: 0; Az: 34.61148; Alt:  3.72026),
    (Name:'Capella';  RAH: 5; RAM:16; RAS:59; DecSign: 1; DecD:45; DecM:59; DecS: 0; Az:296.12006; Alt: 44.87709),
    (Name:'Arcturus'; RAH:14; RAM:15; RAS:10; DecSign: 1; DecD:19; DecM:10; DecS: 0; Az: 91.29588; Alt: 28.75631),
    (Name:'Polaris';  RAH: 2; RAM:31; RAS: 0; DecSign: 1; DecD:89; DecM:15; DecS:36; Az:359.13575; Alt: 44.89011),
    (Name:'Antares';  RAH:16; RAM:29; RAS:24; DecSign:-1; DecD:26; DecM:25; DecS:54; Az:102.08159; Alt:-26.30892));

function OkBool(B: Boolean): string;
begin
  if B then OkBool := 'ok  ' else OkBool := 'FAIL';
end;

function SepDeg(Az1, Alt1, Az2, Alt2: Double): Double;
var
  A1, A2, C: Double;
begin
  A1 := DegToRad(Alt1); A2 := DegToRad(Alt2);
  C := Sin(A1)*Sin(A2) + Cos(A1)*Cos(A2)*Cos(DegToRad(Az2 - Az1));
  if C > 1.0 then C := 1.0;
  if C < -1.0 then C := -1.0;
  SepDeg := RadToDeg(ArcCos(C));
end;

function RunEpoch(const Checks: array of THCheck;
                  Y, Mo, D, H: Double; const Label_: string;
                  NoPrec: Boolean): Integer;
var
  I: Integer;
  RA, Dec, Az, Alt, S: Double;
  Ok: Boolean;
  JD: Double;
begin
  RunEpoch := 0;
  JD := JulianDate(Y, Mo, D, H, 0, 0);
  for I := 0 to High(Checks) do
  begin
    RA := Checks[I].RAH + Checks[I].RAM / 60.0 + Checks[I].RAS / 3600.0;
    Dec := Checks[I].DecSign *
           (Checks[I].DecD + Checks[I].DecM / 60.0 + Checks[I].DecS / 3600.0);
    if not NoPrec then
      PrecessRaDecJ2000ToDate(JD, RA, Dec);
    HorizontalCoords(Y, Mo, D, H, 0, 0, 45.0, 10.0, RA, Dec, Az, Alt);
    S := SepDeg(Az, Alt, Checks[I].Az, Checks[I].Alt);
    Ok := S <= kTolDeg;
    if not Ok then Inc(RunEpoch);
    WriteLn(Format('%s %-9s sep %7.4f deg (tol %.2f)  az %8.3f vs %8.3f  alt %8.3f vs %8.3f  [%s]',
      [OkBool(Ok), Checks[I].Name, S, kTolDeg, Az, Checks[I].Az, Alt, Checks[I].Alt, Label_]));
  end;
end;

var
  Failures, Ctrl: Integer;
begin
  WriteLn('== horizontal-coordinate check, Pascal (J2000 -> precess -> az/alt) ==');
  Failures := RunEpoch(Epoch1, 2026, 9, 23, 21, '2026-09-23', False)
            + RunEpoch(Epoch2, 2027, 3, 23, 21, '2027-03-23', False);

  WriteLn('== negative control: same data WITHOUT precession (must FAIL) ==');
  Ctrl := RunEpoch(Epoch1, 2026, 9, 23, 21, 'no-precession', True);
  if Ctrl = 0 then
  begin
    WriteLn;
    WriteLn('ERROR: negative control passed -- tolerance too loose to detect the');
    WriteLn('       missing-precession bug. Tighten kTolDeg.');
    Halt(1);
  end;
  WriteLn(Format('negative control correctly failed (%d/%d outside tolerance).',
    [Ctrl, Length(Epoch1)]));

  if Failures > 0 then
  begin
    WriteLn;
    WriteLn(Format('%d horizontal-coordinate check(s) FAILED', [Failures]));
    Halt(1);
  end;
  WriteLn;
  WriteLn('All Pascal horizontal-coordinate checks passed.');
end.