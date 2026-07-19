# port/CMakeLists.txt — standalone NetTerm binary + .deb for CardputerZero AppStore.
# Build inside an arm64 Debian Bookworm environment (matches the device ABI).
# LVGL v9.5 is provided via -DLVGL_DIR. See port/build.sh.
cmake_minimum_required(VERSION 3.20)
project(netterm VERSION 0.2.2 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(PORT_FBDEV "Build the on-device (fbdev + evdev) variant" OFF)
set(LVGL_DIR "" CACHE PATH "Path to an LVGL v9.5 source tree")
set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Repo root (holds src/, emu/, share/)")

if(NOT LVGL_DIR OR NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "Set -DLVGL_DIR to an LVGL v9.5 source tree")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM    REQUIRED vterm)
pkg_check_modules(FREETYPE REQUIRED freetype2)

# Our lv_conf.h drives LVGL (freetype, fonts, fbdev/evdev drivers; no SDL).
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
target_include_directories(lvgl PRIVATE ${FREETYPE_INCLUDE_DIRS})   # LVGL's freetype driver
target_link_libraries(lvgl PUBLIC ${FREETYPE_LIBRARIES})

add_executable(netterm
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    "${APP_ROOT}/src/main.c"    "${APP_ROOT}/src/pty.c"     "${APP_ROOT}/src/term.c"
    "${APP_ROOT}/src/config.c"  "${APP_ROOT}/src/logsink.c" "${APP_ROOT}/src/sendfile.c"
    "${APP_ROOT}/src/vpn.c"     "${APP_ROOT}/src/themes.c"
)
target_include_directories(netterm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${APP_ROOT}/src" "${APP_ROOT}/emu" "${LVGL_DIR}"
    ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_compile_definitions(netterm PRIVATE
    LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)   # production: no APP_EMU / test hooks
target_link_libraries(netterm PRIVATE lvgl ${VTERM_LIBRARIES} ${FREETYPE_LIBRARIES} pthread m)
if(PORT_FBDEV)
    target_compile_definitions(netterm PRIVATE PORT_FBDEV)
    target_sources(netterm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/evdev_kbd.c")
endif()

# ---- install layout: /usr/share/APPLaunch (Exec=bin/netterm is relative) ----
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/netterm.desktop"
"[Desktop Entry]\nName=NetTerm\nExec=bin/netterm\nTerminal=false\nIcon=share/images/netterm.png\nType=Application\n")

install(TARGETS netterm RUNTIME DESTINATION share/APPLaunch/bin)
install(FILES "${APP_ROOT}/share/images/netterm.png"
        DESTINATION share/APPLaunch/share/images)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/netterm.desktop"
        DESTINATION share/APPLaunch/applications)

# ---- CPack: arm64 .deb (Maintainer must equal the PR author at publish) ----
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_NAME "netterm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_NAME "netterm")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "u44e <u44e@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "NetTerm — SSH/telnet/serial/shell terminal for CardputerZero")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6, libfreetype6, libvterm0, libevdev2, openssh-client, telnet, picocom")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_STRIP_FILES TRUE)   # ship a stripped production binary (no debug symbols)
set(CPACK_PACKAGE_FILE_NAME "netterm_${PROJECT_VERSION}_arm64")
set(CPACK_PACKAGE_DIRECTORY "${APP_ROOT}/port/dist")
include(CPack)
