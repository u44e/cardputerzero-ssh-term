#!/usr/bin/env python3
"""CardputerZero SSH/Telnet ターミナル — 画面案 PNG（実機フォント忠実版）.

実機(LVGL)の標準フォントに合わせる:
  - UI(タイトル/メニュー/一覧/キーガイド/ステータス) = Montserrat (実機ビルトイン montserrat_*)
  - ターミナル本文/ログ閲覧本文 = unscii_8 (実機唯一の等幅, 8x8px) → 320pxで 40列
  - キーガイドの矢印グリフのみ unscii で描画 (Montserratに矢印が無いため。実機ではLVGLシンボル相当)
レンダリング: SS=4 で高解像度描画→縮小。ターミナル/メニュー/ログ閲覧の背景は黒。
出力: docs/mockups/<name>.png (320x170) と <name>@4x.png (1280x680)
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 170
SS = 4
HERE = os.path.dirname(__file__)
FONTS = os.path.join(HERE, "fonts")
OUT = os.path.join(HERE, "mockups")
os.makedirs(OUT, exist_ok=True)

# ---- palette ----
BG      = (0x1A, 0x1A, 0x2E)   # app chrome screens
TERM_BG = (0x00, 0x00, 0x00)   # terminal = black
SURFACE = (0x10, 0x10, 0x1E)
BAR     = (0x12, 0x12, 0x24)
STATUS  = (0x0A, 0x0A, 0x10)
CYAN    = (0x3A, 0xD8, 0xFF)
HILITE  = (0x2C, 0x2C, 0x52)
GREEN   = (0x4C, 0xD9, 0x6A)
AMBER   = (0xFF, 0xB8, 0x2E)
RED     = (0xFF, 0x6B, 0x6B)
TEXT    = (0xEC, 0xEC, 0xF2)
DIM     = (0xA2, 0xA2, 0xC0)
BORDER  = (0x36, 0x36, 0x5C)
PROMPT  = (0x7A, 0xE7, 0xC0)

MONT   = os.path.join(FONTS, "Montserrat-Medium.ttf")
MONT_B = os.path.join(FONTS, "Montserrat-SemiBold.ttf")
UNSCII = os.path.join(FONTS, "unscii-8.ttf")
BIZUD  = os.path.join(FONTS, "BIZUDGothic-Regular.ttf")   # UI 日本語 / 端末CJKのAlibaba PuHui Ti代用
MISAKI = os.path.join(FONTS, "misaki_gothic.ttf")          # (旧案)
MENLO  = "/System/Library/Fonts/Menlo.ttc"                # 端末等幅=Liberation Mono のプロキシ(実機はLiberation Mono)

def is_ascii(s):
    return all(ord(ch) < 128 for ch in s)

class Canvas:
    """論理 320x170 座標で描き、SS倍で描画→縮小保存."""
    def __init__(self, bg):
        self.img = Image.new("RGB", (W*SS, H*SS), bg)
        self.d = ImageDraw.Draw(self.img)
    def font(self, path, size):
        return ImageFont.truetype(path, int(size*SS))
    def rect(self, box, **kw):
        self.d.rectangle([v*SS for v in box], **kw)
    def line(self, pts, **kw):
        kw["width"] = max(1, kw.get("width", 1)*SS)
        self.d.line([v*SS for v in pts], **kw)
    def text(self, xy, s, font, **kw):
        self.d.text((xy[0]*SS, xy[1]*SS), s, font=font, **kw)
    def ellipse(self, box, **kw):
        self.d.ellipse([v*SS for v in box], **kw)
    def tlen(self, s, font):
        return self.d.textlength(s, font=font)/SS
    def save(self, name):
        self.img.save(os.path.join(OUT, f"{name}@{SS}x.png"))
        self.img.resize((W, H), Image.LANCZOS).save(os.path.join(OUT, f"{name}.png"))
        print("wrote", name)

def fonts(c):
    return dict(
        title  = c.font(MONT_B, 13),
        ui     = c.font(MONT, 11),
        small  = c.font(MONT, 10),
        status = c.font(MONT, 10),
        arrow  = c.font(UNSCII, 8),
        term   = c.font(MENLO, 11.5),    # 端末等幅 = Liberation Mono 相当(Menlo代用) → 45桁
        term_jp= c.font(BIZUD, 11.5),    # 端末内CJK = Alibaba PuHui Ti 相当(BIZUD代用)
        jp     = c.font(BIZUD, 11),      # UI の日本語
        jp_s   = c.font(BIZUD, 10),
    )

def term_font(F, line):
    return F["term"] if is_ascii(line) else F["term_jp"]

TITLE_H, BOTTOM_H = 26, 20

def chrome(c, F, title, right=None):
    c.rect([0, 0, W, TITLE_H], fill=BAR)
    c.line([0, TITLE_H, W, TITLE_H], fill=BORDER)
    c.text((8, TITLE_H/2), title, font=F["title"], fill=CYAN, anchor="lm")
    if right:
        c.text((W-8, TITLE_H/2), right, font=F["small"], fill=DIM, anchor="rm")
    by = H - BOTTOM_H
    c.rect([0, by, W, H], fill=BAR)
    c.line([0, by, W, by], fill=BORDER)
    return TITLE_H+1, by-1

def keyguide(c, F, arrows, text):
    """arrows: unscii矢印トークン(例 '↑↓'), text: Montserrat本文."""
    by = H - BOTTOM_H; yc = by + BOTTOM_H/2; x = 8
    if arrows:
        c.text((x, yc), arrows, font=F["arrow"], fill=DIM, anchor="lm")
        x += c.tlen(arrows, F["arrow"]) + 3
    c.text((x, yc), text, font=F["small"], fill=DIM, anchor="lm")

# =====================================================================
# 1. SCR_PROFILES
# =====================================================================
def scr_profiles():
    c = Canvas(BG); F = fonts(c)
    top, _ = chrome(c, F, "Sessions", right="wifi   3/4")
    rows = [
        ("home-pi",   "ssh", "pi@192.168.1.50:22",  True,  True),
        ("router",    "tel", "192.168.1.1:23",      False, False),
        ("vps",       "ssh", "root@example.com:22", True,  False),
        ("現場ルータ", "tel", "10.20.0.1:23",        False, True),
    ]
    rh = 22; y = top + 4
    for i, (name, proto, target, vpn, log) in enumerate(rows):
        sel = (i == 0); yc = y + rh/2 - 1
        if sel:
            c.rect([0, y-2, W, y+rh-4], fill=HILITE)
            c.rect([0, y-2, 3, y+rh-4], fill=CYAN)
        nfont = F["ui"] if is_ascii(name) else F["jp"]
        c.text((12, yc), name, font=nfont, fill=TEXT, anchor="lm")
        c.text((104, yc), proto, font=F["small"], fill=(CYAN if proto=="ssh" else AMBER), anchor="lm")
        c.text((136, yc), target, font=F["small"], fill=(TEXT if sel else DIM), anchor="lm")
        fx = W-12
        if log:
            c.text((fx, yc), "L", font=F["ui"], fill=AMBER, anchor="rm"); fx -= 16
        if vpn:
            c.text((fx, yc), "V", font=F["ui"], fill=GREEN, anchor="rm")
        y += rh
    keyguide(c, F, "↑↓", " Sel    Enter Connect    e Edit    n New    d Del    l Logs")
    c.save("1_profiles")

# =====================================================================
# 2. SCR_EDITOR
# =====================================================================
def scr_editor():
    c = Canvas(BG); F = fonts(c)
    top, _ = chrome(c, F, "Edit: home-pi")
    fields = [
        ("Name",  "home-pi",          "text", False),
        ("Host",  "192.168.1.50",     "edit", True),
        ("Port",  "22",               "text", False),
        ("User",  "pi",               "text", False),
        ("Proto", "ssh",              "sel",  False),
        ("VPN",   "wg-home",          "sel",  False),
        ("Log",   "save session log", "chk",  False),
        ("Size",  "12px  45x12",      "sel",  False),
        ("UI",    "12px",             "sel",  False),
    ]
    rh = 13; y = top + 2
    for label, val, kind, sel in fields:
        yc = y + rh/2
        if sel:
            c.rect([0, y, W, y+rh-1], fill=HILITE)
            c.rect([0, y, 3, y+rh-1], fill=CYAN)
        c.text((12, yc), label, font=F["ui"], fill=(TEXT if sel else DIM), anchor="lm")
        vx = 92
        if kind == "sel":
            c.text((vx, yc), "‹", font=F["ui"], fill=DIM, anchor="lm")
            c.text((vx+10, yc), val, font=F["ui"], fill=CYAN, anchor="lm")
            w = c.tlen(val, F["ui"])
            c.text((vx+10+w+3, yc), "›", font=F["ui"], fill=DIM, anchor="lm")
        elif kind == "chk":
            c.text((vx, yc), "[x]", font=F["ui"], fill=GREEN, anchor="lm")
            c.text((vx+26, yc), val, font=F["ui"], fill=TEXT, anchor="lm")
        else:
            c.text((vx, yc), val, font=F["ui"], fill=TEXT, anchor="lm")
            if kind == "edit":
                w = c.tlen(val, F["ui"])
                c.rect([vx+w+2, y+2, vx+w+5, y+rh-2], fill=CYAN)  # caret
        y += rh
    keyguide(c, F, "↑↓←→", " Field/Toggle   Enter Edit   s Save   ESC Cancel")
    c.save("2_editor")

# =====================================================================
# 3. SCR_TERM  (unscii_8, 40 cols, black bg)
# =====================================================================
TERM_LINES = [
    ("pi@home-pi:~$ ls", PROMPT),
    ("Desktop  bin  notes.txt  src", TEXT),
    ("pi@home-pi:~$ echo 日本語テスト", PROMPT),
    ("日本語テスト", TEXT),
    ("pi@home-pi:~$ ping -c1 10.0.0.1", PROMPT),
    ("64 bytes from 10.0.0.1: time=0.42 ms", CYAN),
    ("pi@home-pi:~$ ", PROMPT),
]

def term_grid(c, F, top):
    lh = 12; y = top; last_x = 0
    for text, col in TERM_LINES:
        fnt = term_font(F, text)
        c.text((3, y), text, font=fnt, fill=col, anchor="lt")
        last_x = 3 + c.tlen(text, fnt)
        y += lh
    cy = top + (len(TERM_LINES)-1)*lh
    c.rect([last_x, cy+1, last_x+7, cy+12], fill=TEXT)  # block cursor (1 cell 7x12)

def status_bar(c, F, rec=True):
    by = H - 16
    c.rect([0, by, W, H], fill=STATUS)
    c.line([0, by, W, by], fill=BORDER)
    yc = by + 8; x = 7
    c.text((x, yc), "home-pi", font=F["status"], fill=TEXT, anchor="lm"); x += c.tlen("home-pi", F["status"])+10
    c.ellipse([x, yc-3, x+6, yc+3], fill=GREEN); x += 10
    c.text((x, yc), "CONNECTED", font=F["status"], fill=GREEN, anchor="lm"); x += c.tlen("CONNECTED", F["status"])+12
    c.text((x, yc), "VPN up", font=F["status"], fill=CYAN, anchor="lm"); x += c.tlen("VPN up", F["status"])+12
    if rec:
        c.ellipse([x, yc-3, x+6, yc+3], fill=AMBER); x += 10
        c.text((x, yc), "REC", font=F["status"], fill=AMBER, anchor="lm")
    c.text((W-7, yc), "SIDE=menu", font=F["status"], fill=DIM, anchor="rm")

def scr_term():
    c = Canvas(TERM_BG); F = fonts(c)
    term_grid(c, F, 4)
    status_bar(c, F, rec=True)
    c.save("3_term")

# =====================================================================
# 4. Session Menu overlay (over black terminal)
# =====================================================================
def scr_menu():
    c = Canvas(TERM_BG); F = fonts(c)
    term_grid(c, F, 4)
    status_bar(c, F, rec=True)
    ov = Image.new("RGBA", (W*SS, H*SS), (0, 0, 0, 120))
    c.img = Image.alpha_composite(c.img.convert("RGBA"), ov).convert("RGB")
    c.d = ImageDraw.Draw(c.img)
    mw, mh = 168, 128; mx, my = W-mw-10, 12
    c.rect([mx, my, mx+mw, my+mh], fill=SURFACE, outline=CYAN)
    c.rect([mx, my, mx+mw, my+18], fill=HILITE)
    c.text((mx+8, my+9), "Session", font=F["ui"], fill=CYAN, anchor="lm")
    items = [("Detach (keep running)", True), ("Send file (config)...", False),
             ("Font size  ‹ 12px ›", False),
             ("Toggle log  [on]", False), ("Close session", False),
             ("Back to list", False)]
    iy = my+29
    for label, sel in items:
        if sel:
            c.rect([mx+3, iy-8, mx+mw-3, iy+8], fill=HILITE)
        c.text((mx+12, iy), label, font=F["small"],
               fill=(TEXT if sel else DIM), anchor="lm")
        iy += 16
    by = H - BOTTOM_H
    c.rect([0, by, W, H], fill=BAR); c.line([0, by, W, by], fill=BORDER)
    keyguide(c, F, "↑↓", " Move    Enter Select    SIDE / ESC  Close menu")
    c.save("4_menu")

# =====================================================================
# 5. SCR_LOGS list
# =====================================================================
def scr_logs():
    c = Canvas(BG); F = fonts(c)
    top, _ = chrome(c, F, "Logs", right="12 files")
    rows = [
        ("home-pi-20260625-101107.log", "12.4 KB", True),
        ("router-20260624-180233.log",  "3.1 KB",  False),
        ("vps-20260624-093015.log",     "240.7 KB",False),
        ("home-pi-20260623-220140.log", "8.0 KB",  False),
    ]
    rh = 22; y = top + 4
    for name, size, sel in rows:
        yc = y + rh/2 - 1
        if sel:
            c.rect([0, y-2, W, y+rh-4], fill=HILITE)
            c.rect([0, y-2, 3, y+rh-4], fill=CYAN)
        c.text((12, yc), name, font=F["ui"], fill=(TEXT if sel else DIM), anchor="lm")
        c.text((W-12, yc), size, font=F["small"], fill=DIM, anchor="rm")
        y += rh
    keyguide(c, F, "↑↓", " Sel    Enter View    d Delete    ESC Back")
    c.save("5_logs")

# =====================================================================
# 6. SCR_LOGS viewer (unscii_8 content, black bg)
# =====================================================================
def scr_logview():
    c = Canvas(TERM_BG); F = fonts(c)
    c.rect([0, 0, W, TITLE_H], fill=BAR); c.line([0, TITLE_H, W, TITLE_H], fill=BORDER)
    c.text((8, TITLE_H/2), "home-pi-20260625-101107.log", font=F["title"], fill=CYAN, anchor="lm")
    c.text((W-8, TITLE_H/2), "1/12", font=F["small"], fill=DIM, anchor="rm")
    lines = [
        ("Last login: Wed Jun 25 10:11:00 2026", DIM),
        ("pi@home-pi:~$ ls", PROMPT),
        ("Desktop  bin  notes.txt  src", TEXT),
        ("pi@home-pi:~$ uname -srm", PROMPT),
        ("Linux 6.6.74-v8 aarch64", TEXT),
        ("pi@home-pi:~$ uptime", PROMPT),
        (" 10:12:03 up 3 days,  load 0.08", TEXT),
        ("pi@home-pi:~$ exit", PROMPT),
    ]
    y = TITLE_H + 5
    for ln, col in lines:
        c.text((4, y), ln, font=F["term"], fill=col, anchor="lt"); y += 12
    by = H - BOTTOM_H
    c.rect([0, by, W, H], fill=BAR); c.line([0, by, W, by], fill=BORDER)
    keyguide(c, F, "↑↓", " Scroll    ESC Back")
    c.save("6_logview")

# =====================================================================
# 7. IME — 日本語入力(変換候補バー) over terminal
# =====================================================================
def scr_ime():
    c = Canvas(TERM_BG); F = fonts(c)
    # 端末: 入力途中の行を表示
    lines = [
        ("pi@home-pi:~$ ls", PROMPT),
        ("Desktop  bin  notes.txt  src", TEXT),
        ("pi@home-pi:~$ echo ", PROMPT),
    ]
    y = 4
    for t, col in lines:
        c.text((3, y), t, font=term_font(F, t), fill=col, anchor="lt"); y += 12
    # 未確定文字列(preedit) を下線付きで継続表示
    px = 3 + c.tlen("pi@home-pi:~$ echo ", F["term"])
    py = 4 + 2*12
    pre = "にほんご"
    c.text((px, py), pre, font=F["term_jp"], fill=AMBER, anchor="lt")
    pw = c.tlen(pre, F["term_jp"])
    c.line([px, py+12, px+pw, py+12], fill=AMBER)        # 変換前の下線
    c.rect([px+pw, py+1, px+pw+7, py+12], fill=TEXT)     # カーソル

    # 候補バー (画面下, ステータスの上)
    cb_y = H - 16 - 22
    c.rect([0, cb_y, W, cb_y+22], fill=SURFACE)
    c.line([0, cb_y, W, cb_y], fill=BORDER)
    c.text((6, cb_y+11), "変換", font=F["jp_s"], fill=DIM, anchor="lm")
    cands = ["日本語", "二本語", "日本後", "にほんご"]
    x = 44
    for i, w in enumerate(cands):
        lab = f"{i+1} {w}"
        ww = c.tlen(lab, F["jp_s"]) + 14
        if i == 0:
            c.rect([x-4, cb_y+2, x-4+ww, cb_y+20], fill=HILITE, outline=CYAN)
        c.text((x, cb_y+11), lab, font=F["jp_s"], fill=(TEXT if i == 0 else DIM), anchor="lm")
        x += ww + 6
    # ステータス (IME ON 表示 "あ")
    by = H - 16
    c.rect([0, by, W, H], fill=STATUS); c.line([0, by, W, by], fill=BORDER)
    c.rect([6, by+3, 22, by+13], fill=GREEN)
    c.text((14, by+8), "あ", font=c.font(BIZUD, 9), fill=(0,0,0), anchor="mm")
    c.text((28, by+8), "IME ON  Fn+Space 切替   Space 変換   Enter 確定",
           font=F["jp_s"], fill=DIM, anchor="lm")
    c.save("7_ime")

# =====================================================================
# 8. 設定流し込み — Send file + 文字コード自動認識
# =====================================================================
def scr_sendfile():
    c = Canvas(BG); F = fonts(c)
    top, _ = chrome(c, F, "Send file → home-pi", right="config")
    y = top + 6
    def row(label, value, vcol=TEXT, sel=False, vfont=None):
        nonlocal y
        if sel:
            c.rect([0, y-2, W, y+15], fill=HILITE); c.rect([0, y-2, 3, y+15], fill=CYAN)
        c.text((12, y+7), label, font=F["small"], fill=(TEXT if sel else DIM), anchor="lm")
        c.text((110, y+7), value, font=(vfont or F["ui"]), fill=vcol, anchor="lm")
        y += 19
    row("File",      "RX1500-running.cfg", TEXT)
    row("Detected",  "Shift_JIS  (auto 98%)", AMBER)
    row("Send as",   "‹ UTF-8 ›   SJIS / EUC / raw", CYAN, sel=True)
    row("Pace",      "‹ 15 ms/line ›   wait-prompt: off", CYAN)
    # progress bar
    c.text((12, y+7), "Progress", font=F["small"], fill=DIM, anchor="lm")
    bx, bw = 110, 150
    c.rect([bx, y+1, bx+bw, y+13], outline=BORDER)
    c.rect([bx, y+1, bx+int(bw*0.21), y+13], fill=GREEN)
    c.text((bx+bw+6, y+7), "24/118", font=F["small"], fill=DIM, anchor="lm")
    keyguide(c, F, "↑↓←→", " Field/Change   Enter Send   ESC Cancel")
    c.save("8_sendfile")

# =====================================================================
# 9. ファイルブラウザ (Send file のファイル選択)
# =====================================================================
def scr_files():
    c = Canvas(BG); F = fonts(c)
    top, _ = chrome(c, F, "Send file:  /sdcard", right="5 items")
    entries = [
        ("..",                 "",       True,  False),
        ("configs/",           "",       True,  False),
        ("RX1500-running.cfg",  "3.2 KB", False, True),
        ("sw-core.txt",         "1.1 KB", False, False),
        ("メモ.txt",            "842 B",  False, False),
    ]
    rh = 20; y = top + 3
    for name, size, is_dir, sel in entries:
        yc = y + rh/2 - 1
        if sel:
            c.rect([0, y-1, W, y+rh-3], fill=HILITE); c.rect([0, y-1, 3, y+rh-3], fill=CYAN)
        # icon
        c.text((12, yc), ("[D]" if is_dir else "   "), font=F["small"], fill=AMBER, anchor="lm")
        nf = F["ui"] if is_ascii(name) else F["jp"]
        c.text((42, yc), name, font=nf, fill=(CYAN if is_dir else (TEXT if sel else DIM)), anchor="lm")
        if size:
            c.text((W-12, yc), size, font=F["small"], fill=DIM, anchor="rm")
        y += rh
    keyguide(c, F, "↑↓", " Sel    Enter Open / Pick    / Filter    ESC Back")
    c.save("9_files")

# helper: 中央ダイアログ
def dialog(c, F, w, h, title, tcol):
    mx, my = (W-w)//2, (H-h)//2 - 4
    c.rect([mx, my, mx+w, my+h], fill=SURFACE, outline=tcol)
    c.rect([mx, my, mx+w, my+20], fill=HILITE)
    c.text((mx+10, my+10), title, font=F["ui"], fill=tcol, anchor="lm")
    return mx, my

# =====================================================================
# 10. 接続中オーバーレイ (VPN→疎通→ssh)
# =====================================================================
def scr_connecting():
    c = Canvas(BG); F = fonts(c)
    chrome(c, F, "Sessions", right="connecting")
    ov = Image.new("RGBA", (W*SS, H*SS), (0, 0, 0, 120))
    c.img = Image.alpha_composite(c.img.convert("RGBA"), ov).convert("RGB")
    c.d = ImageDraw.Draw(c.img)
    mx, my = dialog(c, F, 234, 104, "Connecting  home-pi", CYAN)
    steps = [
        ("VPN  wg-home  up",        GREEN, "done"),
        ("Probe 192.168.1.50:22",   CYAN,  "active"),
        ("ssh  pi@192.168.1.50",    DIM,   "pending"),
    ]
    iy = my + 34
    for label, col, st in steps:
        c.ellipse([mx+14, iy-4, mx+22, iy+4], fill=col)
        c.text((mx+32, iy), label, font=F["small"], fill=(TEXT if st!="pending" else DIM), anchor="lm")
        c.text((mx+234-12, iy), st, font=F["small"], fill=col, anchor="rm")
        iy += 20
    c.text((mx+14, my+94), "Waiting for VPN handshake...", font=F["small"], fill=DIM, anchor="lm")
    keyguide(c, F, "", "ESC Cancel")
    c.save("10_connecting")

# =====================================================================
# 11. 確認/エラーダイアログ (VPN失敗→このまま接続/中止)
# =====================================================================
def scr_dialog():
    c = Canvas(BG); F = fonts(c)
    chrome(c, F, "Sessions")
    ov = Image.new("RGBA", (W*SS, H*SS), (0, 0, 0, 130))
    c.img = Image.alpha_composite(c.img.convert("RGBA"), ov).convert("RGB")
    c.d = ImageDraw.Draw(c.img)
    mx, my = dialog(c, F, 220, 100, "VPN failed", RED)
    c.text((mx+14, my+34), "wg-home did not come up", font=F["small"], fill=TEXT, anchor="lm")
    c.text((mx+14, my+50), "(handshake timeout 8s)", font=F["small"], fill=DIM, anchor="lm")
    opts = [("Connect anyway", True), ("Cancel", False)]
    ox = mx+14
    for label, sel in opts:
        w = c.tlen(label, F["small"]) + 18
        if sel:
            c.rect([ox, my+66, ox+w, my+86], fill=HILITE, outline=CYAN)
        c.text((ox+w/2, my+76), label, font=F["small"], fill=(TEXT if sel else DIM), anchor="mm")
        ox += w + 10
    keyguide(c, F, "←→", " Select    Enter OK    ESC Cancel")
    c.save("11_dialog")

# =====================================================================
# 12. 切断状態の端末
# =====================================================================
def scr_term_disc():
    c = Canvas(TERM_BG); F = fonts(c)
    lines = list(TERM_LINES[:-1]) + [
        ("pi@home-pi:~$ logout", PROMPT),
        ("Connection to home-pi closed.", DIM),
    ]
    y = 4
    for t, col in lines:
        c.text((3, y), t, font=term_font(F, t), fill=col, anchor="lt"); y += 12
    by = H - 16
    c.rect([0, by, W, H], fill=STATUS); c.line([0, by, W, by], fill=BORDER)
    yc = by + 8; x = 7
    c.text((x, yc), "home-pi", font=F["status"], fill=DIM, anchor="lm"); x += c.tlen("home-pi", F["status"])+10
    c.ellipse([x, yc-3, x+6, yc+3], fill=RED); x += 10
    c.text((x, yc), "DISCONNECTED", font=F["status"], fill=RED, anchor="lm")
    c.text((W-7, yc), "Enter: close   r: reconnect", font=F["status"], fill=DIM, anchor="rm")
    c.save("12_term_disc")

if __name__ == "__main__":
    scr_profiles(); scr_editor(); scr_term()
    scr_menu(); scr_logs(); scr_logview()
    scr_ime(); scr_sendfile()
    scr_files(); scr_connecting(); scr_dialog(); scr_term_disc()
    print("done ->", OUT)
