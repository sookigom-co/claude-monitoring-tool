#!/usr/bin/env python3
"""Turns the render_screens .ppm files into PNGs and labelled comparison sheets (needs Pillow)."""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

LANGS = [("en", "English"), ("ko", "한국어")]
LAYOUTS = [("A", "A안 동심원 링"), ("B", "B안 위 시계 + 아래 게이지"), ("C", "C안 좌우 반원 링 (기본)")]
STATES = [("normal", "정상 42% / 55%"), ("pct70", "70%대 74% / 78%"), ("pct90", "90%대 93% / 97%"),
          ("nodata", "데이터 없음 (조회 실패)"), ("relay-error", "relay status=error"),
          ("relay-stale", "relay status=stale"), ("warn-token", "경고: 토큰 평문"), ("warn-cert", "경고: 인증서 미검사"),
          ("no-time", "시각 미동기화"), ("wifi-lost", "Wi-Fi 끊김 · 100% / 0%")]
SINGLES = ["setup", "updating", "connecting"]
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
SCALE, PAD, HEAD, LEFT = 2, 16, 40, 170


def grid(src, out, font, rows, cols, name):
    cell = 240 * SCALE
    sheet = Image.new("RGB", (LEFT + len(cols) * (cell + PAD), HEAD + len(rows) * (cell + PAD)), (230, 232, 236))
    draw = ImageDraw.Draw(sheet)
    for col, (_, title) in enumerate(cols):
        draw.text((LEFT + col * (cell + PAD), 10), title, font=font, fill=(20, 20, 20))
    for row, (key, title) in enumerate(rows):
        y = HEAD + row * (cell + PAD)
        draw.text((10, y + cell // 2 - 24), title.replace(" ", "\n", 2), font=font, fill=(20, 20, 20))
        for col, (state, _) in enumerate(cols):
            stem = key.format(state)
            img = Image.open(src / f"{stem}.ppm").convert("RGB")
            img.save(out / f"{stem}.png")
            sheet.paste(img.resize((cell, cell), Image.NEAREST), (LEFT + col * (cell + PAD), y))
    sheet.save(out / name)


def main():
    src = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(FONT, 18, index=1)
    rows = [(f"{lang}-{key}-{{}}", f"{label} {title}") for key, title in LAYOUTS for lang, label in LANGS]
    grid(src, out, font, rows, STATES, "sheet.png")
    rows = [(f"{lang}-{{}}", label) for lang, label in LANGS]
    grid(src, out, font, rows, [(s, s) for s in SINGLES], "sheet-setup.png")


if __name__ == "__main__":
    main()
