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

### 進捗サマリ（`port/` — 実機なしで arm64 `.deb` までビルド済み）
`./port/build.sh`（arm64 Debian Bookworm コンテナ、Apple Silicon でネイティブ）で以下を確認済み：
- **検証ビルド**（オフスクリーンmemory表示）：arm64 ELF が **headless 起動→config シード→SIGTERM 終了**＝アプリが arm64 で動作。
- **実機ビルド**（`-DPORT_FBDEV`：`lv_linux_fbdev`＋`lv_evdev`）：arm64 ELF がリンク成立。
- **`.deb`**（CPack）：`port/dist/netterm_0.2.2-m5stack1_arm64.deb`。APPLaunch レイアウト
  （`bin/netterm`＋`applications/netterm.desktop`＋`share/images/netterm.png`）、Maintainer=`u44e@users.noreply.github.com`、
  依存 `libvterm0/openssh-client/telnet/picocom` 等。→ **前述 CI の機械的関門を満たす**。
- **③ evdev キーボードのタグ再現：実装済（`port/evdev_kbd.c`）** — Ctrl=`0x40…`/Alt=`0x20…`/F系=`0x10…`/Meta を
  **標準 Linux キーコード**から再構成する LVGL keypad indev。修飾キー追跡＋US base/shift 配列。fbdev 版に組込・ビルド確認済み。
  **実機で確定が要る残り**：CardputerZero の **Fn/記号レイヤ**の配列（US と異なる）と **SIDEキーの keycode**
  （`-DAPP_SIDE_KEYCODE=<n>`、暫定 `KEY_MENU`）。これらは実機の keymap を見て埋める。

### 移植フェーズ（詳細）
- **① 標準バイナリ化（済・検証済）** — `port/main.c`。`main()` が LVGL の display+input を持ち、既存の
  `app_main(lv_screen_active())` をそのまま呼ぶ（src/*.c は無改変）。launcher の **SIGTERM**（Home/長ESC）で
  `app_event(CZ_EV_EXIT_REQUEST)` を呼びクリーン終了。arm64 で headless 起動→config シード→SIGTERM 終了を確認。
  （初期の macOS-SDL 版 proof は `port/` に統合。窓付きプレビューは `./build-emu.sh --run`＝emulator を使用。）
- **② 端末表示/入力（実機必須）** — SDL を `lv_linux_fbdev_create()`（`APPLAUNCH_LINUX_FBDEV_DEVICE`／`/dev/fb0`）
  ＋ `lv_evdev_create()` に差し替え。Template `src/platform/linux_input.*` が雛形。
- **③ evdev キーボードのタグ再現（済＝`port/evdev_kbd.c`／実機で keymap 確定）** — Ctrl/Alt/Fn/F系/Meta を
  標準 Linux キーコードから再構成する LVGL keypad indev を実装（修飾追跡＋US base/shift）。SIDEキーは
  `app_event(CZ_EV_SIDE_KEY)` へ（`APP_SIDE_KEYCODE`）。**残り：CardputerZero の Fn/記号レイヤ配列と SIDE keycode を実機で確定。**
- **④ CMake＋パッケージ（Template 準拠）** — `CMakePresets.json` の `cp0-cross`（aarch64＋BSP sysroot 自動DL）で
  FetchContent LVGL 9.5＋freetype/png/jpeg/zlib、`vterm`/`iconv` を追加リンク。`cmake/cm0-package.cmake` 方式の
  **CPack DEB**（`/usr/bin/<bin>`・`/usr/share/<app>/`・`/usr/share/APPLaunch/applications/<app>.desktop`・
  `/usr/share/APPLaunch/share/images/`）で `dist/*_arm64.deb`。macOS は `darwin-arm64` preset で SDL プレビュー可。

> ②③④は **実機（M5CardputerZero）での検証が前提**。現状 device が無いため、フォント（`/usr/share/APPLaunch/share/font/*`
> の常駐可否）・evdev キー対応・fbdev 表示・ESC終了は**実機到着後に確定**。現行の emulator/czpi（dlopen）系は別 launcher 向け。

## 提出フロー（移植後）
```
czdev login → .deb をビルド（pack-deb.sh）→ czdev publish --deb <file>
   → CardputerZero/packages へ PR 自動作成 → CI 検証 → レビュー → マージで公開
```
承認後、4文字 **share code**（ホーム画面 `S`→コード）。`czdev login`/`czdev publish` は
**本人の GitHub OAuth が必要**なのでアシスタントでは実行しない。

## メタデータ（レジストリ・スキーマに準拠して記述済み）
公式は**別 `meta.json` ではなく `app-builder.json` の `"store"` ブロック**（2048・skill-ai-coding-guide で確認）。
`appstore-registry-requirements.md` のスキーマに合わせて記述済み：
- **`uuid`**：`76048bc8-5565-4872-9855-51ffba65f160`（**share code＝先頭4桁 `7604`**。ホーム画面 `S`→`7604` で導入）。
- `title`/`summary`/`description`/`locales`(en,ja)。カテゴリは**レジストリ分類**（Games/Media/Music/Productivity/
  **Developer Tools**/**Network**/Communication/Hardware/Education/Utilities/System/AI/Experimental）から
  `["Network","Developer Tools","Utilities","System"]`。
