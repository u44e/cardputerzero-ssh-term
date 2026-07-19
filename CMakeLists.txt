cmake_minimum_required(VERSION 3.16)
project(ssh_term C)

# Standalone app (not under AppBuilder/examples). Point at the SDK + emulator
# (override with -DCZ_SDK_DIR / -DCZ_EMULATOR_DIR).
if(NOT DEFINED CZ_SDK_DIR)
    set(CZ_SDK_DIR "$ENV{HOME}/Projects/CardputerZero-AppBuilder/sdk")
endif()
if(NOT DEFINED CZ_EMULATOR_DIR)
    set(CZ_EMULATOR_DIR "$ENV{HOME}/cardputer-zero/emulator")
endif()

list(APPEND CMAKE_MODULE_PATH "${CZ_SDK_DIR}/cmake")
include(CZApp)

# NO_KBD_STUBS: keep lv_sdl_keyboard_* unresolved so the host's real keyboard
# driver binds at dlopen (full keys incl. Tab/arrows/ESC). czpi/build.sh compiles
# lv_sdl_keyboard.c into the .so for the arm64 path.
cz_add_lvgl_app(ssh_term NO_KBD_STUBS
    SOURCES
        src/main.c
        src/pty.c
        src/term.c
        src/config.c
        src/themes.c
        src/logsink.c
        src/sendfile.c
        src/vpn.c
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED vterm)
target_include_directories(ssh_term PRIVATE ${VTERM_INCLUDE_DIRS})
target_link_libraries(ssh_term PRIVATE ${VTERM_LIBRARIES} pthread)
