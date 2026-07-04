# NetTerm (ssh_term) 機能一覧

**M5CardputerZero**（AArch64 Linux / Raspberry Pi OS / LVGL 9.5, 320×170）向け SSH/telnet/serial/shell ターミナル。
表示名は **NetTerm**（SSHは4トランスポートの1つのため）。パッケージ/バイナリIDは `ssh_term` のまま。
凡例: ✅ 実装・emで検証済 / ◐ 一部（実機で全対応）/ ⏳ 実機でのみ動作・未検証 / ➖ 仕様上あえて

## ターミナルコア
| 機能 | 状態 |
|------|:--:|
| **libvterm(MIT)** によるVT100/ANSI完全エミュレーション（カーソル/消去/スクロール領域/挿入削除/alt-screen/DECモード/DA・DSR応答/アプリカーソルキー） | ✅ |
| **自前 forkpty** によるPTY子プロセス管理（著作権クリーン、launcherコード非流用） | ✅ |
| **SGRカラー**（8/16/256色・truecolor、**前景＋背景色・反転(reverse)・下線・太字（明色化）**、色ラン毎ラベル描画、既定は緑/黒＝ネイティブ忠実） | ✅ |
| 等幅フォント = **Liberation Mono 12px → 45桁×12行**（実機常駐をfreetype読込、unscii_8フォールバック） | ✅ |
| ブロックカーソル、readerスレッド＋40ms描画タイマ＋dirty差分 | ✅ |
| **スクロールバック**（400行リングバッファ）：**Alt+↑↓=1行 / Alt+←→=1画面** で履歴を遡る（ライブ維持）・入力で最新へ復帰・履歴表示中はヒントバー | ✅ |
| **行コピー＆貼り付け**：**Alt+c**=コピーモード（↑↓でハイライト行を選択・履歴へも遡れる・Enterで1行コピー）→ **Alt+v**=貼り付け（内部クリップボード。切断レビュー中もコピー可） | ✅ |
| 日本語/CJK表示：端末フォント(Liberation Mono)に **AlibabaPuHuiTi をfallback** → em/実機とも描画（tofu解消） | ✅ |

## 接続・セッション
| 機能 | 状態 |
|------|:--:|
| プロトコル選択：**ssh / telnet / shell（ローカル端末）/ serial（USB-シリアルコンソール）** | ✅ |
| **serial**：`picocom -b <baud> -d/-y/-p <fmt> <device>` を実行（Host→デバイス、Port→ボーレート。**Format プリセット 8N1/7E1/7O1/8N2**。User/VPN欄は非表示） | ✅(argv) / ⏳(実機picocom) |
| **serial BREAK送信**（Session Menu、serial時のみ表示。picocom escape `C-a C-b` — ルータのパスワードリカバリ用） | ✅(UI) / ⏳(実機) |
| **ssh keep-alive**（`ServerAliveInterval=30` / `CountMax=3` を常時付与 — 現場の不安定な回線で切断を確実に検知） | ✅ |
| **ssh 鍵ファイル指定**（プロファイル毎 Key 欄 → `-i <path>`。未指定なら従来通り） | ✅ |
| **接続先プロファイル**（名前/ホスト/ポート/ユーザ/proto/VPN種別/**VPN接続名**/ログ/サイズ。VPN秘密情報は非保持） | ✅ |
| 永続化：フラット key=value（`/sdcard/term.conf`、env `TERM_CONF`、初回シード） | ✅ |
| 一覧（選択→接続）／**編集画面 CRUD**（新規`n`/編集`e`/削除`d`＋確認ダイアログ）／インライン文字入力 | ✅ |
| **切断・接続失敗レビュー**：最終出力を残し赤 `DISCONNECTED` 表示（ウォッチドッグ検知）→ `Enter`/`ESC`で一覧・`r`で再接続・`Alt+矢印`で履歴 | ✅ |
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
| **F1-F12 / Insert / PageUp / PageDown** → xterm CSI（`ESC OP`〜`ESC[24~` 等。emはドライバがタグ付け、実機はホスト依存） | ✅(em) / ⏳(実機) |
| **Alt+文字 = Meta送出**（ESCプレフィクス。readline/emacs の `M-b` `M-f` 等。Alt+矢印はスクロールバック優先） | ✅(em) / ⏳(実機) |
| ESCはPTYへ素通し（vim等）、SIDEキーで Session Menu、**Alt+矢印でスクロールバック** | ✅ |
| Ctrl系：emは制御文字一部、実機は `LV_EVENT_KEYBOARD`(key_item)修飾で全対応 | ◐ |

## 日本語入力（IME）
**他アプリと同じく OS IME（fcitx5-mozc）に一本化**。ON/OFF はシステムのIMEトグル（既定 Ctrl+Space）で、アプリ内IMEは持たない。
| 機能 | 状態 |
|------|:--:|
| **OS IMEに委譲**：`SDL_TEXTINPUT`→確定かな漢字がそのままPTYへ（変換コード不要、候補窓はfcitx5が表示） | ✅(配線) / ⏳(実機mozc) |

