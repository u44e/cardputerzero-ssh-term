# 実機(M5CardputerZero)ブリングアップ手順 — 2026年11月以降に実施

Mac側の実装・検証は完了済み。残るは**実機でしか確認できない項目**のみ。
このチェックリスト順に進めれば、迷わず再開できる。

## 0. 前提
- **M5CardputerZero**（Raspberry Pi OS / AArch64）を起動。※手元検証を Pi Zero 2W 代替機で行う場合は
  headless cloud-init NoCloud（2.4GHz Wi-Fi。`pi-zero2w-headless-cloudinit` メモリ参照）。
- ホストにフォントが常駐していること: `/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf` と
  `AlibabaPuHuiTi-3-55-Regular.ttf`（APPLaunch導入で入る）。無ければ別途配置。

## 1. ビルド & デプロイ（Mac から）
```sh
~/cardputer-zero/czpi/build.sh ~/Projects/cardputerzero-ssh-term     # out/libssh_term.so (arm64) 生成
~/cardputer-zero/czpi/deploy-run.sh ssh_term --host pi@<IP>          # labwc にウィンドウ表示
```
- 実機ランタイム依存（deploy-run.sh が libvterm0 まで自動。残りは一度だけ手動）:
  `sudo apt install -y openssh-client telnet picocom`（任意: `wireguard-tools openvpn fcitx5-mozc`）

## 2. 検証チェックリスト
- [ ] **R1: フルキー到達** — `` ` `` IMEオフで、英字/記号/**Fn矢印/Tab/ESC** が端末に通る（特にTab補完・矢印履歴・vimのESC）。
      → 食われる場合は単一objグループの focus 無効化を確認（main.c attach_capture）。
      → **F1-F12/PgUp/PgDn/Ins/Alt+文字** はホストのキードライバが em と同じタグ
        （Ctrl=`0x40000000|c` / Alt=`0x20000000|k` / Fn系=`0x10000000|code`）を届ける前提。
        届かなければホスト側ドライバ拡張（emのパッチ `lv_sdl_keyboard.c` 参照）。
- [ ] **実 SSH 接続** — プロファイルで proto=ssh、LAN機器へ。鍵/パスワード、vim/htop が**色付き**で動く。
- [ ] **実 telnet 接続** — 国交省/網機器へ。
- [ ] **USB-シリアルコンソール** — USB-シリアル変換を挿し proto=serial（Device=`/dev/ttyUSB0`, Baud=9600等）→
      picocom 経由で網機器コンソールに入れる。切断は Close session（picocom ごと終了）。
      → デバイスが `dialout` グループ権限を要求する場合は `sudo usermod -aG dialout pi`。
      → **Format 7E1** の機器で文字化けしないこと。**Send BREAK**（メニュー）が picocom の
        send-break escape `C-a C-\`（0x01 0x1C）として通り BREAK が出ること（違えば main.c MI_BREAK のバイト列を修正）。
- [ ] **ssh 鍵/keep-alive** — Key に `-i` 鍵パスを入れて鍵認証で入れる。LAN を切って約90秒で
      DISCONNECTED になる（ServerAliveInterval=30×3）。
- [ ] **フォントサイズ** — 編集の Size、Session Menu の Font size で 12/16/20px 実行中切替（桁行が変わる）。
- [ ] **ログ** — log=1 で接続→`/sdcard/logs/<name>-<ts>.log` 生成、ログ閲覧で ANSI除去表示・↑↓スクロール。
- [ ] **設定流し込み** — SIDEキー→Send file→ファイル選択→**Detected: Shift_JIS** 表示→Enterで UTF-8変換送出（機器に流れる）。
- [ ] **日本語入力(OS IME)** — fcitx5-mozc を Wayland で有効化（`GTK_IM_MODULE=fcitx` 等）。漢字変換した文字が端末へ。
      → 候補窓は fcitx5 が自前表示。**アプリ内IMEは持たない（OS委譲）**ので、効かない時は fcitx5 側の設定
        （`GTK_IM_MODULE`/`QT_IM_MODULE`/`XMODIFIERS`、fcitx5 の自動起動、labwc/Wayland対応）を確認する。
- [ ] **Ctrl 全種** — Ctrl-C/D/Z/L 等（実機ホストの key_item 修飾。emで出なかった分）。
      → 不足なら main.c に `LV_EVENT_KEYBOARD`(key_item) ハンドラ追加を検討（plan 07-input 知見）。
- [ ] **VPN** — 事前にOSへVPN接続を作成 → プロファイルの **Connection** に接続名。`pkexec nmcli connection up <名>`
      （NM無しは `wg-quick up <名>` 等へfallback）が通るか。失敗時ダイアログ「Connect anyway」。終了で down。
      → **秘密情報はアプリに保存しない**（OS側が保持。旧 term.conf の秘密キーは次回保存で消去）。
        polkit エージェント無し環境は pkexec 失敗するので、下記 **§4 の polkit ルール**を配置する。
- [ ] **SIDEキー** — 実機の物理SIDEで Session Menu が開く（emは未配線。`app_event(CZ_EV_SIDE_KEY)`受信を確認）。

## 3. 残実装（実機確認後に必要なら）
- 接続中オーバーレイ（VPN→疎通→ssh 段階表示, モック10）。
- Ctrl全種のための key_item 入力経路（届くか要実機確認→届けば実装）。
- （済）VPNはOS管理へ集約：アプリは接続名のみ保持、`nmcli` 優先＋秘密非保存。ipsec/xl2tpd の config 自前生成TODOは廃止。

## 4. polkit ルール（VPN を pkexec で無人起動）
labwc/headless では polkit 認証エージェントが無く `pkexec` がパスワード要求で失敗する。pi ユーザに VPN 起動
だけを無認証で許可するルールを1本置く（NetworkManager 経由の例）。

`/etc/polkit-1/rules.d/49-ssh_term-vpn.rules`:
```js
polkit.addRule(function(action, subject) {
  if (subject.user == "pi" &&
      action.id.indexOf("org.freedesktop.NetworkManager") == 0) {
    return polkit.Result.YES;
  }
});
```
- 反映: `sudo systemctl restart polkit`。確認: `pkexec nmcli connection up <名>` がパスワード無しで通る。
- NM を使わず `wg-quick`/`ipsec` を直接叩く構成では、対象コマンド用に pkexec action + sudoers/polkit を別途用意する
  （例: `/etc/sudoers.d` で `pi ALL=(root) NOPASSWD: /usr/bin/wg-quick` ＋ 起動側を sudo に変更）。

## 5. 公式 AppStore 移植（実機到着後にやること）
`port/` の arm64 `.deb` は実機なしでビルド済み（`./port/build.sh`）。残るは実機依存の keymap 確定と検証。詳細は `docs/APPSTORE.md`。

- [ ] **keymap 採取** — 実機で `evtest`（または `cat /dev/input/event*` + `libinput debug-events`）を実行し、
      各物理キー（英字/数字/記号、`fn`/`ctrl`/`alt`/`Aa(shift)`、矢印=`F▲ Z◀ X▼ C▶`、**SIDEボタン**）の
      **evdev keycode** と、キーボードの event デバイス番号（`/dev/input/eventN`）を記録。
- [ ] **`port/evdev_kbd.c` の確定** — 採取した keycode で:
      (a) **Fn/記号レイヤ**の配列を `KMAP` に反映（US と異なる。例 `Q~ W\` E+ R- …`）。
      (b) **SIDEキー** の keycode を `-DAPP_SIDE_KEYCODE=<n>` で指定（暫定 `KEY_MENU`）。
      (c) 矢印が Fn 合成なら Fn 修飾の扱いを追加。
