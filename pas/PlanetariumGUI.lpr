program PlanetariumGUI;

{$mode objfpc}{$H+}

// Live sky-dome Form application (Lazarus LCL / Delphi VCL compatible structure).
// Polar projection like the ASCII planetarium: zenith center, horizon at edge,
// N top / E right / S bottom / W left. Live mode redraws on a TTimer.
//
// Build (Lazarus):   lazbuild PlanetariumGUI.lpi   -or-
//                    fpc -Fu. PlanetariumGUI.lpr
// Needs celestial_objects.csv and planetary_objects.csv in the working dir.
//
// Delphi: create a VCL form with the same controls (TForm + TPaintBox + TTimer
// + TTrackBar) and wire OnPaint/OnTimer identically; the AstroP unit is
// Delphi-compatible (only {$mode objfpc} + generics syntax are FPC-specific
// in trivial ways).


uses
  {$IFDEF UNIX}cthreads,{$ENDIF}
  Interfaces, Forms, Controls, StdCtrls, ExtCtrls, ComCtrls, Graphics, Classes,
  Menus, SysUtils, Unix, Math, astrop in 'astrop.pas';

type
  TSkyPoint = record
    Name: string;
    Az, Alt, Mag: Double;
    Sym: Char;
    IsPlanet: Boolean;
    Bayer: string; // Bayer designation (e.g. Alpha_Boo) for stars; empty for clusters
    ScreenX, ScreenY: Integer; // last drawn pixel position (for hit-testing)
    Visible: Boolean;
  end;
  { TSkyForm }

  TSkyForm = class(TForm)
  public
    PaintBox: TPaintBox;
    Timer: TTimer;
    MagTrack: TTrackBar;
    MagLabel: TLabel;
    TargetLabel: TLabel;
    IntervalLabel: TLabel;
    Sky: array of TSkyPoint;
    MagLimit: Double;
    LastSecond: Integer;
    Selected: Integer;       // index into Sky of clicked/confirmed object (-1 = none)
    HoverIdx: Integer;       // index into Sky of mouse-over object (-1 = none)
    KeyCursor: Integer;      // keyboard cursor into Sky (-1 = none), n/p move it, t confirms
    ObsLat, ObsLon: Double;  // observer position, degrees (from argv)
    // Fixed-time override (--date/--time). FixedJD = NaN means "use real clock".
    FixedJD: Double;
    UseFixed: Boolean;
    CurrentJD: Double;   // JD of the displayed frame (fixed or live)
    LegendVisible: Boolean; // L (or View menu) toggles the right-hand legend column
    ViewMenu: TMenuItem;    // top-level View menu (nil when menus unavailable)
    ViewMenuItem: TMenuItem; // the 'Legend column' checkable item
    View: TSkyView;         // limited (zoomed) field of view; default = full sky
    Follow: Boolean;        // f key: keep View centred on Selected as the sky turns
    // Rectangular az/alt window mode (--window AZ1,AZ2,ALT1,ALT2, or the 'x'
    // key to toggle). When on it replaces the circular fov field: only objects
    // between the two azimuths (shortest arc) and two altitudes are drawn and
    // listed. RectOn = False means the circular/full-sky view above.
    RectOn: Boolean;
    Window: TRectWindow;
    ProjView: TSkyView;     // view ProjectPoint projects with (full sky in rect mode)
    DomeCx, DomeCy, DomeR: Double; // dome geometry, used by ProjectPoint
    constructor Create(AOwner: TComponent); override;
    procedure DoPaint(Sender: TObject);
    procedure DoTimer(Sender: TObject);
    procedure DoTrack(Sender: TObject);
    procedure DoMouseMove(Sender: TObject; Shift: TShiftState; X, Y: Integer);
    procedure DoClick(Sender: TObject);
    procedure DoKeyDown(Sender: TObject; var Key: Word; Shift: TShiftState);
    procedure MoveCursor(Step: Integer);
    function FindObject(const AName: string; out ACandidates: specialize TArray<string>): Integer;
    procedure SaveTarget(const AName: string);
    function ReadMountStatus(out MAz, MAlt, MAge: Double): Boolean;
    procedure UpdateSky;
    procedure DrawDome(C: TCanvas; W, H: Integer);
    // Project a sky position into dome pixels for the current View. False when
    // the point is outside the field of view (dome geometry from DrawDome).
    function ProjectPoint(Az, Alt: Double; out X, Y: Double): Boolean;
    function HitTest(X, Y: Integer): Integer;
    procedure ShowLegend;
    procedure GetUTC(out ST: TSystemTime);
    procedure DoToggleLegend(Sender: TObject);
  end;

const
  HEADER_H = 76;   // reserved band on top: header + tracking + view lines
  LEGEND_W = 240;  // right-hand column for the clickable legend

var
  SkyForm: TSkyForm;

{ TSkyForm }

// string-returning conditional (Math.IfThen is only overloaded for numbers)
function FlagStr(On: Boolean; const Text: string): string;
begin
  if On then Result := Text else Result := '';
end;

// Star cluster names get a digit shorthand on the dome (same rule as the C++
// planetarium). Matches the naming used in celestial_objects.csv.
function IsClusterName(const AName: string): Boolean;
begin
  Result := (Pos('Cluster', AName) > 0) or (Pos('Pleiades', AName) > 0)
         or (Pos('Hyades', AName) > 0) or (Pos('Beehive', AName) > 0);
end;

// Which legend group an object belongs to: 0 = Moon (the Sun is drawn on the
// dome but is not in Sky), 1 = planets & dwarf planets, 2 = stars & clusters.
function InLegendGroup(const P: TSkyPoint; Group: Integer): Boolean;
begin
  case Group of
    0: Result := P.Name = 'Moon';
    1: Result := P.IsPlanet and (P.Name <> 'Moon');
    2: Result := not P.IsPlanet;
  else
    Result := False;
  end;
end;

