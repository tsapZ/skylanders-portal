// weak_gpu.iss: [Code] for the six games' setups (#include it inside [Code]). Whether this PC's graphics
// card runs the games slowly at 2x, so the setup's "Internal resolution" field starts at 1 instead of 2
// (the owner's wish, 2026-11-05: weak cards get 1x). The same list as Core of Light's src/gpu_tier.cpp,
// which passes /SCALE=1 for them: at 2x the games are limited by the graphics card even on fast ones
// (Swap Force, Trap Team and Imaginators about 66-70 FPS in their levels on an RX 6800 XT), so a card
// a third as fast runs them at about 30 (a player's GTX 1070, 2026-11-05).
//
// By name, through WMI's Win32_VideoController: its AdapterRAM stops at 4 GB, so the memory can't
// decide. Only real cards count (NVIDIA, AMD / Radeon, Intel); virtual displays (Parsec, remote
// desktop, Microsoft Basic Display) are skipped. One strong card makes the PC strong: a laptop's
// second, integrated chip doesn't matter. No WMI or no card found: not weak (2x as before).

// A model number not followed by another digit: "RX 570" in "RX 570" and "RX 570 4GB", not in "RX 5700".
function WeakGpuHasModel(Upper, Model: String): Boolean;
var
  P, E: Integer;
begin
  Result := False;
  P := Pos(Model, Upper);
  while P > 0 do begin
    E := P + Length(Model);
    if (E > Length(Upper)) or (Upper[E] < '0') or (Upper[E] > '9') then begin
      Result := True;
      Exit;
    end;
    Upper := Copy(Upper, P + 1, Length(Upper));
    P := Pos(Model, Upper);
  end;
end;

// 0: not a real graphics card (a virtual display), 1: weak at 2x, 2: strong enough.
function WeakGpuClass(Name: String): Integer;
var
  U: String;
begin
  U := Uppercase(Name);
  Result := 0;
  if Pos('INTEL', U) > 0 then begin
    // Everything but Arc is integrated; the small Arc A310 / A380 are weak too.
    if (Pos('ARC', U) = 0) or (Pos('A310', U) > 0) or (Pos('A380', U) > 0) then Result := 1 else Result := 2;
  end else if Pos('NVIDIA', U) > 0 then begin
    // The GTX cards (up to the 16 series), GT and MX, the old Quadros and the smallest RTX.
    if (Pos('GTX', U) > 0) or (Pos(' GT ', U) > 0) or (Pos(' MX', U) > 0) or
       ((Pos('QUADRO', U) > 0) and (Pos('RTX', U) = 0)) or (Pos('RTX 3050', U) > 0) or
       (Pos('RTX 2050', U) > 0) then Result := 1 else Result := 2;
  end else if (Pos('AMD', U) > 0) or (Pos('RADEON', U) > 0) then begin
    // The integrated ones have no RX in their name ("AMD Radeon(TM) Graphics", "Radeon 780M
    // Graphics", "Radeon Vega 8 Graphics"); then the cards before the RX 5700 and the small RX 6000s.
    if (Pos('RX ', U) = 0) and (Pos('PRO W', U) = 0) then Result := 1
    else if (Pos('RX 4', U) > 0) or (Pos('RX 5300', U) > 0) or (Pos('RX 5500', U) > 0) or
            (Pos('RX 5600', U) > 0) or (Pos('RX 6300', U) > 0) or (Pos('RX 6400', U) > 0) or
            (Pos('RX 6500', U) > 0) or (Pos('VEGA', U) > 0) or
            WeakGpuHasModel(U, 'RX 550') or WeakGpuHasModel(U, 'RX 560') or WeakGpuHasModel(U, 'RX 570') or
            WeakGpuHasModel(U, 'RX 580') or WeakGpuHasModel(U, 'RX 590') then Result := 1
    else Result := 2;
  end;
end;

function WeakGraphicsCard: Boolean;
var
  Locator, Service, Items: Variant;
  I, Kind: Integer;
  AnyWeak, AnyStrong: Boolean;
begin
  Result := False;
  AnyWeak := False;
  AnyStrong := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Items := Service.ExecQuery('SELECT Name FROM Win32_VideoController');
    for I := 0 to Items.Count - 1 do begin
      Kind := WeakGpuClass(Items.ItemIndex(I).Name);
      if Kind = 1 then AnyWeak := True;
      if Kind = 2 then AnyStrong := True;
    end;
  except
    Exit;
  end;
  Result := AnyWeak and not AnyStrong;
end;