- `author.github`=`u44e`、`license`=MIT、`commercial_use`=true。
- **`source.openness`**：現在 **`closed-source`**（repo が private のため）。公開すれば `open-source`。**要判断（下記）**。
- `permissions`（全次元を明示）＋ `privacy`（収集なし・端末外送信は接続先のみ・ローカル保存・第三者共有なし）＋
  `external_hardware`(USB-serial)＋`service`=false＋`hdmi_output`=false＋`risk_flags`（pkexec VPN / USB-serial / ssh egress）。
- `download.{package,url,md5}`：**url/md5 は publish 時に確定**（`.deb` の MD5）。

## 準拠チェックリスト（submission-policy / user-agreement）
- **本人性**：GitHub ID `u44e` で提出（traceable identity・必須）。
- **配布権**：本体は MIT、libvterm=MIT（リンク）、フォントは**同梱せず**実機常駐を読むだけ＝再配布問題なし。
- **メタ真実性**：network/filesystem/external_hardware/service を正確申告（隠蔽・誇大禁止）。
- **プライバシー**：接続プロファイル（ホスト/ユーザ/鍵パス。**パスワード・VPN秘密は非保存**）とログを `/sdcard` にローカル保存、
  端末外へはユーザが選んだ接続先へのみ、テレメトリ無し。→ `privacy` ブロックに明記済み。
- **デバイス安全**：VPN は `pkexec` で特権ツール起動、USB-serial は外部HW。→ `risk_flags` に明記、審査コメントでも用途説明。
- **UX/退出**：320×170 準拠。**短ESC=戻る/長ESC・Home=終了**が公式作法。本アプリの端末では ESC を PTY へ素通し（vim等）
  するため、退出は Home/SIGTERM（`app_event(CZ_EV_EXIT_REQUEST)` で後始末）に依存 → **移植②③で長ESC/Home 終了を実装・実機確認**。
- **起動即クラッシュ不可／実験扱いは明示**：該当なし。

## デバイス実装の要件（application-development-guide / skill-ai-coding-guide）
- `.desktop`：`Exec=bin/netterm`（`/usr/share/APPLaunch` 相対）・`Terminal=false`・`Icon=share/images/netterm.png`。
  環境変数が要るならラッパースクリプト（複雑な `Exec` は禁止）。
- **framebuffer**：`LV_LINUX_FBDEV_DEVICE` を尊重。**`/dev/fb0` 決め打ち禁止**（LCD は `/dev/fb1` の場合あり）。
- **CJKフォント**：日本語表示に CJK 対応フォント（実機常駐の AlibabaPuHuiTi を利用）。
- **czdev**：AppBuilder から `cargo build --release -p czdev` → `czdev doctor` / `czdev run|watch`（SDLデバッグ）/
  `czdev deploy --host pi@<ip> --deb <file>` / `czdev login` / `czdev publish --deb <file>`。

## 提出ゲートの実際（`CardputerZero/packages` の `validate-pr.yml` を確認）
機械的に強制されるのは **`.deb` の構造チェックのみ**（PR CI）:
1. 生`.deb`をgit commitしない（Release manifest方式）／2. manifestに`url`/`sha256`/`filename`／
3. URLからDL可＋**sha256一致**／4. 正しい`.deb`／5. ディレクトリ名=Package名／6. **`.desktop`を含む**／
7. パッケージ名正規表現／8. ファイル名=`<pkg>_<ver>_<arch>.deb`／9. **`.deb`のMaintainer email = PR作成者**
（`<id>@users.noreply.github.com` か検証済メール）／10. バージョンが既存より新しい。

- **実機テストは無い**（GitHub Actions は arm64 GUI を実機で動かせない）。SDL smoke test も要件文書には
  あるが**実CIには未実装**。store の uuid/スクショ/権限/プライバシーもこの関門では未検証。
- したがって **物理デバイス無しでも提出PRは通せる。** 本当に要るのは **ビルド済み arm64 `.deb`**
  （＝移植②③④＋クロスビルド。**デバイス自体は不要**）。
- ただし実機動作確認は**推奨（開発者セルフチェック）＋人間のメンテナ審査が要求しうる**もの。本アプリは
  **closed-source＋network＋pkexec＋USB**で審査が厳しめ（policy）なので、未検証提出は `needs-changes`/`rejected`
  の**審査リスク**が高い（機械的ゲートではない）。
- **Maintainer email**：`.deb` の control の Maintainer を PR作成者（`u44e@users.noreply.github.com` 等）に合わせる必要。

**結論**：提出に不可欠なのは *arm64 `.deb` のビルド*（移植②③④）。実機は「機械的必須」ではないが「審査を通すため実質推奨」。
順序：移植②③④ →（可能なら実機/エミュ検証）→ `czdev publish`。

## 形式に依存せず用意済みのアセット
| 準備物 | 実体 | 要件 |
|---|---|---|
| アイコン | `share/images/netterm.png` | **100×100 PNG**（`icon.png` 128×128 を縮小） |
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
