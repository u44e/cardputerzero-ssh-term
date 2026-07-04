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
- [ ] **実 SSH 接続** — プロファイルで proto=ssh、LAN機器へ。鍵/パスワード、vim/htop が**色付き**で動く。
- [ ] **実 telnet 接続** — 国交省/網機器へ。
- [ ] **USB-シリアルコンソール** — USB-シリアル変換を挿し proto=serial（Device=`/dev/ttyUSB0`, Baud=9600等）→
      picocom 経由で網機器コンソールに入れる。切断は Close session（picocom ごと終了）。
      → デバイスが `dialout` グループ権限を要求する場合は `sudo usermod -aG dialout pi`。
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

## 参考
- 実装プラン: `~/.claude/plans/cardputerzero-vpn-delightful-adleman.md`
- 画面仕様/モック: `docs/SCREENS.md` / `docs/mockups/`
- 全体: `README.md`
