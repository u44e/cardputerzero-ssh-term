# 実機(Pi Zero 2W)ブリングアップ手順 — 2026年11月以降に実施

Mac側の実装・検証は完了済み。残るは**実機でしか確認できない項目**のみ。
このチェックリスト順に進めれば、迷わず再開できる。

## 0. 前提
- Pi Zero 2W を headless セットアップ（cloud-init NoCloud、2.4GHz Wi-Fi。`pi-zero2w-headless-cloudinit` メモリ参照）。
- ホストにフォントが常駐していること: `/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf` と
  `AlibabaPuHuiTi-3-55-Regular.ttf`（APPLaunch導入で入る）。無ければ別途配置。

## 1. ビルド & デプロイ（Mac から）
```sh
~/cardputer-zero/czpi/build.sh ~/Projects/cardputerzero-ssh-term     # out/libssh_term.so (arm64) 生成
~/cardputer-zero/czpi/deploy-run.sh ssh_term --host pi@<IP>          # labwc にウィンドウ表示
```
- 実機ランタイム依存（deploy-run.sh が libvterm0 まで自動。残りは一度だけ手動）:
  `sudo apt install -y openssh-client telnet`（任意: `wireguard-tools openvpn fcitx5-mozc`）

## 2. 検証チェックリスト
- [ ] **R1: フルキー到達** — `` ` `` IMEオフで、英字/記号/**Fn矢印/Tab/ESC** が端末に通る（特にTab補完・矢印履歴・vimのESC）。
      → 食われる場合は単一objグループの focus 無効化を確認（main.c attach_capture）。
- [ ] **実 SSH 接続** — プロファイルで proto=ssh、LAN機器へ。鍵/パスワード、vim/htop が**色付き**で動く。
- [ ] **実 telnet 接続** — 国交省/網機器へ。
- [ ] **フォントサイズ** — 編集の Size、Session Menu の Font size で 12/16/20px 実行中切替（桁行が変わる）。
- [ ] **ログ** — log=1 で接続→`/sdcard/logs/<name>-<ts>.log` 生成、ログ閲覧で ANSI除去表示・↑↓スクロール。
- [ ] **設定流し込み** — SIDEキー→Send file→ファイル選択→**Detected: Shift_JIS** 表示→Enterで UTF-8変換送出（機器に流れる）。
- [ ] **日本語入力(OS IME)** — fcitx5-mozc を Wayland で有効化（`GTK_IM_MODULE=fcitx` 等）。漢字変換した文字が端末へ。
      → 候補窓は fcitx5 が自前表示。効かない時は `` ` `` で自前ローマ字→ひらがな フォールバックを使用。
- [ ] **Ctrl 全種** — Ctrl-C/D/Z/L 等（実機ホストの key_item 修飾。emで出なかった分）。
      → 不足なら main.c に `LV_EVENT_KEYBOARD`(key_item) ハンドラ追加を検討（plan 07-input 知見）。
- [ ] **VPN** — プロファイルに vpn 名、`pkexec wg-quick up <名>` が通るか。失敗時ダイアログ「Connect anyway」。終了で down。
      → polkit エージェント無し環境は pkexec 失敗するので、`zero-helper` 拡張 or polkit ルールを検討。
- [ ] **SIDEキー** — 実機の物理SIDEで Session Menu が開く（emは未配線。`app_event(CZ_EV_SIDE_KEY)`受信を確認）。

## 3. 残実装（実機確認後に必要なら）
- VPN設定スキーマ拡張（type=wireguard/openvpn/tailscale, config/up/down コマンド）。
- 接続中オーバーレイ（VPN→疎通→ssh 段階表示, モック10）。
- Ctrl全種のための key_item 入力経路（届くか要実機確認→届けば実装）。

## 参考
- 実装プラン: `~/.claude/plans/cardputerzero-vpn-delightful-adleman.md`
- 画面仕様/モック: `docs/SCREENS.md` / `docs/mockups/`
- 全体: `README.md`
