# CardputerZero AppStore 提出メモ / submission notes

NetTerm（`ssh_term`）を CardputerZero AppStore へ出すための準備状況と手順。
公式ガイド: <https://cardputerzero.github.io/#/documents/app-submission-guide>
（`developer-docs/dev/publish.md` / `packaging.md`）

## 提出フロー（公式）
```
czdev login → .deb をビルド → czdev publish --deb <file>
   → CardputerZero/packages へ PR 自動作成 → CI 検証 → メンテナがレビュー → マージで公開
```
`czdev`（[CardputerZero-AppBuilder](https://github.com/CardputerZero/CardputerZero-AppBuilder)）が fork/branch/upload/PR を自動化する。**手動 git 操作は不要。**
承認後、4文字の **share code** が付与され、ユーザーはホーム画面で `S` → コード入力で導入できる。

## モデルの整合性（確認済み）
NetTerm は lvgl-dlopen（`libssh_term.so`、`cz_add_lvgl_app`）だが、AppBuilder の
`scripts/pack_deb.py` が `.so` + フォント + 画像 + `.desktop` + 権限を APPLaunch 配下へ配置して
`.deb` 化する。**標準 `main()` バイナリへ作り替える必要はない。**

## 本リポジトリに準備済み
| 項目 | 実体 | 要件 |
|---|---|---|
| ストア用メタデータ | `meta.json` | title/summary/description/categories/license/source_repo/icon/screenshots/permissions |
| アイコン | `store/ssh_term.png` | **100×100 PNG**（`icon.png` 128×128 を縮小） |
| スクショ ×5 | `store/screenshots/0[1-5]-*-320x170.png` | **320×170 PNG**（一覧/編集/端末/メニュー/マクロ） |
| ライセンス | MIT（README のライセンス方針） | 原作／適切なライセンス |
| 320×170 動作 | ✅ | 必須 |
| パッケージサイズ | `.so` 数百 KB ≪ 100 MB | < 100 MB |

## ビルド（`.deb`）
```sh
# AppBuilder のコンテナで pack（公式フロー）
cd ~/Projects/CardputerZero-AppBuilder
docker run --rm -v $(pwd):/src -w /src \
  ghcr.io/cardputerzero/build-env:latest \
  scripts/pack-deb.sh <this-app-path>
#  -> dist/ssh_term_0.2.1-m5stack1_arm64.deb
```
`app-builder.json`（`package_name=ssh_term` / `app_name=NetTerm` / `runtime=lvgl-dlopen` /
`caps=[keyboard,network,pty,process,filesystem]`）がパッケージ名・表示名・権限の元になる。

## 公開（ユーザーが実行）
> `czdev login`（GitHub OAuth）と `czdev publish` は**本人の GitHub 認証が必要**なので、こちら（アシスタント）では実行しない。
```sh
czdev login
czdev publish --deb dist/ssh_term_0.2.1-m5stack1_arm64.deb
```

## レビュー前に判断が要る点
1. **source_repo が private**（`github.com/u44e/cardputerzero-ssh-term`）。
   AppStore の掲載は source_repo を指す。**公開**するか **CardputerZero org へ移管**するかを決める
   （既存アプリは `github.com/CardputerZero/<App>`）。private のままだと審査で弾かれる可能性。
2. **権限の妥当性**（審査で説明できるように）：
   - `network` … ssh/telnet、VPN。
   - `filesystem: full` … `/sdcard/term.conf`・`/sdcard/logs`、流し込み元/SSH鍵など任意ファイル読取。
   - `external_hardware` … USB-シリアル（`/dev/ttyUSB*`）。
   - **VPN は `pkexec` で特権ツール（wg-quick/nmcli 等）を起動**する。ガイドの
     「system files を APPLaunch 外で変更しない」に照らし、審査コメントで用途を明記。
     VPN秘密情報はアプリに保存せず OS 側が保持（`docs/FEATURES.md` 参照）。
3. **ESC の扱い**：端末中は ESC を PTY へ素通し（vim 等）。アプリ終了は HOME ボタン
   （`CZ_EV_EXIT_REQUEST` で PTY/ログ/VPN を後始末）。ガイドの「ESC で綺麗に終了」は
   HOME 経由の後始末で満たすが、必要なら審査コメントで補足。
4. **カテゴリ**：ガイドの一覧（Games/Utilities/Communication/AI/Media/Education/Development/System）から
   Utilities/Development/Communication を採用。

## meta.json フィールド（採用値）
`docs/FEATURES.md` の機能一覧が description の根拠。categories/permissions は上記の通り。
更新時は `app-builder.json` の `version` と歩調を合わせ、`czdev publish` で版上げ PR を作る。
