# ssh_term 機能一覧

CardputerZero（Raspberry Pi CM0 / Linux / LVGL 9.5, 320×170）向け SSH/telnet/shell ターミナル。
凡例: ✅ 実装・emで検証済 / ◐ 一部（実機で全対応）/ ⏳ 実機でのみ動作・未検証 / ➖ 仕様上あえて

## ターミナルコア
| 機能 | 状態 |
|------|:--:|
| **libvterm(MIT)** によるVT100/ANSI完全エミュレーション（カーソル/消去/スクロール領域/挿入削除/alt-screen/DECモード/DA・DSR応答/アプリカーソルキー） | ✅ |
| **自前 forkpty** によるPTY子プロセス管理（著作権クリーン、launcherコード非流用） | ✅ |
| **SGRカラー**（8/16/256色・truecolor、色ラン毎ラベル描画、既定は緑/黒＝ネイティブ忠実） | ✅ |
| 等幅フォント = **Liberation Mono 12px → 45桁×12行**（実機常駐をfreetype読込、unscii_8フォールバック） | ✅ |
| ブロックカーソル、readerスレッド＋40ms描画タイマ＋dirty差分 | ✅ |
| 日本語/CJK表示（実機=Alibaba PuHui Ti、emはCJKフォント無くtofu） | ◐ |

## 接続・セッション
| 機能 | 状態 |
|------|:--:|
| プロトコル選択：**ssh / telnet / shell（ローカル端末）** | ✅ |
| **接続先プロファイル**（名前/ホスト/ポート/ユーザ/proto/VPN種別/VPN設定/ログ/サイズ） | ✅ |
| 永続化：フラット key=value（`/sdcard/term.conf`、env `TERM_CONF`、初回シード） | ✅ |
| 一覧（選択→接続）／**編集画面 CRUD**（新規`n`/編集`e`/削除`d`＋確認ダイアログ）／インライン文字入力 | ✅ |
| セッション終了で一覧へ自動復帰（ウォッチドッグ） | ✅ |
| 実SSH/telnet接続（argv生成済、実ホスト未検証） | ⏳ |

## フォント・表示
| 機能 | 状態 |
|------|:--:|
| **フォントサイズ可変 12/16/20px**：プロファイル毎＋実行中変更（メニュー「Font size」→`term_resize`で桁行＋winsize再計算） | ✅ |
| 1行ステータスバー（名前 / CONNECTED / VPN / REC / SIDE） | ✅ |
| 画面遷移・各画面のキーガイド | ✅ |

## キーボード入力
| 機能 | 状態 |
|------|:--:|
| フルキー受信（`NO_KBD_STUBS`、単一objグループで **Tab/矢印もハンドラに到達**） | ✅ |
| キー→PTYバイト変換：印字/Unicode、矢印(CSI)、Enter/BS/ESC/Tab/Home/End/Del | ✅ |
| ESCはPTYへ素通し（vim等）、SIDEキーで Session Menu | ✅ |
| Ctrl系：emは制御文字一部、実機は `LV_EVENT_KEYBOARD`(key_item)修飾で全対応 | ◐ |

## 日本語入力（IME）
| 機能 | 状態 |
|------|:--:|
| **OS IMEに委譲**（fcitx5-mozc / macOS）：`SDL_TEXTINPUT`→確定かな漢字がそのままPTYへ（変換コード不要） | ✅(配線) / ⏳(実機mozc) |
| アプリ内 **ローマ字→ひらがな** フォールバック（`` ` ``トグル、preedit＋確定、OS IME無し環境用） | ✅ |

## ログ
| 機能 | 状態 |
|------|:--:|
| **生セッションログ tee**（`/sdcard/logs/<名前>-<時刻>.log`、env `TERM_LOGDIR`） | ✅ |
| ログ一覧（mtime降順）＋閲覧（**ANSI除去**・スクロール）・削除 | ✅ |
| 実行中ログON/OFF（メニュー Toggle log） | ✅ |

## 設定ファイル流し込み（config injection）
| 機能 | 状態 |
|------|:--:|
| **文字コード自動判定**（内蔵ヒューリスティック：UTF-8 / Shift_JIS / EUC-JP / ascii） | ✅ |
| `iconv` で **UTF-8 へ変換**して送出 | ✅ |
| 行ごと **ペース送出**（低速CLI/機器向け） | ✅ |
| ファイルブラウザ → **送出ダイアログ**（検出エンコーディング表示）→ 送出 | ✅ |

## VPN
| 機能 | 状態 |
|------|:--:|
| **VPN方式選択（iPhone風）**：none / WireGuard / OpenVPN / IKEv2 / L2TP / Tailscale | ✅(UI) |
| 種別ごとの起動/停止：`pkexec` で wg-quick / openvpn / strongSwan ipsec / xl2tpd / tailscale | ⏳(実機exec) |
| 疎通プローブ（`getifaddrs` で wg/tun/utun 検出） | ✅ |
| 接続前にVPNゲート → 失敗時「Connect anyway / Cancel」ダイアログ／終了時 down（自分で上げた時のみ） | ✅(UI) |

## 画面（全10種）
接続先一覧 / プロファイル編集 / ライブ端末 / Session Menu(オーバーレイ) / ファイルブラウザ /
送出ダイアログ / ログ一覧 / ログ閲覧 / 確認ダイアログ / IME preedit。モック=`docs/SCREENS.md`・`docs/mockups/`。

## ビルド・プラットフォーム
| 項目 | 状態 |
|------|:--:|
| **macOSエミュレータ**（`./build-emu.sh --run`、cardputer-emu、ヘッドレス `EMU_SHOT`） | ✅ |
| **arm64 Pi Zero 2W**（`czpi/build.sh`→ELF aarch64 `.so`、`deploy-run.sh`） | ✅(ビルド) / ⏳(実機) |
| lvgl-dlopen アプリ（`cz_app.h` ABI：app_main/app_event/ui_init） | ✅ |

## 設計・ライセンス
- **launcherコードは非流用**（無許諾のため機能のみ参照）。libvterm=MIT、フォントは実機常駐を読むだけ（再配布なし）。
- 約1,750行 / 8モジュール：`main / pty / term / config / logsink / sendfile / vpn / ime`。
- GitHub: `github.com/u44e/cardputerzero-ssh-term`（private、`v0.0.1`）。

## 残（実機/低優先）
実機デプロイ＋対話打鍵検証、fcitx5-mozc漢字、VPN実起動、Ctrl全種、接続中オーバーレイ（モック10、未実装）、
端末SGRの背景色/反転（前景色のみ実装）。詳細は `docs/DEVICE_CHECKLIST.md`。
