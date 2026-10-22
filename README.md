# skylanders-portal

Skylanders Portal of Power emulation for [ReXGlue](https://github.com/rexglue/rexglue-sdk)
static recompilations of the Xbox 360 Skylanders games. It started as the portal code of
[Giants Recompiled](https://github.com/TheBiemGamer/GiantsRecomp) and is used as a git submodule by
[Trap Team Recompiled](https://github.com/TheBiemGamer/TrapTeamRecomp).

## Targets

- **`skylanders_portal_core`** has no ReXGlue dependency and is unit-tested. It holds the portal
  protocol (a software portal with up to 16 figures, including Trap Team traps), the Xbox 360
  frame format, figure files, the figure catalog, and figure save-data decoding.
- **`skylanders_portal_rex`** is the ReXGlue glue: settings (`portal_mode`, ...), the handler for
  the SDK's `XamInputNonControllerGetRaw/SetRaw` hook, a real portal over USB (hidapi), the F6
  figure overlay, and portal speaker audio. It is built only when the including project provides
  the `rex::runtime` target.

- **`skylanders_portal_figures`** is only the figure files (catalog, names, elements, stats) for
  programs without the SDK, such as the Core of Light launcher; on Windows it takes AES and MD5
  from Windows (CNG). Built whenever `REXSDK_DIR` isn't set.

`figure_stats` uses the AES and MD5 implementations the ReXGlue SDK vendors, so set `REXSDK_DIR` to
an SDK source tree when configuring. Standalone test build:

```
cmake -S . -B out -G Ninja -DREXSDK_DIR=<path to rexglue-sdk>
cmake --build out && ctest --test-dir out
```

## The shared picture folder

Each game that calls `StartFigurePictureCache(SharedPicturesDir(user_data_root, "<game>"))` leaves
what it reads from its disc in `Documents\Skylanders\Pictures\<game>` for the launcher, which reads
no disc itself: every figure picture (`<id>-<variant>.png`), the hats (`hats.txt`, `hat-<n>.png`),
the upgrade trees of the figures in the figure folder (`upgrades-<id>-<variant>.txt` and their
pictures), and the achievements with the title's own pictures (`achievements.txt`, `ach-<n>.png`).
The menus use it the other way round as well: `OtherGamePicture` gives a figure's picture from
another game on the same PC, which the portal menu takes where the game has none.

## The exe's icon

`skylanders_game_icon(<target> <rom folder>)` (cmake/game_icon.cmake) gives a game's exe its own
logo: at build time `tools/make_game_icon.ps1` reads the NXE dashboard tile (`nxeslot.jpg`) from the
owner's disc's `nxeart` package and writes it as an .ico (256 to 16 px) into the build folder,
which is built into the exe as its first icon. Explorer, the installers' shortcuts and, through
SDL, the window and the taskbar show it. Without a disc it is a plain icon of our own; the
picture never goes into a repository.

## Credits

- [Cemu](https://github.com/cemu-project/Cemu), [RPCS3](https://github.com/RPCS3/rpcs3) and
  [Dolphin](https://github.com/dolphin-emu/dolphin) already emulate the Portal of Power; their code
  was read to learn the commands, the Xbox 360 message format, the figure layout and the Trap Team
  speaker format. No code was copied.
- [Xenia Canary](https://github.com/xenia-canary/xenia-canary) for the XAM non-controller API.
- [Skylanders Ultimate NFC Pack](https://skylandersnfc.github.io/Skylanders-Ultimate-NFC-Pack/)
  for the figure dumps behind the name/id catalog.
- [SkyReader](https://github.com/reedstrm/SkyReader) and Marijn Kneppers'
  ["Reverse engineering Skylanders' Toys-to-life mechanics"](https://marijnkneppers.dev/posts/reverse-engineering-skylanders-toys-to-life-mechanics/)
  for figure save-data decoding.

## License

MIT (see `LICENSE`). Skylanders is a trademark of its owners; this project is unofficial.
