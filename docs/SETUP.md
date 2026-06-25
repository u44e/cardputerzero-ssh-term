# ssh_term 実機セットアップ手順書（M5CardputerZero）

Mac でビルドして **M5CardputerZero**（AArch64 / Raspberry Pi OS）へ導入し、
日本語入力（OS IME）まで使えるようにする手順。所要 30〜60 分（初回のエミュビルドを除く）。

> 検証用に Pi Zero 2W 代替機を使う場合も同じ手順（`--host` を代替機のIPに）。

---

## 0. 必要なもの
- **M5CardputerZero 実機**（Raspberry Pi OS / AArch64 が起動済み、Wi‑Fi 接続済み、SSH 有効）
- **Mac**（Apple Silicon）: Homebrew、Apple `container`、`~/cardputer-zero/emulator`（cardputer‑emu）、`~/cardputer-zero/czpi`
- 同一 LAN（Mac と実機が相互に到達可能）

---

## 1. Mac 側の準備（初回のみ）
```sh
brew install libvterm            # ターミナルコア
# Apple container が動くこと（czpi が使用）
container system start
```
- リポジトリ取得（未取得なら）:
  ```sh
  git clone git@github.com:u44e/cardputerzero-ssh-term.git ~/Projects/cardputerzero-ssh-term
  ```

---

## 2. 実機へ SSH 鍵を通す（パスワード入力を省く）
```sh
ssh-copy-id pi@<実機IP>          # 以降パスワード不要
ssh pi@<実機IP> 'uname -srm'     # 接続確認（aarch64 GNU/Linux が出ればOK）
```

---

## 3. 実機ランタイム依存の導入（実機側、1回だけ）
`deploy-run.sh` が `libvterm0` まで自動投入。残りは手動で：
```sh
ssh pi@<実機IP> 'sudo apt-get update && sudo apt-get install -y \
  openssh-client telnet libvterm0'
# 任意（使う場合）:
#   VPN:        sudo apt-get install -y wireguard-tools openvpn strongswan xl2tpd
#   日本語入力: sudo apt-get install -y fcitx5 fcitx5-mozc
```
- **フォント**: 端末用 Liberation Mono と日本語 Alibaba PuHui Ti は APPLaunch 導入で
  `/usr/share/APPLaunch/share/font/` に常駐（同梱・再配布なし）。無ければ APPLaunch を導入。

---

## 4. ビルド & デプロイ（Mac 側）
```sh
# arm64 .so を生成（初回は cardputer-emu もビルド＝数分。以降キャッシュ）
~/cardputer-zero/czpi/build.sh ~/Projects/cardputerzero-ssh-term
#  -> ~/cardputer-zero/czpi/out/libssh_term.so（ELF aarch64）

# 実機へ転送して labwc ウィンドウで起動
~/cardputer-zero/czpi/deploy-run.sh ssh_term --host pi@<実機IP>
```
画面に **接続先一覧（Sessions）** が出れば成功。

---

## 5. 日本語入力（OS IME）の有効化 ※日本語を打つ場合
他アプリと共通の **fcitx5‑mozc** を使います（アプリ内IMEは無し）。
```sh
ssh pi@<実機IP> 'sudo apt-get install -y fcitx5 fcitx5-mozc fcitx5-config-qt'
```
Wayland セッションで IME を有効化（`~/.config/environment.d/im.conf` 等）:
```
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
```
- 再ログイン後、**Ctrl+Space**（既定）で日本語入力 ON/OFF。確定文字がそのまま端末へ入ります。
- UI 表示言語（メニューの英/日）は別物：一覧で `g` キー。

---

## 6. 初回の設定
- 接続先プロファイルは初回起動時に既定値がシードされ、`/sdcard/term.conf` に保存。
- 一覧で `n`（新規）/ `e`（編集）→ ホスト・ユーザ・proto（ssh/telnet/shell）・VPN方式・ログ・文字サイズを設定 → `s` 保存。
- ログは `/sdcard/logs/`、流し込み元は `$HOME`（既定）。

---

## 7. 動作確認（最低限）
1. `local shell` を `Enter` で接続 → `ls` 等が動く（色付き）。
2. `ssh` プロファイルで LAN 機器へ接続 → `vim`/`htop` が色付きで動く。
3. SIDE キーで Session Menu → Send file でファイル流し込み（文字コード自動判定）。
4. （IME導入後）Ctrl+Space で日本語入力。

詳しい検証項目は `docs/DEVICE_CHECKLIST.md`、操作は `docs/MANUAL.md`。

---

## 8. トラブルシュート
| 症状 | 対処 |
|------|------|
| 起動しない / 黒画面 | `libvterm0` 未導入の可能性 → 手順3。`deploy-run.sh` のログを確認 |
| 文字が豆腐(□) | フォント未常駐 → APPLaunch 導入、または `/usr/share/APPLaunch/share/font/` を確認 |
| Tab/矢印が効かない | 端末はフルキー前提。ホストのキーマップを確認（`07-input` 参照） |
| Ctrl-C 等が効かない | 実機は key_item 修飾で対応。emu では一部のみ |
| VPN が張れない | `pkexec`/polkit エージェント、各VPNツールの導入と権限を確認 |
| 日本語が打てない | fcitx5 が起動・有効か（`GTK_IM_MODULE=fcitx` 等）、Ctrl+Space を確認 |

---

## 付録: 更新（再デプロイ）
コード更新後は手順4を再実行するだけ（cardputer‑emu はキャッシュ、`.so` のみ再ビルド）。