constructor TSkyForm.Create(AOwner: TComponent);
begin
  inherited CreateNew(AOwner);
  Caption := 'Live Planetarium (Pascal)';
  Width := 1000;  // wide enough for dome + 240px legend column side by side
  Height := 680;
  Position := poScreenCenter;
  Color := clBlack;
  Font.Color := clWhite;
  MagLimit := 4.0;
  LastSecond := -1;
  Selected := -1;
  HoverIdx := -1;
  KeyCursor := -1;
  LegendVisible := True;
  View := FullSkyView;
  Follow := False;
  RectOn := False;
  Window.Az1 := 0; Window.Az2 := 0; Window.Alt1 := 0; Window.Alt2 := 90;
  DomeCx := 0; DomeCy := 0; DomeR := 1;
  FixedJD := NaN;
  UseFixed := False;
  CurrentJD := 0;

  PaintBox := TPaintBox.Create(Self);
  PaintBox.Parent := Self;
  PaintBox.Align := alClient;
  PaintBox.OnPaint := @DoPaint;
  PaintBox.OnMouseMove := @DoMouseMove;
  PaintBox.OnClick := @DoClick;
  PaintBox.ShowHint := True;

  // keyboard target picking: n/p (or Tab) move the cursor, t confirms, c clears
  KeyPreview := True;
  OnKeyDown := @DoKeyDown;

  MagTrack := TTrackBar.Create(Self);
  MagTrack.Parent := Self;
  MagTrack.Align := alBottom;
  MagTrack.Min := -2;   // mag -1 .. 10 in 0.5 steps (x2)
  MagTrack.Max := 20;
  MagTrack.Position := 8;
  MagTrack.OnChange := @DoTrack;
  MagTrack.Height := 40;

  MagLabel := TLabel.Create(Self);
  MagLabel.Parent := Self;
  MagLabel.Align := alBottom;
  MagLabel.Caption := 'Mag limit: 4.0';

  TargetLabel := TLabel.Create(Self);
  TargetLabel.Parent := Self;
  TargetLabel.Align := alBottom;
  TargetLabel.Font.Color := clLime;
  TargetLabel.Caption := 'target: none  (click a star or legend line to select it)';

  Timer := TTimer.Create(Self);
  Timer.Interval := 500;
  Timer.OnTimer := @DoTimer;
  Timer.Enabled := True;

  // View menu: same toggle as the L key, for discoverability.
  ViewMenu := nil;
  ViewMenuItem := nil;
  if Menu = nil then
  begin
    Menu := TMainMenu.Create(Self);
    ViewMenu := TMenuItem.Create(Self);
    ViewMenu.Caption := 'View';
    ViewMenuItem := TMenuItem.Create(Self);
    ViewMenuItem.Caption := 'Legend column\tL';
    ViewMenuItem.Checked := True;
    ViewMenuItem.OnClick := @DoToggleLegend;
    ViewMenu.Add(ViewMenuItem);
    Menu.Items.Add(ViewMenu);
  end;

  UpdateSky;
end;

procedure TSkyForm.UpdateSky;
var
  NowT: TSystemTime;
  Y, Mo, D, H, Mi, S, JD, JD0: Double;
  Stars: specialize TArray<TCelestialObject>;
  Planets: specialize TArray<TPlanetaryObject>;
  I: Integer;
  Az, Alt: Double;
  Sym: Char;
  Counter, ClusterNum: Integer;
begin
  GetUTC(NowT);
  Y := NowT.Year; Mo := NowT.Month; D := NowT.Day;
  H := NowT.Hour; Mi := NowT.Minute; S := NowT.Second;
  if UseFixed then
  begin
    // Fixed-time override: recover calendar fields from the fixed JD.
    // Real clock is bypassed entirely.
    JD := FixedJD;
    JDToCalendar(JD, Y, Mo, D, H, Mi, S);
  end
  else
    JD := JulianDate(Y, Mo, D, H, Mi, S);
  CurrentJD := JD;

  SetLength(Sky, 0);
  Counter := 0;

  Stars := LoadStars('celestial_objects.csv');
  ClusterNum := 1;
  for I := 0 to High(Stars) do
  begin
    HorizontalCoords(Y, Mo, D, H, Mi, S, ObsLat, ObsLon, Stars[I].RAHours, Stars[I].DecDeg, Az, Alt);
    // Shorthand on the dome: letters for the brightest stars, digits for the
    // brightest star clusters - same scheme as the C++ planetarium.
    if (Stars[I].Magnitude <= 1.5) and (Alt > 0) then
    begin
      if IsClusterName(Stars[I].Name) then
      begin
        if ClusterNum <= 9 then
          Sym := Chr(Ord('0') + ClusterNum)
        else
          Sym := Chr(Ord('a') + ClusterNum - 10);
        Inc(ClusterNum);
      end
      else
      begin
        if Counter < 26 then
          Sym := Chr(Ord('A') + Counter)
        else
          Sym := Chr(Ord('a') + Counter - 26);
        Inc(Counter);
      end;
    end
    else
      Sym := '.';
    SetLength(Sky, Length(Sky) + 1);
    Sky[High(Sky)].Name := Stars[I].Name;
    Sky[High(Sky)].Az := Az;
    Sky[High(Sky)].Alt := Alt;
    Sky[High(Sky)].Mag := Stars[I].Magnitude;
    Sky[High(Sky)].Sym := Sym;
    Sky[High(Sky)].IsPlanet := False;
    Sky[High(Sky)].Bayer := Stars[I].Bayer;
  end;

  Planets := LoadPlanets('planetary_objects.csv', JD);
  for I := 0 to High(Planets) do
  begin
    HorizontalCoords(Y, Mo, D, H, Mi, S, ObsLat, ObsLon, Planets[I].RAHours, Planets[I].DecDeg, Az, Alt);
    if Planets[I].Name = 'Moon' then
      Sym := 'O'
    else
      Sym := LowerCase(Planets[I].Name)[1];
    SetLength(Sky, Length(Sky) + 1);
    Sky[High(Sky)].Name := Planets[I].Name;
    Sky[High(Sky)].Az := Az;
    Sky[High(Sky)].Alt := Alt;
    Sky[High(Sky)].Mag := Planets[I].Magnitude;
    Sky[High(Sky)].Sym := Sym;
    Sky[High(Sky)].IsPlanet := True;
    Sky[High(Sky)].Bayer := '';
  end;

  LastSecond := NowT.Second;
end;

procedure TSkyForm.DoTimer(Sender: TObject);
var
  NowSec: Integer;
begin
  NowSec := GetTickCount64 div 1000;
  if NowSec <> LastSecond then
  begin
    UpdateSky;
    LastSecond := NowSec;
    PaintBox.Invalidate;
  end;
end;

