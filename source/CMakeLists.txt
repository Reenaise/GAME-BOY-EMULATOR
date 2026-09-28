cmake_minimum_required(VERSION 3.16)

project(gameboy_emulator
    VERSION 1.0.0
    DESCRIPTION "A Game Boy (DMG) emulator in C, based on the Cinoop design"
    LANGUAGES C)

# ---------------------------------------------------------------------------
# Language / build configuration
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

option(GB_BUILD_TESTS  "Build the unit test executable" ON)
option(GB_SDL2_STATIC  "Link SDL2 statically so SDL2.dll is not needed at runtime" ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    set(CMAKE_C_FLAGS_DEBUG   "-O0 -g")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

# ---------------------------------------------------------------------------
# Emulator core (no SDL dependency, so the tests can link it directly)
# ---------------------------------------------------------------------------
add_library(gbcore STATIC
    src/cpu.c
    src/instructions.c
    src/memory.c
    src/cartridge.c
    src/interrupts.c
    src/timer.c
    src/serial.c
    src/ppu.c
    src/input.c
    src/emulator.c
    src/debugger.c
    src/screenshot.c
)
target_include_directories(gbcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# SDL2 (prefer the local copy in SDL2 - no network needed)
# ---------------------------------------------------------------------------
set(GB_LOCAL_SDL2 "${CMAKE_CURRENT_SOURCE_DIR}/SDL2")
if(EXISTS "${GB_LOCAL_SDL2}/cmake/sdl2-config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${GB_LOCAL_SDL2}")
endif()

find_package(SDL2 CONFIG)
if(NOT SDL2_FOUND)
    message(FATAL_ERROR
        "SDL2 was not found.\n"
        "Download SDL2-devel-2.x.y-mingw.zip from https://github.com/libsdl-org/SDL/releases, "
        "extract it to SDL2 (so that SDL2/cmake/sdl2-config.cmake exists), "
        "or pass -DSDL2_DIR=<path to the folder containing sdl2-config.cmake>.")
endif()

add_executable(gameboy-emulator
    src/main.c
    src/display.c
)
target_link_libraries(gameboy-emulator PRIVATE gbcore)
# We provide our own main() and call SDL_SetMainReady(), so SDL2main is not used.
target_compile_definitions(gameboy-emulator PRIVATE SDL_MAIN_HANDLED)

if(GB_SDL2_STATIC AND TARGET SDL2::SDL2-static)
    target_link_libraries(gameboy-emulator PRIVATE SDL2::SDL2-static)
    if(MINGW)
        # Fully self-contained .exe: no SDL2.dll / libgcc / winpthread DLLs.
        target_link_options(gameboy-emulator PRIVATE -static)
    endif()
    message(STATUS "SDL2: linking statically (${SDL2_VERSION})")
else()
    target_link_libraries(gameboy-emulator PRIVATE SDL2::SDL2)
    if(WIN32)
        # Copy SDL2.dll next to the executable so it can start.
        add_custom_command(TARGET gameboy-emulator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:gameboy-emulator>)
    endif()
    message(STATUS "SDL2: linking dynamically (${SDL2_VERSION})")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GB_BUILD_TESTS)
    enable_testing()
    add_executable(gb_tests
        tests/test_main.c
        tests/test_util.c
        tests/test_cpu.c
        tests/test_memory.c
        tests/test_cartridge.c
        tests/test_interrupts.c
        tests/test_timer.c
        tests/test_ppu.c
        tests/test_input.c
        tests/test_integration.c
    )
    target_link_libraries(gb_tests PRIVATE gbcore)
    if(MINGW)
        target_link_options(gb_tests PRIVATE -static)
    endif()
    target_compile_definitions(gb_tests PRIVATE
        GB_ROMS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/roms")

    foreach(suite cpu memory cartridge interrupts timer ppu input integration)
        add_test(NAME ${suite} COMMAND gb_tests ${suite})
    endforeach()

    # Optional third-party test ROMs (not distributed with the project).
    # Put Blargg's cpu_instrs.gb / instr_timing.gb in build/test-roms/ (or set
    # GB_TEST_ROMS_DIR) and re-run CMake; they report through the serial port.
    set(GB_TEST_ROMS_DIR "${CMAKE_BINARY_DIR}/test-roms" CACHE PATH "Folder with optional test ROMs")
    if(EXISTS "${GB_TEST_ROMS_DIR}/cpu_instrs.gb")
        add_test(NAME blargg_cpu_instrs
                 COMMAND gameboy-emulator --headless --serial --frames 4000 "${GB_TEST_ROMS_DIR}/cpu_instrs.gb")
        set_tests_properties(blargg_cpu_instrs PROPERTIES PASS_REGULAR_EXPRESSION "Passed all tests")
    endif()
    if(EXISTS "${GB_TEST_ROMS_DIR}/instr_timing.gb")
        add_test(NAME blargg_instr_timing
                 COMMAND gameboy-emulator --headless --serial --frames 600 "${GB_TEST_ROMS_DIR}/instr_timing.gb")
        set_tests_properties(blargg_instr_timing PROPERTIES PASS_REGULAR_EXPRESSION "Passed")
    endif()
endif()
