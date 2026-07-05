#!/usr/bin/env bash
# standalone/build.sh — build NetTerm as a STANDALONE binary (no dlopen host).
#
# Desktop proof-of-port: links the app sources + a main() that owns an SDL
# display + keyboard, instead of being dlopen'd by the emulator. The on-device
# AppStore build swaps SDL for lv_linux_fbdev + lv_evdev (see docs/APPSTORE.md);
# main()'s app_main() call is identical. LVGL is linked from the emulator's
# prebuilt liblvgl.a here for a fast local build; the submission build uses
# FetchContent LVGL 9.5 via a Template-style CMake.
set -euo pipefail
APP="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU_DIR:-$HOME/cardputer-zero/emulator}"
LVGL_A="$EMU/build/lib/lvgl/lib/liblvgl.a"
OUT="$APP/standalone/out"; mkdir -p "$OUT"
BIN="$OUT/netterm"

[ -f "$LVGL_A" ] || { echo "liblvgl.a not found ($LVGL_A) — build the emulator first"; exit 1; }
INC_COMPAT=(); [ -f "$EMU/src/emu_compat.h" ] && INC_COMPAT=(-include "$EMU/src/emu_compat.h")
FT_INC="$(pkg-config --cflags freetype2 2>/dev/null || echo -I/opt/homebrew/opt/freetype/include/freetype2)"

clang -O2 -std=gnu11 -arch arm64 \
  -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DAPP_EMU -DSSH_TERM_TEST_HOOKS \
  "${INC_COMPAT[@]}" \
  -I "$EMU" -I "$EMU/lib" -I "$EMU/lib/lvgl" -I "$APP/emu" $FT_INC \
  $(pkg-config --cflags sdl2) $(pkg-config --cflags vterm) \
  "$APP/standalone/main.c" \
  "$APP/src/main.c" "$APP/src/pty.c" "$APP/src/term.c" "$APP/src/config.c" \
  "$APP/src/logsink.c" "$APP/src/sendfile.c" "$APP/src/vpn.c" \
  "$EMU/lib/lvgl/src/drivers/sdl/lv_sdl_keyboard.c" \
  "$LVGL_A" \
  $(pkg-config --libs sdl2) $(pkg-config --libs vterm) \
  -lpthread -lm -liconv -lfreetype \
  -o "$BIN"
echo "built: $BIN"
