# 公式 AppStore 提出手順（コピペ用・検証済み）

方針：**リポジトリを公開 → 公式 `czdev` で提出**。`czdev login` は本人の GitHub OAuth が要るので
以下は**あなたの端末で実行**する。`.deb` とメタデータ（`app-builder.json` の `store` ブロック）は準備済み。

- 出力物: `port/dist/netterm_0.2.2-m5stack1_arm64.deb`（sha256 `5abb43ffab4696c485b67b34251fe648baa6d3311fddd05a29e816f2bff1d52e` / 474 KB）
- czdev の検証（`.desktop` あり・Maintainer=`u44e@users.noreply.github.com`・パッケージ名 `netterm` 有効・サイズ<100MB）は充足済み

> **czdev の実体**（重要）：公式 doc のリンク `CardputerZero/CardputerZero-AppBuilder` は**古くて 404**。
> 正しくは **`m5stack/CardputerZero-AppBuilder`**（public）。**提出系コマンドは Python 製で Rust 不要**
> （`scripts/czdev/` パッケージ。要 Python3・git・git-lfs・dpkg-deb）。

---

## 0. リポジトリを公開（source/homepage URL を解決＝審査で有利）
```bash
gh repo edit u44e/cardputerzero-ssh-term --visibility public --accept-visibility-change-consequences
```
> メタデータは既に `source.openness=open-source` で push 済み。

## 1. `.deb` をビルド（未ビルド or 作り直す場合のみ。要 Docker）
```bash
cd ~/Projects/cardputerzero-ssh-term
./port/build.sh          # -> port/dist/netterm_0.2.2-m5stack1_arm64.deb
```

## 2. czdev（Python 製）と依存を用意（初回のみ・Rust 不要）
```bash
# 依存（mac に dpkg-deb が無いので dpkg を入れる）
brew install git-lfs dpkg && git lfs install

# AppBuilder を取得（提出だけなら submodule 不要。エミュレータも使うなら --recursive）
git clone https://github.com/m5stack/CardputerZero-AppBuilder ~/CardputerZero-AppBuilder

# 動作確認（--help が出れば OK）
PYTHONPATH="$HOME/CardputerZero-AppBuilder/scripts" python3 -m czdev --help
```

## 3. ログイン（初回のみ・GitHub OAuth デバイスフロー）
```bash
PYTHONPATH="$HOME/CardputerZero-AppBuilder/scripts" python3 -m czdev login
# ブラウザでコード認可 → token は ~/.config/czdev/token に保存
```

## 4. 提出（★必ず「自分のアプリ dir」で実行）
`czdev` は **cwd の `app-builder.json`** を読んで store メタ／アイコン／スクショを解決する。
必ず `~/Projects/cardputerzero-ssh-term` で実行すること。
```bash
cd ~/Projects/cardputerzero-ssh-term
PYTHONPATH="$HOME/CardputerZero-AppBuilder/scripts" python3 -m czdev publish \
  --deb port/dist/netterm_0.2.2-m5stack1_arm64.deb
# 完了時に PR URL が表示される（CardputerZero/packages 宛て）
```
`czdev publish` が自動で: `.deb` 検証（.desktop / email / パッケージ名 / サイズ）→ `CardputerZero/packages` を
**あなたの fork** に fork → fork の Release に `.deb` をアップロード（git には binary を入れない）→
`meta.json`＋アイコン＋スクショ＋`<pkg>_<ver>_<arch>.deb.release.json`（fork Release URL＋sha256）を commit → PR 作成。

## 5. 提出後
- CI（`validate-pr.yml`）が `.deb` 構造を検証 → メンテナがレビュー → マージで `apt-pool` に昇格し公開。
- 承認後、**4文字 share code**（ホーム画面 `S`→コード）でユーザーが導入可能に。
- **審査コメントに出る開示**（`app-builder.json` の `review.notes` に記載済み）：
  「arm64 コンテナでビルド・起動検証済み／**実機 M5CardputerZero では未検証**（evdev の Fn/記号レイヤ・
  SIDE keycode・fbdev 表示は実機 keymap 待ち）」。実機入手後に keymap を確定し再 publish で更新。

---

## czdev の既知バグと必要な設定（外部コントリビュータ向け・適用済み）
外部（非 collaborator）が publish するとき、素の czdev は2箇所で落ちる。`~/CardputerZero-AppBuilder` に修正済み：
1. **`HTTP 403` で `check_permission` がクラッシュ** → `scripts/czdev/github_client.py` の `if e.code == 404:` を
   `if e.code in (403, 404):` に（403＝権限照会不可＝write権限なし→fork フローへ）。
2. **メタデータ push が SSH（`git@github.com`）** → `scripts/czdev/publish.py` の `remote_url` を
   `https://github.com/...` に変更（SSH 鍵不要で gh の認証で push）。加えて **一度だけ**:
   ```bash
   gh auth setup-git      # git の HTTPS push で gh のトークンを使う
   ```
> これらは czdev 側のバグなので、上流（`m5stack/CardputerZero-AppBuilder`）に issue/PR を出すと親切。

## つまずいたら
- **`No module named czdev`** → `PYTHONPATH` に `~/CardputerZero-AppBuilder/scripts` を必ず付ける（`-m czdev` は package 実行）。
- **`app-builder.json not found`** → `cd ~/Projects/cardputerzero-ssh-term` で（＝cwd をアプリ dir に）実行しているか確認。
- **`dpkg-deb: command not found`** → `brew install dpkg`。
- **Maintainer email 不一致** → `.deb` は `u44e@users.noreply.github.com`。PR 作成者（`u44e`）と一致するので通常OK。
- **カテゴリで弾かれた** → `store.categories` を `["Utilities","System"]` に落とす（`Network`/`Developer Tools` は新レジストリ分類）。
- **バージョン更新** → `app-builder.json`＋`port/CMakeLists.txt` の版を上げ、`./port/build.sh` 後に再 `publish`。
  `python3 -m czdev bump --deb <file>` で次パッチ版を確認できる。
