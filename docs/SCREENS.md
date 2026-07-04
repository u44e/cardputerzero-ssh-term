# CardputerZero SSH/Telnet ターミナル — 画面案

対象: 320×170 横 / LVGL 9.5。PNGモックは `docs/mockups/`（`*.png`=実寸 / `*@4x.png`=拡大）、
生成は `docs/gen_mockups.py`（`python3 docs/gen_mockups.py`）。

## フォント方針（実機の実フォントに準拠）
実機LVGLが標準で持つのは **Montserrat 8–48（プロポーショナル・Latinのみ）** と
**unscii_8 / unscii_16（唯一の等幅・ASCIIのみ）** だけ。日本語・罫線は標準に無く、
**TTF同梱→tiny_ttf/FreeTypeで実行時ロード**（replayerのBIZUD方式）で足す。

| 用途 | フォント | 備考 |
|------|---------|------|
| UI（タイトル/一覧/メニュー/キーガイド/ステータス） | **Montserrat**（実機 montserrat_10/12/14） | プロポーショナル。**等幅はUIに使わない**（指示） |
| UIの日本語 | **BIZUD UDGothic**（同梱、replayer踏襲） | プロファイル名等の日本語 |
| ターミナル本文（ASCII） | **Liberation Mono 12px**（実機常駐、freetype） | **45桁 × 12行**（CHAR_W=7/CHAR_H=12）。等幅はここだけ。モックはMenloで代用 |
| ターミナル本文（日本語/CJK） | **Alibaba PuHui Ti 3**（実機常駐、freetype） | 既存CLIと同じ。同梱不要。モックはBIZUDで代用 |

