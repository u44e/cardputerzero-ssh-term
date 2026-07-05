# CardputerZero AppStore 提出メモ / submission notes

NetTerm（`ssh_term`）を公式 CardputerZero AppStore へ出す場合の**現状と要件**。
公式ドキュメント: <https://cardputerzero.github.io/#/documents>
（`developer-docs/dev/{publish,packaging,applauncher,lvgl,desktop-spec}.md`）

## ⚠️ 重要：モデルが合っていない（要移植）
公式 AppStore のアプリは **fork/exec される標準バイナリ**である。
- `APPLauncher` はアプリ選択時に**子プロセスを fork/exec** し、子が **framebuffer を占有**する
  （`applauncher.md`：hal_process = fork/exec、"Child app has exclusive framebuffer access"）。
- LVGL アプリは **`main()` を持ち**、`lv_linux_fbdev_create()`＋`lv_evdev_create()` で
  `/dev/fb0`・evdev を**自前で**開く（`lvgl.md`）。`.desktop` は `Exec=<絶対パスのバイナリ>`（`desktop-spec.md`）。
- 実在アプリで確認：2048 の SConstruct target は `Program`（`M5CardputerZero-2048`）、Calendar も
  `runtime: legacy-deb-only` の標準バイナリ。**dlopen アプリは存在しない。**

一方 **NetTerm は lvgl-dlopen（`libssh_term.so`、`app_main`/`app_event`/`ui_init` を
ホスト launcher が dlopen）**。`app-builder.json` の `runtime: "lvgl-dlopen"` がそれ。
→ **そのままでは公式 AppStore の形式ではない。** 提出には標準バイナリへの**移植**が要る。

### 移植でやること（概算）
1. `main()` を新設し、`lv_init()` → `lv_linux_fbdev_create()`（`APPLAUNCH_LINUX_FBDEV_DEVICE`）
   ＋ `lv_evdev_create()` で表示/入力を自前化。
2. 現在の `app_main(parent)` を `main()` から呼ぶ（`parent = lv_screen_active()`）。
   `app_event(CZ_EV_*)` 相当は、SIGTERM（ESC 長押しで launcher が送る）でクリーン終了に置換
   （HOMEボタン/終了時の PTY/ログ/VPN 後始末を SIGTERM ハンドラへ）。
3. ビルドを CMake（FetchContent で LVGL 9.x）＋ `packaging/{meta.env,build.sh,stage.sh}` に対応、
   `bin_name` を実行バイナリ（例 `M5CardputerZero-NetTerm`）に。
4. Docker: `docker run --rm --network=host ... ghcr.io/cardputerzero/build-env:latest scripts/pack-deb.sh <app>`。

> 現行の emulator/czpi（dlopen）ワークフローは**別 launcher 向け**。公式 AppStore とは別系統。

## 提出フロー（移植後）
```
czdev login → .deb をビルド（pack-deb.sh）→ czdev publish --deb <file>
   → CardputerZero/packages へ PR 自動作成 → CI 検証 → レビュー → マージで公開
```
承認後、4文字 **share code**（ホーム画面 `S`→コード）。`czdev login`/`czdev publish` は
**本人の GitHub OAuth が必要**なのでアシスタントでは実行しない。

## メタデータ（形式は確認済み・準備済み）
公式は**別ファイル `meta.json` ではなく `app-builder.json` の `"store"` ブロック**（2048 で確認）。
本リポジトリの `app-builder.json` に追加済み：`store{summary,categories,license,source_repo,icon,screenshots,permissions}`。
- カテゴリは公式一覧（Games/Utilities/Communication/AI/Media/Education/Development/System）から採用。
- `permissions`：`keyboard_input`/`network`/`filesystem:"full"`/`external_hardware`。

## 形式に依存せず用意済みのアセット
| 準備物 | 実体 | 要件 |
|---|---|---|
| アイコン | `share/images/ssh_term.png` | **100×100 PNG**（`icon.png` 128×128 を縮小） |
| スクショ ×5 | `store/screenshots/0[1-5]-*-320x170.png` | **320×170 PNG** |

## レビュー前に判断が要る点
1. **移植の要否**：公式 AppStore へ出すなら上記の標準バイナリ移植が前提。dlopen のまま使う別 launcher 環境が
   本命なら、公式 AppStore 提出自体が不要かもしれない（要方針決定）。
2. **source_repo が private**（`u44e/...`）。掲載は source_repo を指す。**公開** or **CardputerZero org へ移管**を検討。
3. **VPN は `pkexec` で特権ツール（wg-quick/nmcli 等）を起動**。ガイドの「APPLaunch 外の system files を
   変更しない」に照らし審査コメントで用途明記（秘密情報はアプリ非保存＝OS側）。
4. **終了**：ESC は端末中 PTY へ素通し。アプリ終了は launcher の SIGTERM で後始末（移植時に実装）。

## 訂正履歴
初版で「AppBuilder の `pack_deb.py` が dlopen `.so` をそのまま .deb 化＝作り替え不要」と記したのは**誤り**。
その `pack_deb.py` は旧世代の別スクリプトで、生成 `.desktop` は `Exec=バイナリ`＋systemd 起動＝**実行バイナリ前提**。
公式 AppStore は fork/exec 標準バイナリのため、上記のとおり**移植が必要**。
