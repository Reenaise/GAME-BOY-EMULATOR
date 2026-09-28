Game Boy Emulator
=================

A Game Boy (DMG) emulator written in C with SDL2. Sound is not emulated.

Running it
----------

To start a game, drag its `.gb` file onto `gameboy-emulator.exe`.

There is a small demo in `roms`. From PowerShell at the repository root, run:

    .\gameboy-emulator.exe .\roms\demo.gb

You can also pass a ROM from somewhere else. Put the path in quotes if it has
spaces:

    .\gameboy-emulator.exe "C:\Games\My Game.gb"

The executable includes SDL2, so you do not need to install SDL2 or copy an
SDL2.dll next to it. Windows may warn that the executable is unsigned; only
run it if you trust where you got it.

`roms\demo.gb` is a little demo where you can move a smiley with the arrow
keys. `roms\mbc1_test.gb` is a technical test and does not show anything.


Controls
--------

  Game Boy          Keyboard
  --------          --------
  D-pad             Arrow keys
  A                 X
  B                 Z
  Start             Enter
  Select            Backspace

  Emulator
  --------
  Esc               Quit (the game's save is written to <game>.sav)
  P                 Pause
  Tab (hold)        Fast forward
  F2                Reset
  F12               Screenshot (screenshot000.png, ...)
  Space / F1        Open the debugger in the console window

  To use other keys, copy keys.example.cfg, edit it and run:
     gameboy-emulator.exe --keys mykeys.cfg game.gb


ROMs
----

  Use ROMs you have the right to use. The demo is homebrew; you do not need
  any commercial game ROM to try the emulator.

  Free, legal games that work well:
   * Libbet and the Magic Floor (puzzle game, zlib license)
       https://github.com/pinobatch/libbet/releases  -> download libbet.gb
   * More free homebrew:  https://hh.gbdev.io  and  itch.io (search "game boy rom")

  Supported cartridge types are ROM-only, MBC1, MBC3 (with clock), and MBC5.
  To check a ROM before starting it:

     gameboy-emulator.exe --info game.gb

  "Game Boy Color only" games (like Pokemon Crystal) load, but show their
  "this game is only for Game Boy Color" screen, as on a real Game Boy.
  "Black cartridge" games that also support the original Game Boy run in
  black and white.


Command-line options
--------------------

  gameboy-emulator.exe [options] game.gb

   -h, --help            show help
   -s, --scale N         window size 1-10 (default 4 = 640x576)
   -k, --keys FILE       custom key mapping
   -d, --debug           start in the debugger
   --info                show the cartridge header and quit
   --headless            run without a window (for testing)
   --frames N            quit after N frames
   --screenshot FILE     save the last frame as PNG when quitting
   --serial              print what the game sends over the link cable
   --trace FILE          write a trace of every instruction
   --bootrom FILE        run your own dump of the Game Boy boot ROM


Building from source
--------------------

  The source is in `source`. SDL2 is included there, so you do not need to
  download it separately.

    a) Install GCC (MinGW-w64), CMake, and Ninja. For example, in PowerShell:

      winget install BrechtSanders.WinLibs.POSIX.UCRT
      winget install Kitware.CMake
      winget install Ninja-build.Ninja

     Then CLOSE and REOPEN the terminal so the new PATH is picked up.
     Check it worked:   gcc --version   cmake --version   ninja --version

  b) Build:

       cd source
       cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
       cmake --build build

     Result:  source\build\gameboy-emulator.exe

  c) Run the tests (optional):

       ctest --test-dir build --output-on-failure

     Expected result: "100% tests passed".

  There is more documentation in `source\README.md` and `source\docs`.

  Troubleshooting
   * "cmake is not recognized": reopen the terminal after winget, or
     restart Windows.
   * "SDL2 was not found": make sure source\third_party\SDL2 exists; it is
     included in the repository.
   * If you already have MSYS2/MinGW, any 64-bit MinGW GCC works too.


About
-----

  The emulator's design was inspired by Cinoop by CTurt
  (https://cturt.github.io/cinoop.html). The CPU passes Blargg's cpu_instrs
  and instr_timing test ROMs.
  Code license: MIT (source\LICENSE).
  SDL2: zlib license (source\third_party\SDL2\LICENSE.txt).

  Not emulated: sound, Game Boy Color colours, Super Game Boy borders,
  link-cable multiplayer, and the rare MBC2 / MMM01 / HuC cartridges.