procedure TSkyForm.DoTrack(Sender: TObject);
begin
  MagLimit := MagTrack.Position;
  MagLabel.Caption := Format('Mag limit: %.1f', [MagLimit]);
  PaintBox.Invalidate;
end;

// Map a sky position into dome pixels for the current View. False when the
// point is outside the field of view. Mirrors ProjectSky in sky_view.h /
// astrop.pas: the angular distance from the view centre maps linearly to the
// drawing radius, keeping the planisphere orientation (N up, E right).
function TSkyForm.ProjectPoint(Az, Alt: Double; out X, Y: Double): Boolean;
var
  U, Vv: Double;
begin
  Result := ProjectSky(ProjView, Az, Alt, U, Vv);
  if not Result then Exit;
  X := DomeCx + U * DomeR;
  Y := DomeCy - Vv * DomeR;
end;

procedure TSkyForm.DrawDome(C: TCanvas; W, H: Integer);
const
  CardLabels: array[0..3] of Char = ('N', 'E', 'S', 'W');
var
  CX, CY, R: Double;
  I: Integer;
  X, Y: Double;
  DX, DY, Len: Double;
  Ok, Prev: Boolean;
  P: TSkyPoint;
  Size: Integer;
  Ilum: Double;
  SunRAH, SunDecD, SunAz, SunAlt: Double;
  NowT: TSystemTime;
  JD, MoonRAH, MoonDecD: Double;
  Planets: specialize TArray<TPlanetaryObject>;
  SunRA2, SunDec2, SunAz2, SunAlt2, Rise, SetT, CRise, CSet: Double;
  Y2, Mo2, D2, H2, Mi2, S2: Double;
  MAz, MAlt, MAge, DAz, DAlt, Sep: Double;
  Line3: string;
  Flag: Char;
  HeaderTop, DomeW: Integer;
  Group, Count: Integer;
  EntryName, FollowTxt: string;
begin
  // Follow mode: keep the view centred on the locked target so the limited
  // field tracks it as the sky turns. Sky[] was refreshed by UpdateSky for the
  // displayed frame, so this recentres using the object's current position.
  if Follow and (Selected >= 0) and (Selected <= High(Sky)) then
  begin
    View.CenterAz := NormalizeDeg(Sky[Selected].Az);
    View.CenterAlt := Sky[Selected].Alt;
  end;
  C.Brush.Style := bsClear;
  C.Pen.Color := clGray;
  // Layout: the top HEADER_H pixels are a text band (header + tracking), the
  // next LEGEND_W pixels on the right belong to the legend column; the dome is
  // centered in what remains, so nothing overlaps and nothing sits in a
  // cramped corner.
  HeaderTop := 4;
  if LegendVisible then DomeW := W - LEGEND_W else DomeW := W;
  CX := DomeW / 2.0;
  CY := HeaderTop + HEADER_H + (H - HeaderTop - HEADER_H) / 2.0;
  R := Min(CX, CY - HeaderTop - HEADER_H - 24.0) - 12.0;
  if R < 40 then R := 40; // tiny windows: keep a usable dome
  DomeCx := CX; DomeCy := CY; DomeR := R; // publish for ProjectPoint
  // Rect window mode projects the full sky (the window edges do the limiting);
