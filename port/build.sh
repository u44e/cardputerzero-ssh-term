#!/usr/bin/env bash
# port/build.sh — build the standalone NetTerm binary + arm64 .deb for the
# CardputerZero AppStore, inside an arm64 Debian Bookworm container (matches the
# device ABI). On Apple Silicon this runs natively (fast).
#
#   ./port/build.sh
#     1. verify build (off-screen memory display) — runs headless, proves the
#        app works on arm64 (seeds term.conf, exits on SIGTERM).
#     2. device build (-DPORT_FBDEV: lv_linux_fbdev + lv_evdev) — compile+link.
#     3. CPack -> port/dist/netterm_<ver>-m5stack1_arm64.deb, then inspect it.
#
# Objects build on the container-local fs (/tmp) — the macOS bind mount flakes
# under LVGL's ~900-file build. Source + LVGL clone + .deb output use the mount.
set -euo pipefail
APP="$(cd "$(dirname "$0")/.." && pwd)"

docker run --rm --platform linux/arm64 -v "$APP":/work -w /work debian:bookworm bash -euo pipefail -c '
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends \
  cmake ninja-build build-essential pkg-config git ca-certificates file dpkg-dev \
  libvterm-dev libfreetype-dev libpng-dev libjpeg-dev zlib1g-dev libevdev-dev >/dev/null

LVGL=/work/port/.lvgl
[ -f "$LVGL/lvgl.h" ] || git clone --depth 1 -b v9.5.0 https://github.com/lvgl/lvgl.git "$LVGL" >/dev/null 2>&1

# lv_conf.h from the template: freetype + fonts + fbdev/evdev (NO SDL).
CONF=/work/port/lv_conf.h
cp "$LVGL/lv_conf_template.h" "$CONF"
sed -i \
  -e "0,/#if 0/s//#if 1/" \
  -e "s/#define LV_FONT_MONTSERRAT_12 0/#define LV_FONT_MONTSERRAT_12 1/" \
  -e "s/#define LV_FONT_MONTSERRAT_14 0/#define LV_FONT_MONTSERRAT_14 1/" \
  -e "s/#define LV_FONT_UNSCII_8  0/#define LV_FONT_UNSCII_8  1/" \
  -e "s/#define LV_USE_FREETYPE 0/#define LV_USE_FREETYPE 1/" \
  -e "s/#define LV_USE_LINUX_FBDEV      0/#define LV_USE_LINUX_FBDEV      1/" \
  -e "s/#define LV_USE_EVDEV    0/#define LV_USE_EVDEV    1/" \
  "$CONF"

cfg() {  # $1=builddir $2=extra
  cmake -S /work/port -B "$1" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLVGL_DIR="$LVGL" ${2:-} >"$1/cfg.log" 2>&1 \
    || { echo "CONFIGURE FAILED"; tail -15 "$1/cfg.log"; exit 1; }
}
mkdir -p /tmp/bv /tmp/bf

echo "### 1. verify build (memory display, arm64) ###"
cfg /tmp/bv ""
cmake --build /tmp/bv -j"$(nproc)" >/tmp/bv/b.log 2>&1 \
  || { echo "BUILD FAILED"; grep -nE "error:|undefined reference|fatal error" /tmp/bv/b.log | head; exit 1; }
echo "verify binary: $(file -b /tmp/bv/netterm | cut -d, -f1-2)"
rm -f /tmp/nt.conf
SDL_VIDEODRIVER=dummy TERM_CONF=/tmp/nt.conf /tmp/bv/netterm & PID=$!
sleep 3
kill -0 $PID 2>/dev/null && { kill -TERM $PID; sleep 1; }
kill -0 $PID 2>/dev/null && { echo "verify: DID NOT EXIT"; kill -9 $PID; } || echo "verify: exited cleanly on SIGTERM"
[ -s /tmp/nt.conf ] && echo "verify: app ran on arm64 (seeded: $(head -1 /tmp/nt.conf))" || echo "verify: NO config seeded"

echo "### 2. device build (fbdev + evdev, arm64) ###"
cfg /tmp/bf "-DPORT_FBDEV=ON"
cmake --build /tmp/bf -j"$(nproc)" >/tmp/bf/b.log 2>&1 \
  || { echo "BUILD FAILED"; grep -nE "error:|undefined reference|fatal error" /tmp/bf/b.log | head; exit 1; }
echo "device binary: $(file -b /tmp/bf/netterm | cut -d, -f1-2)"

echo "### 3. .deb (CPack) ###"
mkdir -p /work/port/dist
( cd /tmp/bf && cpack -G DEB >/tmp/bf/cpack.log 2>&1 ) || { echo "CPACK FAILED"; tail -15 /tmp/bf/cpack.log; exit 1; }
DEB=$(ls /work/port/dist/netterm_*_arm64.deb | head -1)
echo "== $DEB =="
dpkg-deb -I "$DEB" | sed -n "1,20p"
echo "--- contents ---"; dpkg-deb -c "$DEB" | awk "{print \$1, \$6}"
echo "--- md5 ---"; md5sum "$DEB"
'
