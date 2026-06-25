# docs/fonts — mockup generator fonts (not committed)

`docs/gen_mockups.py` renders the screen mockups using these fonts. The TTFs are
**not committed** (to avoid redistributing licensed fonts); fetch them locally:

```sh
cd docs/fonts
# UI (OFL)
curl -fsSL -o Montserrat-Medium.ttf   "https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Medium.ttf"
curl -fsSL -o Montserrat-SemiBold.ttf "https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-SemiBold.ttf"
# JP UI (OFL) — Alibaba PuHui Ti proxy
curl -fsSL -o BIZUDGothic-Regular.ttf "https://github.com/googlefonts/morisawa-biz-ud-gothic/raw/main/fonts/ttf/BIZUDGothic-Regular.ttf"
# terminal mono proxy: Liberation Mono is on the device; the mockup uses macOS Menlo (/System/Library/Fonts/Menlo.ttc)
```

The **real device** loads Liberation Mono + Alibaba PuHui Ti from
`/usr/share/APPLaunch/share/font/` — the app does not bundle or redistribute fonts.