// otherwise the projection carries the circular fov field.
  if RectOn then ProjView := FullSkyView else ProjView := View;

  // Frame of the drawing: circular field boundary (which is the horizon in
  // the full-sky view), or the straight az/alt window edges in rect mode.
  C.Pen.Color := clGray;
  if RectOn then
  begin
    // walk the four edges of the az/alt window (sampled, since a constant-
    // altitude edge is not a straight line on the dome)
    for I := 0 to 719 do
    begin
      case I div 180 of
        0: Ok := ProjectPoint(Window.Az1 + RectAzDiff(Window.Az1, Window.Az2) * (I mod 180) / 180.0, Window.Alt1, X, Y);
        1: Ok := ProjectPoint(Window.Az2, Window.Alt1 + (Window.Alt2 - Window.Alt1) * (I mod 180) / 180.0, X, Y);
        2: Ok := ProjectPoint(Window.Az2 - RectAzDiff(Window.Az1, Window.Az2) * (I mod 180) / 180.0, Window.Alt2, X, Y);
      else Ok := ProjectPoint(Window.Az1, Window.Alt2 - (Window.Alt2 - Window.Alt1) * (I mod 180) / 180.0, X, Y);
      end;
      if Ok then
      begin
        if ((I mod 180) = 0) or not Prev then C.MoveTo(Round(X), Round(Y))
        else C.LineTo(Round(X), Round(Y));
      end;
      Prev := Ok;
    end;
  end
  else
  begin
    C.Ellipse(Round(CX - R), Round(CY - R), Round(CX + R), Round(CY + R));
    if IsLimitedView(View) then
    begin
      Prev := False;
      for I := 0 to 719 do
      begin
        Ok := ProjectPoint(I * 0.5, 0.0, X, Y);
        if Ok then
        begin
          if Prev then C.LineTo(Round(X), Round(Y)) else C.MoveTo(Round(X), Round(Y));
        end;
        Prev := Ok;
      end;
    end;
  end;

  // cardinal labels, nudged just outside the horizon point
  C.Font.Color := clYellow;
  for I := 0 to 3 do
  begin
    if not ProjectPoint(I * 90.0, 0.0, X, Y) then Continue;
    DX := X - CX; DY := Y - CY;
    Len := Sqrt(DX * DX + DY * DY);
    if Len > 0.001 then begin DX := DX / Len; DY := DY / Len; end;
    C.TextOut(Round(X + DX * 12) - 4, Round(Y + DY * 12) - 8, CardLabels[I]);
  end;

  // zenith marker: only when it falls inside the current view
  C.Font.Color := clWhite;
  if ProjectPoint(0.0, 90.0, X, Y) then
    C.TextOut(Round(X) - 3, Round(Y) - 8, '+');

  // sun (if up) - uses CurrentJD set by UpdateSky (fixed or live)
  JD := CurrentJD;
  SunRAandDec(JD, SunRAH, SunDecD);
  JDToCalendar(JD, Y2, Mo2, D2, H2, Mi2, S2);
  HorizontalCoords(Y2, Mo2, D2, H2, Mi2, S2,
    ObsLat, ObsLon, SunRAH, SunDecD, SunAz, SunAlt);
  if (SunAlt > 0) and ProjectPoint(SunAz, SunAlt, X, Y) then
  begin
    C.Font.Color := clYellow;
    C.Font.Style := [fsBold];
    C.TextOut(Round(X) - 4, Round(Y) - 8, '@');
    C.Font.Style := [];
  end;

  // reset visibility flags from previous frame
  for I := 0 to High(Sky) do
  begin
    Sky[I].Visible := False;
    Sky[I].ScreenX := -1;
    Sky[I].ScreenY := -1;
  end;

  // objects: brightest above horizon last (drawn on top)
  for I := 0 to High(Sky) do
  begin
    P := Sky[I];
    if P.Alt <= 0 then Continue;    // below horizon: not drawn
    if P.Mag > MagLimit then Continue;
    // rect window mode filters by the az/alt box instead of the circular field
    if RectOn and not InRectWindow(Window, P.Az, P.Alt) then Continue;
    if not ProjectPoint(P.Az, P.Alt, X, Y) then Continue; // outside the fov field
    Sky[I].Visible := True;
    Sky[I].ScreenX := Round(X);
    Sky[I].ScreenY := Round(Y);
    if P.Sym = '.' then
    begin
      C.Pen.Color := clSilver;
      C.Brush.Color := clBlack;
      Size := 1;
      C.Rectangle(Round(X) - Size, Round(Y) - Size, Round(X) + Size, Round(Y) + Size);
    end
    else
    begin
      // highlight the selected / hovered / keyboard-cursor object with a ring
      if (I = Selected) or (I = HoverIdx) or (I = KeyCursor) then
      begin
        C.Brush.Style := bsClear;
        if I = Selected then
          C.Pen.Color := clRed
        else if I = KeyCursor then
          C.Pen.Color := clYellow
        else
          C.Pen.Color := clAqua;
        C.Ellipse(Round(X) - 8, Round(Y) - 8, Round(X) + 8, Round(Y) + 8);
      end;
      if P.IsPlanet then
        C.Font.Color := clLime
      else if P.Sym = 'O' then
        C.Font.Color := clYellow
      else
        C.Font.Color := clWhite;
      C.Font.Size := 10;
      C.TextOut(Round(X) - 4, Round(Y) - 8, P.Sym);
    end;
  end;

  // header band (top-left of the reserved strip): date/time, moon phase
  // (CurrentJD = fixed or live frame time)
  C.Font.Color := clWhite;
  C.Font.Size := 9;
  JD := CurrentJD;
  Planets := LoadPlanets('planetary_objects.csv', JD);
  MoonRAH := 0; MoonDecD := 0;
  for I := 0 to High(Planets) do
    if Planets[I].Name = 'Moon' then
    begin
      MoonRAH := Planets[I].RAHours;
      MoonDecD := Planets[I].DecDeg;
    end;
  // calendar fields of the displayed frame (for header + sun events)
  JDToCalendar(JD, Y2, Mo2, D2, H2, Mi2, S2);
  Ilum := MoonIllumination(MoonRAH, MoonDecD, JD);
  C.TextOut(8, HeaderTop + 0, Format('%04.4d-%02.2d-%02.2d %02.2d:%02.2d:%02.2d UTC | moon %.0f%% %s',
    [Trunc(Y2), Trunc(Mo2), Trunc(D2), Trunc(H2), Trunc(Mi2), Trunc(S2),
     Ilum * 100, MoonPhaseName(MoonRAH, MoonDecD, JD, Ilum)]));

  // sun altitude / twilight state + sunrise/sunset + civil twilight window
  SunRAandDec(JD, SunRA2, SunDec2);
  HorizontalCoords(Y2, Mo2, D2, H2, Mi2, S2,
    ObsLat, ObsLon, SunRA2, SunDec2, SunAz2, SunAlt2);
  C.TextOut(8, HeaderTop + 12, Format('sun alt %.1f  |  %s', [SunAlt2, TwilightState(SunAlt2)]));
  FindSunEvents(Y2, Mo2, D2, ObsLat, ObsLon, -0.833, Rise, SetT);
  FindSunEvents(Y2, Mo2, D2, ObsLat, ObsLon, -6.0, CRise, CSet);
  Line3 := 'sun ';
  if Rise >= 0 then Line3 += Format('rise %.2d:%.2d', [Trunc(Rise), Round(Frac(Rise) * 60)]) + ' UTC';
  if SetT >= 0 then
  begin
    if Rise >= 0 then Line3 += '  ';
    Line3 += Format('set %.2d:%.2d', [Trunc(SetT), Round(Frac(SetT) * 60)]) + ' UTC';
  end;
  if (Rise < 0) and (SetT < 0) then
    if SunAlt2 > 0 then Line3 += 'up all day' else Line3 += 'down all day';
  if (CRise >= 0) or (CSet >= 0) then
  begin
    Line3 += '  |  civil ';
    if CRise >= 0 then Line3 += Format('%.2d:%.2d', [Trunc(CRise), Round(Frac(CRise) * 60)]);
    if CSet >= 0 then
    begin
      if CRise >= 0 then Line3 += '-';
      Line3 += Format('%.2d:%.2d', [Trunc(CSet), Round(Frac(CSet) * 60)]) + ' UTC';
    end;
  end;
  C.TextOut(8, HeaderTop + 24, Line3);

  // limited-view line: where the field points, how wide it is, whether follow
  // mode is steering it, and the keyboard shortcuts (+/- zoom, f follow,
  // w/a/s/d pan, 0 full sky, x rect window).
  if RectOn then
    Line3 := Format('  (window az %.0f-%.0f, alt %.0f-%.0f)', [Window.Az1, Window.Az2, Window.Alt1, Window.Alt2])
  else if IsLimitedView(View) then Line3 := '  (limited)' else Line3 := '  (full sky)';
  if Follow then
  begin
    if (Selected >= 0) and (Selected <= High(Sky)) then
      FollowTxt := '  follow: on (' + Sky[Selected].Name + ')'
    else
      FollowTxt := '  follow: on (no target locked)';
  end
  else
    FollowTxt := '  follow: off';
  C.TextOut(8, HeaderTop + 52, Format('view: center az %.1f alt %.1f  fov %.1f deg%s%s   [f] follow  [x] window  [+/-] zoom  [w/a/s/d] pan  [0] full sky',
    [View.CenterAz, View.CenterAlt, View.Fov, Line3, FollowTxt]));

  // live tracking panel: selected object vs the mount's last sent position
  if (Selected >= 0) and ReadMountStatus(MAz, MAlt, MAge) then
  begin
    DAz := MAz - Sky[Selected].Az;
    while DAz > 180.0 do DAz := DAz - 360.0;
    while DAz < -180.0 do DAz := DAz + 360.0;
    DAlt := MAlt - Sky[Selected].Alt;
    Sep := ArcCos(EnsureRange(
      Sin(DegToRad(MAlt)) * Sin(DegToRad(Sky[Selected].Alt)) +
      Cos(DegToRad(MAlt)) * Cos(DegToRad(Sky[Selected].Alt)) * Cos(DegToRad(DAz)), -1.0, 1.0)) * 180.0 / Pi;
    C.Font.Color := IfThen(Sep < 0.5, clLime, clAqua);
    C.TextOut(8, HeaderTop + 40, Format('mount: AZ %.2f ALT %.2f   dAZ %+.2f  dALT %+.2f   sep %.2f deg%s   (%ds ago)',
      [MAz, MAlt, DAz, DAlt, Sep, FlagStr(Sep < 0.5, '  ON TARGET'), Round(MAge)]));
    C.Font.Color := clWhite;
  end
  else if Selected >= 0 then
  begin
    C.Font.Color := clGray;
    C.TextOut(8, HeaderTop + 40, 'mount: no position recorded - run ./autoobservatory ... --from-planetarium -a --serial DEV');
    C.Font.Color := clWhite;
  end;

  // clickable legend: its own column on the right, grouped into Moon/planets
  // and stars (each group brightest first), with small grey headers. Click a
  // line to select that object on the map (red ring). Hidden entirely when the
  // legend column is collapsed (L key / View menu).  C.Font.Size := 7;
  if not LegendVisible then Exit;
  Y := HeaderTop + 2;
  for Group := 0 to 2 do
  begin
    // group header (dim), skipped when the group has no visible members
    // (same filters as the entry loop: named, above horizon, within mag limit)
    Count := 0;
    for I := 0 to High(Sky) do
      if InLegendGroup(Sky[I], Group) and (Sky[I].Sym <> '.')
         and Sky[I].Visible then
        Inc(Count);
    if Count = 0 then Continue;
    C.Font.Color := clGray;
    case Group of
      0: C.TextOut(DomeW - 20, Round(Y), 'SUN & MOON');
      1: C.TextOut(DomeW - 20, Round(Y), 'PLANETS & DWARFS');
      2: C.TextOut(DomeW - 20, Round(Y), 'STARS & CLUSTERS');
    end;
    Y += 14;
    for I := 0 to High(Sky) do
    begin
      if not InLegendGroup(Sky[I], Group) then Continue;
      if Sky[I].Sym = '.' then Continue;
      if not Sky[I].Visible then Continue; // below horizon / past the mag limit / outside fov
      if I = Selected then
        C.Font.Color := clRed
      else if I = KeyCursor then
        C.Font.Color := clYellow
      else if I = HoverIdx then
        C.Font.Color := clAqua
      else
        C.Font.Color := clWhite;
      if I = Selected then Flag := '*'
      else if I = KeyCursor then Flag := '>'
      else Flag := ' ';
      // Stars show their Bayer designation (e.g. Alpha_Boo Vega) for a
      // professional map; clusters keep the digit shorthand (1 = C Hyades).
      EntryName := Sky[I].Name;
      if (Sky[I].Sym >= '0') and (Sky[I].Sym <= '9') then
        EntryName := 'C ' + EntryName
      else if Sky[I].Bayer <> '' then
        EntryName := Sky[I].Bayer + ' ' + EntryName;
      C.TextOut(DomeW - 20, Round(Y), Format('%s %s  alt %5.1f  mag %4.1f', [Flag, EntryName, Sky[I].Alt, Sky[I].Mag]));
      // store legend line hit box on the object record
      Sky[I].ScreenY := -1000 - Round(Y); // negative marker: legend line at |y|-1000
      Sky[I].ScreenX := DomeW - 20;        // legend hit boxes live in the right column
      Y += 14;
      if Y > H - 8 then Break;
    end;
    Y += 4; // small gap between groups
    if Y > H - 8 then Break;
  end;
  C.Font.Color := clWhite;
