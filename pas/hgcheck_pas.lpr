// hgcheck_pas.lpr - frozen-value unit test for the Pascal H-G phase function.
//
// Pins HGPhase() in astrop.pas against the SAME table as tools/hgcheck.cpp:
// if the C++ and Pascal implementations ever diverge, one of these checks
// fails. Run via: make check-hg
program hgcheck_pas;
{$mode objfpc}{$H+}
uses SysUtils, Math, astrop;

type
  THGCase = record
    G, AlphaDeg, Phi: Double;
  end;

const
  Cases: array[0..18] of THGCase = (
    // alpha = 0: Phi1 = Phi2 = 1 -> Phi = 1 regardless of G
    (G:0.05; AlphaDeg:  0.0; Phi:1.000000),
    (G:0.15; AlphaDeg:  0.0; Phi:1.000000),
    (G:0.32; AlphaDeg:  0.0; Phi:1.000000),
    // G = 0.15 (standard TNO slope) at several phase angles
    (G:0.15; AlphaDeg:  1.0; Phi:0.867725),
    (G:0.15; AlphaDeg:  3.0; Phi:0.754410),
    (G:0.15; AlphaDeg:  6.0; Phi:0.648057),
    (G:0.15; AlphaDeg: 10.0; Phi:0.551047),
    (G:0.15; AlphaDeg: 20.0; Phi:0.398244),
    (G:0.15; AlphaDeg: 40.0; Phi:0.232982),
    (G:0.15; AlphaDeg: 80.0; Phi:0.076252),
    (G:0.15; AlphaDeg:120.0; Phi:0.011557),
    // other slopes: low-G (asteroid-like, Phi1-weighted) and high-G
    (G:0.05; AlphaDeg: 10.0; Phi:0.508968),
    (G:0.05; AlphaDeg: 40.0; Phi:0.192170),
    (G:0.32; AlphaDeg: 10.0; Phi:0.622580),
    (G:0.32; AlphaDeg: 40.0; Phi:0.302362),
    (G:0.11; AlphaDeg: 20.0; Phi:0.379411),
    (G:0.28; AlphaDeg:  6.0; Phi:0.694246),
    // G <= 0 must short-circuit to 1 (major planets: no minor-body model)
    (G:0.00; AlphaDeg: 20.0; Phi:1.000000),
    (G:-0.10; AlphaDeg:40.0; Phi:1.000000));

var
  I: Integer;
  Phi, Err: Double;
  Failures: Integer;
begin
  Failures := 0;
  for I := 0 to High(Cases) do
  begin
    Phi := HGPhase(Cases[I].G, DegToRad(Cases[I].AlphaDeg));
    Err := Abs(Phi - Cases[I].Phi);
    if Err > 5e-5 then Inc(Failures);
    if Err > 5e-5 then
      WriteLn(Format('FAIL G=%.2f alpha=%5.1f  phi=%.6f expect %.6f  (err %.2e)',
        [Cases[I].G, Cases[I].AlphaDeg, Phi, Cases[I].Phi, Err]))
    else
      WriteLn(Format('ok    G=%.2f alpha=%5.1f  phi=%.6f expect %.6f  (err %.2e)',
        [Cases[I].G, Cases[I].AlphaDeg, Phi, Cases[I].Phi, Err]));
  end;
  if Failures > 0 then
  begin
    WriteLn(Format('%d check(s) FAILED', [Failures]));
    Halt(1);
  end;
  WriteLn('All H-G phase checks passed.');
end.
