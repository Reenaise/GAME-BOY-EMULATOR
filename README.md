# Game Boy Emulator

A Game Boy (DMG) emulator written in C with SDL2. Sound is not emulated.

## Build and Play

### Prerequisites

You need GCC for MinGW-w64, CMake, and Ninja. SDL2 is included under
`source/SDL2`. Python 3 is only needed if you want to generate the demo ROMs.
For example, install the build tools with winget:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT Kitware.CMake Ninja-build.Ninja
```

### Building

From the project root, configure and build:

```powershell
cmake -S source -B source/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build source/build
```

The executable will be at `source/build/gameboy-emulator.exe`.

### Running a Game

To generate the demo ROMs, run this from the project root:

```powershell
python source/tools/make_test_roms.py
```

Then run the demo:

```powershell
.\source\build\gameboy-emulator.exe .\source\roms\demo.gb
```

Or pass the path to a ROM you are legally entitled to use:

```powershell
.\source\build\gameboy-emulator.exe "path\to\your-game.gb"
```

### Controls

| Game Boy button | Key |
|---|---|
| D-pad | Arrow keys |
| A | X |
| B | Z |
| Start | Enter |
| Select | Backspace |
| Pause | P |
| Fast forward (hold) | Tab |
| Reset | F2 |
| Screenshot | F12 |
| Quit | Esc |

### Tests

Generate the homebrew ROMs first, then run the test suite from the repository
root:

```powershell
python source/tools/make_test_roms.py
ctest --test-dir source/build --output-on-failure
```

### More Information

Build options, debugger commands, and emulator details are in
[source/README.md](source/README.md). The hardware notes and code walkthrough
are in [source/docs](source/docs).

## License

The emulator is MIT-licensed; see [source/LICENSE](source/LICENSE). The bundled
SDL2 files use the zlib license.
