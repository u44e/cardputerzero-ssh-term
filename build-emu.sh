#!/usr/bin/env bash
# build-emu.sh — build ssh_term as a .dylib for the cardputer-emu desktop emulator.
#
#   ./build-emu.sh           # build only
#   ./build-emu.sh --shot    # build + headless screenshot -> build-emu/shot.bmp
#   ./build-emu.sh --run     # build + launch interactive emulator window (type to test keys)
#
# Mirrors highway-kp/build-emu.sh: compiles lv_sdl_keyboard.c into the .so so the
# emulator's real SDL keyboard (full keys incl. Tab/arrows/ESC) binds at dlopen.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
EMU="${EMU_DIR:-$HOME/cardputer-zero/emulator}"
EMUBIN="$EMU/build/cardputer-emu"
OUT="$APP/build-emu"
DYLIB="$OUT/libssh_term.dylib"
mkdir -p "$OUT"

INC_COMPAT=()
[ -f "$EMU/src/emu_compat.h" ] && INC_COMPAT=(-include "$EMU/src/emu_compat.h")

# Phase 1: main.c + pty.c + term.c (PTY + libvterm). libvterm via pkg-config.
clang -dynamiclib -O2 -std=gnu11 -arch arm64 -fPIC \
  -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DAPP_EMU -DSSH_TERM_TEST_HOOKS \
  "${INC_COMPAT[@]}" \
  -I "$EMU" -I "$EMU/lib" -I "$EMU/lib/lvgl" -I "$APP/emu" \
  $(pkg-config --cflags sdl2) $(pkg-config --cflags vterm) \
  "$APP/src/main.c" "$APP/src/pty.c" "$APP/src/term.c" "$APP/src/config.c" "$APP/src/logsink.c" "$APP/src/sendfile.c" "$APP/src/vpn.c" "$APP/src/ime.c" \
  "$EMU/lib/lvgl/src/drivers/sdl/lv_sdl_keyboard.c" \
  $(pkg-config --libs vterm) \
  -undefined dynamic_lookup -lpthread -lm -liconv \
  -o "$DYLIB"

echo "built: $DYLIB"
nm -gU "$DYLIB" 2>/dev/null | grep -q lv_sdl_keyboard_create \
  && echo "ok: lv_sdl_keyboard_create exported (real keyboard)" \
  || echo "WARN: keyboard sym missing"

case "${1:-}" in
  --shot)
    echo "headless screenshot -> $OUT/shot.bmp"
    SDL_VIDEODRIVER=dummy EMU_SHOT="$OUT/shot.bmp" EMU_SHOT_MS=1200 EMU_SHOT_QUIT=1 \
      "$EMUBIN" "$DYLIB" || true
    ls -la "$OUT/shot.bmp" 2>/dev/null || echo "(no screenshot produced)"
    ;;
  --run)
    echo "launching interactive emulator (type to test keys; close window to exit)"
    "$EMUBIN" "$DYLIB"
    ;;
  *)
    echo "run interactively to test keyboard:  $EMUBIN $DYLIB"
    ;;
esac