## ログ
| 機能 | 状態 |
|------|:--:|
| **生セッションログ tee**（`/sdcard/logs/<名前>-<時刻>.log`、env `TERM_LOGDIR`） | ✅ |
| ログ一覧（mtime降順）＋閲覧（**ANSI除去**・スクロール）・削除 | ✅ |
| 実行中ログON/OFF（メニュー Toggle log） | ✅ |

## マクロ（クイック送信）
| 機能 | 状態 |
|------|:--:|
| **よく使う1行コマンドを登録→Session Menu「マクロ」から選んで送信**（本文＋Enter で実行） | ✅ |
| アプリ内で **追加`n`/編集`e`/削除`d`**（名前＋コマンド、最大12件）、`term.conf` にグローバル保存（`mac<i>.name/.text`） | ✅ |

## ファイル流し込み（file injection）
任意のテキストファイル（機器設定・スクリプト・コマンド列・メモ等。**設定ファイルに限らない**）を端末へ送出。
| 機能 | 状態 |
|------|:--:|
| **文字コード自動判定**（内蔵ヒューリスティック：UTF-8 / Shift_JIS / EUC-JP / ascii） | ✅ |
| `iconv` で **UTF-8 へ変換**して送出 | ✅ |
| 行ごと **ペース送出**（一定 10ms/行）／**プロンプト待ち送出**（出力が静まるまで次行を送らない・機器プロンプト非依存の idle 検知）を送出ダイアログで `←→` 切替 | ✅ |
| ファイルブラウザ → **送出ダイアログ**（検出エンコーディング表示）→ 送出 | ✅ |

## VPN（OS管理：アプリは接続名だけ持ち、秘密情報は保存しない）
| 機能 | 状態 |
|------|:--:|
| **VPN方式選択（iPhone風）**：none / WireGuard / OpenVPN / IKEv2 / L2TP / Tailscale | ✅(UI) |
| プロファイルは **OS側の接続名のみ保持**（wg/ovpn config名・NM/ipsec の connection名）。鍵/PSK/パスワードは**アプリに保存しない**（`/etc/wireguard`・NM 等 OS 側が保持）。旧 term.conf の秘密キーは次回保存で消去 | ✅ |
| 起動/停止：**NetworkManager があれば `pkexec nmcli connection up/down <名>` に一本化**、無ければ種別ごと wg-quick / openvpn / ipsec / xl2tpd / tailscale へfallback | ⏳(実機exec) |
| 疎通プローブ（`getifaddrs` で wg/tun/utun 検出） | ✅ |
| 接続前にVPNゲート → 失敗時「Connect anyway / Cancel」ダイアログ／終了時 down（自分で上げた時のみ） | ✅(UI) |

## 画面（全10種）
接続先一覧 / プロファイル編集 / ライブ端末 / Session Menu(オーバーレイ) / ファイルブラウザ /
送出ダイアログ / ログ一覧 / ログ閲覧 / 確認ダイアログ。モック=`docs/SCREENS.md`・`docs/mockups/`。

## ビルド・プラットフォーム
| 項目 | 状態 |
|------|:--:|
| **macOSエミュレータ**（`./build-emu.sh --run`、cardputer-emu、ヘッドレス `EMU_SHOT`） | ✅ |
| **arm64 実機 M5CardputerZero**（`czpi/build.sh`→ELF aarch64 `.so`、`deploy-run.sh`。Pi Zero 2W検証機も可） | ✅(ビルド) / ⏳(実機) |
| lvgl-dlopen アプリ（`cz_app.h` ABI：app_main/app_event/ui_init） | ✅ |

## 設計・ライセンス
- **launcherコードは非流用**（無許諾のため機能のみ参照）。libvterm=MIT、フォントは実機常駐を読むだけ（再配布なし）。
- 7モジュール：`main / pty / term / config / logsink / sendfile / vpn`（IMEはOS委譲のため自前モジュール無し）。
- GitHub: `github.com/u44e/cardputerzero-ssh-term`（private、`v0.1.0`）。

## 残（実機/低優先）
実機デプロイ＋対話打鍵検証、fcitx5-mozc漢字、**VPN実起動（実質は polkit ルール1本の配置）**、Ctrl全種、
接続中オーバーレイ（モック10、未実装）。詳細は `docs/DEVICE_CHECKLIST.md`。

**バックログ（未実装・優先度中〜低）**：scp/sftp 実ファイル転送（機器→手元の吸い出し）／SSH詳細指定
（鍵 `-i`・ジャンプ `-J`・ポートフォワード `-L/-R/-D`）／ログのサイズ上限・ローテーション／
スクロールバック・ログ内の検索／プロファイル複製・並べ替え／カラーテーマ／複数セッション。