end;

procedure TSkyForm.DoPaint(Sender: TObject);
begin
  DrawDome(PaintBox.Canvas, PaintBox.Width, PaintBox.Height);
  ShowLegend;
end;

// Hit test: within 8 px of a drawn object marker, or on a legend line
function TSkyForm.HitTest(X, Y: Integer): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to High(Sky) do
  begin
    if not Sky[I].Visible then Continue;
    if (Abs(Sky[I].ScreenX - X) <= 8) and (Abs(Sky[I].ScreenY - Y) <= 8) then
      Exit(I);
  end;
  // legend lines: live in the right column (x >= W - LEGEND_W); ScreenY stores
  // -(1000 + lineY), ScreenX the column's left edge. Skipped when collapsed
  // (stale boxes from the last drawn frame would otherwise still be clickable).
  if not LegendVisible then Exit;
  for I := 0 to High(Sky) do
  begin
    if Sky[I].ScreenY <= -1000 then
      if (X >= Sky[I].ScreenX) and (X <= Sky[I].ScreenX + LEGEND_W - 12) and
         (Y >= -Sky[I].ScreenY - 1000) and
         (Y <= -Sky[I].ScreenY - 1000 + 13) then
        Exit(I);
  end;
end;

procedure TSkyForm.DoMouseMove(Sender: TObject; Shift: TShiftState; X, Y: Integer);
var
  Hit: Integer;
begin
  Hit := HitTest(X, Y);
  if Hit <> HoverIdx then
  begin
    HoverIdx := Hit;
    if Hit >= 0 then
      PaintBox.Hint := Format('%s  az %.1f°  alt %.1f°  mag %.1f',
        [Sky[Hit].Name, Sky[Hit].Az, Sky[Hit].Alt, Sky[Hit].Mag])
    else
      PaintBox.Hint := '';
    Application.ActivateHint(Mouse.CursorPos);
    PaintBox.Invalidate;
  end;
end;

