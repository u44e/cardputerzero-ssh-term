# 公式 AppStore 提出手順（コピペ用）

方針：**リポジトリを公開 → 公式 `czdev` で提出**。`czdev login` は本人の GitHub OAuth が要るので
以下は**あなたの端末で実行**する。`.deb` とメタデータ（`app-builder.json` の `store` ブロック）は準備済み。

- 出力物: `port/dist/netterm_0.2.2-m5stack1_arm64.deb`（sha256 `5abb43ffab4696c485b67b34251fe648baa6d3311fddd05a29e816f2bff1d52e` / 474 KB）
- czdev の検証（`.desktop` あり・Maintainer=`u44e@users.noreply.github.com`・サイズ<100MB）は充足済み

---

## 0. リポジトリを公開（source URL を解決させる＝審査で有利）
```bash
gh repo edit u44e/cardputerzero-ssh-term --visibility public --accept-visibility-change-consequences
```
> メタデータは既に `source.openness=open-source` で push 済み。公開で homepage/source_repo の 404 が解消。

## 1. `.deb` をビルド（未ビルド or 作り直す場合のみ。Docker 必要）
```bash
cd ~/Projects/cardputerzero-ssh-term
./port/build.sh          # -> port/dist/netterm_0.2.2-m5stack1_arm64.deb
```

## 2. `czdev` を用意（初回のみ。要 Rust/cargo・git-lfs）
```bash
# git-lfs（未導入なら）
brew install git-lfs && git lfs install

# czdev を CardputerZero-AppBuilder からビルド
git clone https://github.com/CardputerZero/CardputerZero-AppBuilder ~/CardputerZero-AppBuilder
cd ~/CardputerZero-AppBuilder
cargo build --release -p czdev
# PATH を通す（または ~/CardputerZero-AppBuilder/target/release/czdev を直接使う）
export PATH="$HOME/CardputerZero-AppBuilder/target/release:$PATH"
czdev doctor            # 環境チェック（任意）
```

## 3. ログイン（初回のみ・GitHub OAuth デバイスフロー）
```bash
czdev login             # ブラウザでコード認可 → token を ~/.config/czdev/token に保存
```

## 4. 提出（fork/branch/upload/PR を czdev が自動化）
```bash
cd ~/Projects/cardputerzero-ssh-term       # app-builder.json のある場所で実行
czdev publish --deb port/dist/netterm_0.2.2-m5stack1_arm64.deb
# 完了時に PR URL が表示される（CardputerZero/packages 宛て）
```
`czdev publish` は自動で: `.deb` 検証 → `CardputerZero/packages` を fork → `publish/netterm-…` ブランチ作成 →
`.deb`＋`meta.json`（= `store` ブロック由来）＋アイコン＋スクショをアップロード → PR 作成。

## 5. 提出後
- CI（`validate-pr.yml`）が `.deb` 構造を検証 → メンテナがレビュー → マージで公開。
- 承認後、**4文字 share code**（ホーム画面 `S`→コード）でユーザーが導入可能に。
- **審査コメントに明記される開示**（`app-builder.json` の `review.notes` に記載済み）:
  「arm64 コンテナでビルド・起動検証済み／**実機 M5CardputerZero では未検証**（evdev の Fn/記号レイヤ・
  SIDE keycode・fbdev 表示は実機 keymap 待ち）」。実機入手後に keymap を確定し再 publish で更新。

---

## つまずいたら
- **`czdev` が無い/ビルド不可** → CardputerZero-AppBuilder の README を参照（`cargo build --release -p czdev`）。
- **カテゴリで弾かれた** → `app-builder.json` の `store.categories` を `["Utilities","System"]` に落とす
  （`Network`/`Developer Tools` は新レジストリ分類。旧バリデータだと未対応の可能性）。
- **Maintainer email 不一致** → `.deb` は `u44e@users.noreply.github.com`。PR 作成者（`u44e`）と一致するので通常OK。
- **バージョン更新** → `app-builder.json`＋`port/CMakeLists.txt` の版を上げ、`./port/build.sh` 後に再 `czdev publish`。
