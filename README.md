# NetTerm (`ssh_term`) — CardputerZero SSH / telnet / serial / shell terminal

A portable terminal for the **M5CardputerZero** (AArch64 Linux / Raspberry Pi OS, LVGL 9.5,
320×170). SSH / telnet / **USB-serial console** / local-shell sessions with
connection profiles, OS-managed VPN, session logging, file injection (any text,
charset auto-detect), quick-send macros, Japanese input (OS IME), and a
switchable EN/JA UI.

> Display name **NetTerm** (SSH is just one of four transports); the package /
> binary id stays `ssh_term`, so all build & deploy commands are unchanged.

The terminal core is a clean reimplementation — **libvterm (MIT) + a self-written
`forkpty` wrapper** — so no third-party launcher code is copied. Fonts at runtime
are the device's own (Liberation Mono + Alibaba PuHui Ti), not redistributed.

## Features

| Feature | Status |
|---|---|
| Local shell / SSH / telnet via PTY (libvterm) | ✅ working |
| **USB-serial console** (proto=serial → `picocom -b <baud> <device>`; Host/Port fields become Device/Baud) | ✅ argv / ⏳ device |
| **Quick-send macros**: save one-line commands, send from Session Menu (+Enter); in-app add/edit/delete | ✅ working |
| Connection profiles (list / **editor** CRUD / persistence) | ✅ working |
| Terminal render: Liberation Mono 45×12 (freetype; CJK + unscii_8 fallback) | ✅ working |
| **Scrollback** (400-line ring): **Alt+↑↓** line / **Alt+←→** page — stays live; typing returns to latest | ✅ working |
| **Font size** (per-profile + live via menu / editor Size field) 12/16/20px | ✅ working |
| Status bar (name / CONNECTED / REC / SIDE) | ✅ working |
| Session logging (raw tee) + log browser (ANSI-stripped, scrollable) | ✅ working |
| File injection (any text — configs, scripts, snippets): **charset auto-detect → iconv → paced send** + dialog | ✅ working |
| Session menu + file browser (Send file) + **confirm dialog** (delete / VPN-fail) | ✅ working |
| Japanese input: **OS IME (fcitx5-mozc) via SDL_TEXTINPUT** — system toggle (Ctrl+Space), same as other apps; no in-app IME | ✅ plumbed (needs IME running) |
| **VPN type selector** (iPhone-style): WireGuard / OpenVPN / IKEv2 / L2TP / Tailscale | ✅ UI |
| VPN bring-up: **OS-managed** — profile stores only a connection NAME (no secrets in `term.conf`); `pkexec nmcli connection up <name>` (NetworkManager), else per-tool fallback | ⏳ exec is device-only |
| VPN readiness probe (getifaddrs) + teardown | ✅ probe / ⏳ exec is device-only |
| Connecting overlay (VPN→probe→ssh staged) | ⏳ not yet (cosmetic) |
| **SGR in terminal**: fg + **bg colors, reverse-video, underline** (per color-run labels; default green-on-black) | ✅ working (ls --color / htop / vim) |
| Ctrl combos (full set needs key_item / `LV_EVENT_KEYBOARD`) | ◐ partial on emu, full on device |

## Documentation
- **[取扱説明書 (User manual)](docs/MANUAL.md)** — 画面付き / illustrated per-screen ([PDF](docs/MANUAL.pdf))
- **[実機セットアップ手順 (Setup guide)](docs/SETUP.md)** — Mac→M5CardputerZero ([PDF](docs/SETUP.pdf))
- [機能一覧 (Feature list)](docs/FEATURES.md)
- [画面仕様 / mockups](docs/SCREENS.md)
- [実機ブリングアップ手順 (device checklist)](docs/DEVICE_CHECKLIST.md)

## Build & run

### Desktop (macOS emulator — fast dev loop)
```sh
./build-emu.sh --run     # build .dylib + launch cardputer-emu window (type to use)
./build-emu.sh --shot    # build + headless screenshot -> build-emu/shot.bmp
```
Requires: `brew install libvterm`, the emulator at `~/cardputer-zero/emulator`.

### Device (M5CardputerZero, arm64, via czpi — a Pi Zero 2W also works as a dev rig)
```sh
~/cardputer-zero/czpi/build.sh  ~/Projects/cardputerzero-ssh-term   # -> out/libssh_term.so
~/cardputer-zero/czpi/deploy-run.sh ssh_term --host pi@<ip>
```
Pi runtime deps (one-time): `libvterm0` (added to deploy-run.sh), plus
`openssh-client telnet` and optionally `wireguard-tools openvpn mozc-server`.

## Keys

**Sessions list:** `↑/↓` select · `Enter` connect · `e` edit · `n` new · `d` delete · `l` logs · `g` EN/JA UI
**Editor:** `↑/↓` field · `Enter` edit (text) · `←/→` toggle (proto/VPN/log) · `s` save · `ESC` back
**Terminal:** all keys → PTY · **SIDE key** opens the session menu (`Fn+Q` on the emulator) · **Alt+↑↓/←→** scroll back through history
**Japanese input:** via the OS IME (fcitx5-mozc); system toggle (Ctrl+Space) — no in-app IME

## Profiles

Flat `key=value` file at `$TERM_CONF` (default `/sdcard/term.conf`):
```
profiles=2
p0.name=home-pi   p0.proto=ssh   p0.host=192.168.1.50  p0.port=22  p0.user=pi  p0.vpn=  p0.log=1
p1.name=router    p1.proto=telnet p1.host=192.168.1.1  p1.port=23
```
`proto` = `ssh` | `telnet` | `shell` | `serial` (serial: `host`=device, `port`=baud).
Macros are stored globally as `mac<i>.name` / `mac<i>.text`. Defaults are seeded on first run.

## Layout
```
src/main.c       screens + routing + key dispatch (profiles/editor/term/logs/menu/files)
src/pty.{c,h}    self-written forkpty wrapper
src/term.{c,h}   libvterm + per-row LVGL render + reader thread + key->PTY
src/config.{c,h} profiles persistence
src/logsink.{c,h} raw log tee + browser + ANSI strip
src/sendfile.{c,h} charset detect + iconv->UTF-8 + paced send
src/vpn.{c,h}    OS-managed bring-up (nmcli/per-tool by name, no secrets) + getifaddrs probe
                 (Japanese input = OS IME; no in-app IME module)
docs/SCREENS.md  screen spec + docs/mockups/ PNGs   docs/gen_mockups.py
```

## Env hooks (headless testing — compiled in only with `-DSSH_TERM_TEST_HOOKS`)
`TERM_CONF` `TERM_LOGDIR` `TERM_FILEDIR` set paths; `UI_LANG=ja` forces the JA UI;
`AUTO_CONNECT=<i>` `AUTO_EDIT=<i>` `AUTO_LOGS` `AUTO_FILES` `AUTO_MENU`
`AUTO_MACROS` `AUTO_MACRO_EDIT=<i>` `AUTO_MACRO_SEND=<i>`
`AUTO_SENDFILE=<path>` drive screens on startup for screenshots. Device/production
builds (no `SSH_TERM_TEST_HOOKS`) exclude all of these.

## License notes
App code is original (MIT-intended). The CardputerZero launcher is unlicensed, so
**no launcher code is copied** — only behavior was studied. libvterm is MIT. Fonts
are loaded from the device (not redistributed); any bundled fonts would be OFL/UFL/PD.