// Hand the selection to the autoobservatory: it reads the same
// selected_target.txt as the terminal planetarium (--from-planetarium).
procedure TSkyForm.SaveTarget(const AName: string);
var
  Sl: TStringList;
begin
  Sl := TStringList.Create;
  try
    if AName <> '' then Sl.Add(AName);
    try
      Sl.SaveToFile('selected_target.txt');
    except
      // read-only working dir: the GUI still works, just no hand-off
    end;
  finally
    Sl.Free;
  end;
  if AName = '' then
    TargetLabel.Caption := 'target: none'
  else
    TargetLabel.Caption := Format('target: %s  ->  ./autoobservatory %.4f %.4f --from-planetarium -a',
      [AName, ObsLat, ObsLon]);
end;


// Case-insensitive catalog lookup; -1 when the name is unknown to the catalogs.
// The matching itself lives in astrop.pas (ResolveName), the same logic as the
// C++ object_name.h, so planetarium --select, autoobservatory --object and this
// GUI all resolve fuzzy names identically. A fuzzy query matching several
// objects returns -1 and fills AmbiguousList (newline-separated) so the caller
// can list them rather than silently aim the mount at the wrong object.
function TSkyForm.FindObject(const AName: string; out ACandidates: specialize TArray<string>): Integer;
var
  Names: array of string;
  I: Integer;
  Canonical: string;
begin
  SetLength(Names, Length(Sky));
  for I := 0 to High(Sky) do
    Names[I] := Sky[I].Name;
  Result := ResolveName(Names, AName, Canonical, ACandidates);
end;

// Mirror of the terminal planetarium's keys: n/p (or Tab) walk the object
// cursor, t (or Enter) locks it as the mount target, c clears it.
procedure TSkyForm.MoveCursor(Step: Integer);
var
  I, N: Integer;
begin
  N := Length(Sky);
  if N = 0 then Exit;
  if (KeyCursor < 0) or (KeyCursor >= N) then
    KeyCursor := 0
  else
    KeyCursor := (KeyCursor + Step + N) mod N;
  for I := 1 to N do // skip objects that are not drawn (below horizon)
    if Sky[KeyCursor].Visible then Break
    else KeyCursor := (KeyCursor + Step + N) mod N;
  TargetLabel.Caption := Format('cursor: %s  (press t to make it the mount target)', [Sky[KeyCursor].Name]);
  PaintBox.Invalidate;
end;

// View menu -> Legend column: same toggle as pressing L.
procedure TSkyForm.DoToggleLegend(Sender: TObject);
begin
  LegendVisible := not LegendVisible;
  if ViewMenuItem <> nil then
    ViewMenuItem.Checked := LegendVisible;
  PaintBox.Invalidate;
end;

procedure TSkyForm.DoKeyDown(Sender: TObject; var Key: Word; Shift: TShiftState);
begin
  case Key of
    9: MoveCursor(1);           // Tab
    Ord('n'), Ord('N'): MoveCursor(1);
    Ord('p'), Ord('P'): MoveCursor(-1);
    Ord('t'), Ord('T'), 13:     // Enter
      begin
        if (KeyCursor >= 0) and (KeyCursor < Length(Sky)) then
        begin
          Selected := KeyCursor;
          SaveTarget(Sky[Selected].Name);
          PaintBox.Invalidate;
        end;
      end;
    Ord('c'), Ord('C'), 27:     // Esc
      begin
        Selected := -1;
        SaveTarget('');
        PaintBox.Invalidate;
      end;
    Ord('l'), Ord('L'):         // toggle the legend column; dome re-centers
      begin
        LegendVisible := not LegendVisible;
        if ViewMenu <> nil then
          ViewMenuItem.Checked := LegendVisible;
        PaintBox.Invalidate;
      end;
    43, 61, 107:                // +, =, numpad + : zoom in on one part of the sky
      begin
        View.Fov := Max(1.0, View.Fov / 1.5);
        PaintBox.Invalidate;
      end;
    45, 95, 109:                // -, _, numpad - : zoom back out (clamped to full sky)
      begin
        View.Fov := Min(180.0, View.Fov * 1.5);
        PaintBox.Invalidate;
      end;
    Ord('f'), Ord('F'):         // follow the locked target as the sky turns
      begin
        Follow := not Follow;
        PaintBox.Invalidate;
      end;
    Ord('x'), Ord('X'):         // toggle the rectangular az/alt window mode
      begin
        RectOn := not RectOn;
        PaintBox.Invalidate;
      end;
    Ord('w'), Ord('W'):         // pan the centre toward the zenith (stops follow)
      begin
        if RectOn then
        begin
          Window.Alt1 := Min(90.0, Window.Alt1 + 5.0);
          Window.Alt2 := Min(90.0, Window.Alt2 + 5.0);
        end
        else
          View.CenterAlt := Min(90.0, View.CenterAlt + View.Fov / 4.0);
        Follow := False;
        PaintBox.Invalidate;
      end;
    Ord('s'), Ord('S'):         // pan the centre toward the horizon (stops follow)
      begin
        if RectOn then
        begin
          Window.Alt1 := Max(-90.0, Window.Alt1 - 5.0);
          Window.Alt2 := Max(-90.0, Window.Alt2 - 5.0);
        end
        else
          View.CenterAlt := Max(-90.0, View.CenterAlt - View.Fov / 4.0);
        Follow := False;
        PaintBox.Invalidate;
      end;
    Ord('a'), Ord('A'):         // pan the centre west (stops follow)
      begin
        if RectOn then
        begin
          Window.Az1 := NormalizeDeg(Window.Az1 - 5.0);
          Window.Az2 := NormalizeDeg(Window.Az2 - 5.0);
        end
        else
          View.CenterAz := NormalizeDeg(View.CenterAz - View.Fov / 4.0);
        Follow := False;
        PaintBox.Invalidate;
      end;
    Ord('d'), Ord('D'):         // pan the centre east (stops follow)
      begin
        if RectOn then
        begin
          Window.Az1 := NormalizeDeg(Window.Az1 + 5.0);
          Window.Az2 := NormalizeDeg(Window.Az2 + 5.0);
        end
        else
          View.CenterAz := NormalizeDeg(View.CenterAz + View.Fov / 4.0);
        Follow := False;
        PaintBox.Invalidate;
      end;
    Ord('0'):                   // reset to the full sky (stops follow, drops window)
      begin
        View := FullSkyView;
        Follow := False;
        RectOn := False;
        PaintBox.Invalidate;
      end;
  else
    Exit; // leave other keys (trackbar navigation etc.) alone
  end;
  Key := 0; // consumed