- [ ] **framebuffer デバイス** — 小型LCDが `/dev/fb0` か `/dev/fb1` か確認。`LV_LINUX_FBDEV_DEVICE` を
      launcher が注入するか、`.desktop` をラッパー化して設定（`docs/APPSTORE.md` 参照）。
- [ ] **実機 install→launch→exit→uninstall**（公式 acceptance）:
      `sudo apt install --no-install-recommends ./ssh_term_0.2.2-m5stack1_arm64.deb` →
      APPLaunch にアイコン表示 → 起動 → 320×170 で表示崩れ無し → 打鍵（Ctrl/Alt/Fn/矢印/日本語）→
      **短ESC=戻る / 長ESC・Home=終了**でクリーンに launcher へ復帰 → `sudo apt remove ssh_term` でエントリ消滅。
- [ ] **フォント** — `/usr/share/APPLaunch/share/font/` に Liberation Mono + AlibabaPuHuiTi が常駐し、
      端末・日本語が tofu にならないこと（無ければ同梱 or 依存追加を検討）。
- [ ] **公開判断** — `source.openness`（private→公開 or CardputerZero org 移管。審査で有利）。
- [ ] **提出** — `czdev login` → `czdev publish --deb port/dist/ssh_term_0.2.2-m5stack1_arm64.deb`
      （Maintainer email = 自分の GitHub。`.deb` は CI 関門を満たす構造）。

## 参考
- AppStore 移植・提出: `docs/APPSTORE.md`（フェーズ・レジストリ・ポリシー準拠）
- 実装プラン: `~/.claude/plans/cardputerzero-vpn-delightful-adleman.md`
- 画面仕様/モック: `docs/SCREENS.md` / `docs/mockups/`
- 全体: `README.md`