- **ターミナル/メニュー/ログ閲覧の背景は黒(#000000)**（指示）。UIチロームは紺 `#1A1A2E`。
- 罫線文字（vim等）: unsciiに無いためASCII近似、またはMisaki/罫線追加フォントで対応（実装時）。

配色: タイトル `#3AD8FF`(cyan) / 選択帯 `#2C2C52` / 接続 `#4CD96A`(green) / REC・検出 `#FFB82E`(amber) /
未確定 amber下線 / 注意 `#FF6B6B`(red)。

## 画面遷移
```
 SCR_PROFILES ──Enter──▶ SCR_TERM ──SIDE──▶ [Session Menu]
   │  ▲ │ l                 │ ▲                  ├ Detach / Close
   e/n│ │ ESC/s         (IME)│ │(候補)            ├ Send file ─▶ SCR_SENDFILE
   ▼  │ ▼                    ▼ │                  └ Toggle log
 SCR_EDITOR              [候補バー]          SCR_LOGS ─Enter▶ [Log Viewer]
```
グローバルキー: `↑↓`移動 / `Enter`決定 / `ESC`戻る（ターミナル中はPTYへ素通し）/
**SIDEキー長押し**＝Session Menu（エミュ代替 `Fn+Q`）。
※ 図中の `(IME)`/`(候補)`/`[候補バー]` は旧・自前IME案の名残。現在は **OS IME 委譲**で該当画面は無い（下記画面一覧#6・実装メモ参照）。

## 画面一覧（モック対応）
1. **SCR_PROFILES** `1_profiles` — 接続先一覧。名前+proto+`user@host:port`+`V`(VPN)/`L`(Log)。日本語名可。
   `↑↓`選択 `Enter`接続 `e`編集 `n`新規 `d`削除 `l`ログ。
2. **SCR_EDITOR** `2_editor` — Name/Host/Port/User/Proto/VPN/Log/**Size(端末px)**/**UI(px)**。テキストはEnterで入力、選択は`←→`、`s`保存。
3. **SCR_TERM** `3_term` — Liberation Mono 45×12端末（日本語はAlibaba PuHui Ti）。下に1行ステータス（接続/VPN/REC/SIDE）。全キーPTYへ。
4. **Session Menu** `4_menu` — SIDEで開く。Detach/**Send file(config)**/**Font size**/Toggle log/Close/Back。表示中のみESCで閉じる。
5. **SCR_LOGS** `5_logs` / **Log Viewer** `6_logview` — 一覧(mtime降順)→Enterで閲覧（unscii_8・ANSI除去・スクロール）。
6. ~~**IME（日本語入力）** `7_ime`~~ — **廃案**。自前候補バー方式はやめ、**OS IME（fcitx5-mozc）に委譲**（他アプリと同様）。
   確定文字は `SDL_TEXTINPUT` 経由でそのまま PTY へ送出、候補窓は fcitx5 が表示。モック `7_ime` は旧案の記録。
7. **SCR_SENDFILE（設定流し込み）** `8_sendfile` — ファイル選択→**文字コード自動認識**（例 Shift_JIS auto 98%）→
   `Send as`（UTF-8/SJIS/EUC/raw）+ `Pace`（ms/line・wait-prompt）→ 進捗(24/118) で PTY へ流し込み。
8. **SCR_FILES（ファイルブラウザ）** `9_files` — Send file の選択元。`[D]`=ディレクトリ、サイズ表示、日本語名可。
   `↑↓`選択 `Enter`開く/選択 `/`フィルタ `ESC`戻る。→ 選択後 SCR_SENDFILE へ。
9. **Connecting（接続中オーバーレイ）** `10_connecting` — VPN起動→疎通プローブ→ssh を段階表示
   （done=緑 / active=cyan / pending=灰）。`ESC`中止。VPN無しプロファイルはVPN段を省略。
10. **Dialog（確認/エラー）** `11_dialog` — 例: VPN失敗→`Connect anyway`/`Cancel`（赤枠）。削除確認等も共用。`←→`選択 `Enter`決定。
11. **SCR_TERM 切断状態** `12_term_disc` — セッション終了時。`● DISCONNECTED`(赤)、`Enter`閉じる / `r`再接続。

## 新規要件の実装メモ（決定済み）
- **ターミナル = Liberation Mono 45×12（確定）**: 実機常駐 `/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf` を
  freetype読込（既存CLIコンソールと同一）。CJK=Alibaba PuHui Ti（同常駐）。再配布なし＝権利問題なし。PTY winsizeは45×12。
- **フォントサイズ可変（確定）**: 端末=Liberation Mono **12px(45×12) / 16px(34×9目安)**、
  UI=Montserrat **10/12/14px**。設定はプロファイル毎（編集画面 Size/UI）＋実行中に
  **Fn+= / Fn+-** で即ズーム（Session Menu「Font size」からも）。サイズ変更時は自前 `pty_resize()`(TIOCSWINSZ+SIGWINCH) で再通知。
- **日本語入力 = OS IME（fcitx5-mozc）委譲（確定・改定）**: 当初の mozc 直接駆動
  （`mozc_emacs_helper` + 自前候補バー、モック `7_ime`）は**廃案**。他アプリと同様に OS の IME に一本化し、
  確定したかな漢字を `SDL_TEXTINPUT` 経由でそのまま PTY へ送る。変換・候補窓は fcitx5 が表示（アプリ内IMEは持たない）。
  ON/OFF はシステムのIMEトグル（既定 `Ctrl+Space`）。Pi側ランタイムに `fcitx5-mozc` と
  Wayland/labwc でのIME有効化（`GTK_IM_MODULE=fcitx` 等）が必要。
- **設定流し込み = 自動判定→UTF-8（確定既定）**: ローカルファイルをPTYへ送出。
  **文字コード自動認識**=`uchardet`(libuchardet)または`nkf --guess` → 既定でUTF-8へ`iconv`変換して送出
  （`Send as`でSJIS/EUC/rawに変更可）。行ごとにペース送出（`wait-prompt`で機器エコー待ち可）。網機器config流し込み用途。