end;

// Pull one number out of "AZ:12.34 ALT:56.78 TARGET:X T:1700000000" style text
function ExtractNum(const S, Key: string; out Val: Double): Boolean;
var
  P, Q: Integer;
begin
  Result := False;
  P := Pos(Key, S);
  if P = 0 then Exit;
  Inc(P, Length(Key));
  Q := P;
  while (Q <= Length(S)) and (S[Q] <> ' ') do Inc(Q);
  Result := TryStrToFloat(Copy(S, P, Q - P), Val);
end;

// mount_status.txt is written by ./autoobservatory when it sends a position to
// the mount; reading it lets the panel show the real pointing error.
function TSkyForm.ReadMountStatus(out MAz, MAlt, MAge: Double): Boolean;
var
  Sl: TStringList;
  Line: string;
  T: Double;
begin
  Result := False;
  MAz := 0; MAlt := 0; MAge := 0; T := 0;
  if not FileExists('mount_status.txt') then Exit;
  Sl := TStringList.Create;
  try
    Sl.LoadFromFile('mount_status.txt');
    if Sl.Count = 0 then Exit;
    Line := Sl[0];
  finally
    Sl.Free;
  end;
  if not ExtractNum(Line, 'AZ:', MAz) then Exit;
  if not ExtractNum(Line, 'ALT:', MAlt) then Exit;
  ExtractNum(Line, 'T:', T);
  if T > 0 then MAge := Now * 86400.0 - 25569.0 * 86400.0 - T;  // Unix epoch -> age
  Result := True;
end;

procedure TSkyForm.DoClick(Sender: TObject);
var
  Pt: TPoint;
  Hit: Integer;
begin
  Pt := PaintBox.ScreenToClient(Mouse.CursorPos);
  Hit := HitTest(Pt.X, Pt.Y);
  if Hit >= 0 then
    Selected := Hit
  else
    Selected := -1;
  if Selected >= 0 then
    SaveTarget(Sky[Selected].Name)
  else
    SaveTarget('');
  PaintBox.Invalidate;
end;

procedure TSkyForm.ShowLegend;
var
  I, LineY: Integer;
begin
  // legend is drawn inside DrawDome; this placeholder kept for structure
  if Selected < 0 then Exit;
  LineY := 0; // no-op
  I := LineY;
end;

// UTC now: Unix epoch -> TDateTime (Delphi epoch 1899-12-30; 25569 days =
// 1970-01-01). No local-timezone/DST involvement at all.
procedure TSkyForm.GetUTC(out ST: TSystemTime);
var
  TV: TTimeVal;
begin
  fpgettimeofday(@TV, nil);
  DateTimeToSystemTime(25569.0 + TV.tv_sec / 86400.0, ST);
end;

var
  I, SelIdx: Integer;
  Lat, Lon: Double;
  DateStr, TimeStr, SelectName, Msg: string;
  Candidates: specialize TArray<string>;
  FY, FMo, FD, FH, FMi: Double;
  FS: Double;
  ViewFov, ViewAz, ViewAlt: Double;
  ViewHasCenter, FollowFlag: Boolean;
  WindowSpec, Tok: string;
  WindowVals: array[1..4] of Double;
  Wi, GotVals: Integer;
  RectFlag: Boolean;
  RectErr: string;
  Window: TRectWindow;      // --window AZ1,AZ2,ALT1,ALT2 (rect az/alt view)
  ViewStep: Integer;
  Spec, AzStr, AltStr: string;
begin
  Lat := 45.0;
  Lon := 10.0;
  SelectName := '';
  ViewFov := 180.0;
  ViewHasCenter := False;
  FollowFlag := False;
  RectFlag := False;
  // argv: [lat lon] like the C++ planetarium (south/west negative)
  I := 1;
  while I <= ParamCount do
  begin
    if ParamStr(I) = '--date' then
    begin
      if I < ParamCount then
      begin
        DateStr := ParamStr(I + 1);
        Inc(I);
      end;
    end
    else if ParamStr(I) = '--time' then
    begin
      if I < ParamCount then
      begin
        TimeStr := ParamStr(I + 1);
        Inc(I);
      end;
    end
    else if ParamStr(I) = '--select' then
    begin
      if I < ParamCount then
      begin
        SelectName := ParamStr(I + 1);
        Inc(I);
      end;
    end
    else if ParamStr(I) = '--fov' then
    begin
      // Limited view: 0 < fov < 180 degrees (a field smaller than a full sky).
      if I >= ParamCount then
      begin WriteLn('Error: --fov needs a value'); Halt(2); end;
      if not TryStrToFloat(ParamStr(I + 1), ViewFov, FormatSettings) then
      begin WriteLn('Error: invalid field of view "', ParamStr(I + 1), '"'); Halt(2); end;
      if not ValidFov(ViewFov) then
      begin
        WriteLn('Error: --fov must be smaller than a full sky (0 < fov < 180 degrees, got ',
          FormatFloat('0.###', ViewFov), ')');
        Halt(2);
      end;
      Inc(I);
    end
    else if ParamStr(I) = '--center' then
    begin
      // Centre of the limited view: AZ,ALT (or two tokens), az [0,360), alt [-90,90].
      if I >= ParamCount then
      begin WriteLn('Error: --center needs AZ,ALT (or AZ ALT), e.g. --center 210,45'); Halt(2); end;
      Spec := ParamStr(I + 1);
      ViewStep := 1;
      if Pos(',', Spec) > 0 then
      begin
        AzStr := Copy(Spec, 1, Pos(',', Spec) - 1);
        AltStr := Copy(Spec, Pos(',', Spec) + 1, Length(Spec));
        if Pos(',', AltStr) > 0 then
        begin WriteLn('Error: --center takes AZ,ALT (one comma), got "', Spec, '"'); Halt(2); end;
      end
      else
      begin
        if I + 1 > ParamCount then
        begin WriteLn('Error: --center needs AZ,ALT (or AZ ALT), e.g. --center 210,45'); Halt(2); end;
        AzStr := Spec;
        AltStr := ParamStr(I + 2);
        ViewStep := 2;
      end;
      if (not TryStrToFloat(AzStr, ViewAz, FormatSettings)) or
         (not TryStrToFloat(AltStr, ViewAlt, FormatSettings)) then
      begin WriteLn('Error: invalid --center "', Spec, '"'); Halt(2); end;
      if not ValidCenter(ViewAz, ViewAlt) then
      begin
        WriteLn('Error: --center needs azimuth in [0, 360) and altitude in [-90, 90] (got ',
          FormatFloat('0.###', ViewAz), ', ', FormatFloat('0.###', ViewAlt), ')');
        Halt(2);
      end;
      ViewHasCenter := True;
      Inc(I, ViewStep);
    end
    else if ParamStr(I) = '--follow' then
      FollowFlag := True
    else if ParamStr(I) = '--window' then
    begin
      // --window AZ1,AZ2,ALT1,ALT2: show only the objects between these
      // azimuths (shortest arc, may straddle north) and altitudes.
      if I >= ParamCount then
      begin WriteLn('Error: --window needs AZ1,AZ2,ALT1,ALT2, e.g. --window 90,180,10,60'); Halt(2); end;
      WindowSpec := ParamStr(I + 1);
      Inc(I);
      GotVals := 0;
      Tok := '';
      WindowSpec := WindowSpec + ','; // sentinel so the last token parses
      for Wi := 1 to Length(WindowSpec) do
      begin
        if WindowSpec[Wi] = ',' then
        begin
          Inc(GotVals);
          if GotVals > 4 then Break;
          if not TryStrToFloat(Tok, WindowVals[GotVals], FormatSettings) then
          begin
            WriteLn('Error: --window takes AZ1,AZ2,ALT1,ALT2, got "',
              Copy(WindowSpec, 1, Length(WindowSpec) - 1), '"');
            Halt(2);
          end;
          Tok := '';
        end
        else
          Tok := Tok + WindowSpec[Wi];
      end;
      if GotVals <> 4 then
      begin
        WriteLn('Error: --window takes AZ1,AZ2,ALT1,ALT2 (three commas), got "',
          Copy(WindowSpec, 1, Length(WindowSpec) - 1), '"');
        Halt(2);
      end;
      Window.Az1 := WindowVals[1]; Window.Az2 := WindowVals[2];
      Window.Alt1 := WindowVals[3]; Window.Alt2 := WindowVals[4];
      if not ValidRectWindow(Window) then
      begin
        Str(Window.Alt1:0:3, RectErr);
        WriteLn('Error: --window needs azimuths in [0, 360) and ALT1 < ALT2 in [-90, 90] (got ',
          FormatFloat('0.###', Window.Az1), ',', FormatFloat('0.###', Window.Az2), ',',
          FormatFloat('0.###', Window.Alt1), ',', FormatFloat('0.###', Window.Alt2), ')');
        Halt(2);
      end;
      RectFlag := True;
    end
    else if (I < ParamCount) and (ParamStr(I) <> '') then
    try
      Lat := StrToFloat(ParamStr(I), FormatSettings);
      Lon := StrToFloat(ParamStr(I + 1), FormatSettings);
      Inc(I);
      Continue;
    except
      // not a coordinate pair: ignore
    end;
    Inc(I);
  end;

  // --date YYYY-MM-DD / --time HH:MM[:SS] replay a fixed instant (UTC).
  // Strict parsing shared with the C++ tools via astrop.pas: Feb 30, hour 25,
  // wrong separators etc. are usage errors (exit 2), matching the convention.
  if (DateStr <> '') or (TimeStr <> '') then
  begin
    FY := 0; FMo := 1; FD := 1; FH := 0; FMi := 0; FS := 0;
    if (DateStr <> '') and not ParseDateArg(DateStr, FY, FMo, FD, Msg) then
    begin
      WriteLn(Msg);
      Halt(2);
    end;
    if (TimeStr <> '') and not ParseTimeArg(TimeStr, FH, FMi, FS, Msg) then
    begin
      WriteLn(Msg);
      Halt(2);
    end;
  end;

  Application.Initialize;
  Application.CreateForm(TSkyForm, SkyForm);
  SkyForm.ObsLat := Lat;
  SkyForm.ObsLon := Lon;
  // limited view from --fov/--center (omitted = the full sky)
  SkyForm.View := FullSkyView;
  SkyForm.View.Fov := ViewFov;
  if ViewHasCenter then
  begin
    SkyForm.View.CenterAz := ViewAz;
    SkyForm.View.CenterAlt := ViewAlt;
  end;
  SkyForm.Follow := FollowFlag;
  if RectFlag then
  begin
    SkyForm.RectOn := True;
    SkyForm.Window := Window;
  end;
  if (DateStr <> '') or (TimeStr <> '') then
  begin
    SkyForm.FixedJD := JulianDate(FY, FMo, FD, FH, FMi, FS);
    SkyForm.UseFixed := True;
    SkyForm.Caption := SkyForm.Caption + ' - FIXED TIME';
  end;
  SkyForm.UpdateSky;

  // --select NAME: set the target without any interaction (same semantics as
  // the terminal planetarium: case-insensitive, canonical name written out)
  if SelectName <> '' then
  begin
    SelIdx := SkyForm.FindObject(SelectName, Candidates);
    if SelIdx < 0 then
    begin
      if Length(Candidates) > 0 then
      begin
        WriteLn('Ambiguous object: "', SelectName, '" matches ', Length(Candidates), ':');
        for I := 0 to High(Candidates) do
          WriteLn('  ', Candidates[I]);
        WriteLn('Use the full name.');
      end
      else
      begin
        WriteLn('Unknown object: "', SelectName, '"');
        WriteLn('Names come from celestial_objects.csv and planetary_objects.csv');
      end;
      Halt(1);
    end;
    SkyForm.KeyCursor := SelIdx;
    SkyForm.Selected := SelIdx;
    SkyForm.SaveTarget(SkyForm.Sky[SelIdx].Name);
    Write('Target set: ', SkyForm.Sky[SelIdx].Name);
    if not SameText(SkyForm.Sky[SelIdx].Name, SelectName) then
      Write('  (fuzzy match for "', SelectName, '")');
    WriteLn('  (written to selected_target.txt)');
    Flush(Output); // scripts may read this before the window comes up
  end;
  SkyForm.Invalidate;
  Application.Run;
end.
