import csv
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path(__file__).resolve().parent
rows = {0: [], 1: []}
with (OUT_DIR / "sigmaq_dsp_response.csv").open(newline="") as f:
    for r in csv.DictReader(f):
        rows[int(r["type"])].append((float(r["hz"]), float(r["predicted_db"]), float(r["measured_db"])))

img = Image.new("RGB", (1400, 620), "#10151f")
draw = ImageDraw.Draw(img)
def load_font(size, bold=False):
    names = ("DejaVuSans-Bold.ttf", "arialbd.ttf") if bold else ("DejaVuSans.ttf", "arial.ttf")
    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


font = load_font(17)
small = load_font(14)
title = load_font(27, bold=True)
subtitle = load_font(19, bold=True)

draw.text((50, 24), "Sigma-Q DSP response: predicted vs measured", fill="#e8edf5", font=title)
draw.text((50, 64), "44.1 kHz  |  center 1 kHz  |  Q 18  |  gain +30 dB  |  standalone SVF engine", fill="#aebccc", font=font)

def panel(x0, x1, typ, name, ymin, ymax):
    left, right = x0 + 68, x1 - 20
    top, bottom = 154, 525
    draw.text((x0 + 20, 112), name, fill="#e8edf5", font=subtitle)
    def xy(hz, db):
        x = left + (math.log10(hz) - math.log10(20)) / 3 * (right - left)
        y = bottom - (db - ymin) / (ymax - ymin) * (bottom - top)
        return int(x), int(y)
    for hz in (20, 100, 1000, 10000, 20000):
        x, _ = xy(hz, ymin)
        draw.line((x, top, x, bottom), fill="#283344", width=1)
        label = f"{int(hz/1000)}k" if hz >= 1000 else str(hz)
        draw.text((x-12, bottom+11), label, fill="#aebccc", font=small)
    step = 5 if typ == 0 else 10
    for db in range(int(ymin), int(ymax)+1, step):
        _, y = xy(20, db)
        draw.line((left, y, right, y), fill="#283344", width=1)
        draw.text((x0+20, y-8), str(db), fill="#aebccc", font=small)
    predicted = [xy(hz, a) for hz, a, m in rows[typ]]
    draw.line(predicted, fill="#61b8ff", width=3, joint="curve")
    for i, (hz, a, m) in enumerate(rows[typ]):
        if i % 8 == 0 or i == len(rows[typ])-1:
            x, y = xy(hz, m)
            draw.ellipse((x-3,y-3,x+3,y+3), fill="#ffb454")
    error = max(abs(a-m) for _, a, m in rows[typ] if a > -80)
    draw.text((x0+20, 563), f"Max error in plotted sweep: {error:.4f} dB", fill="#aebccc", font=small)

panel(24, 690, 0, "Bell", -5, 35)
panel(710, 1375, 1, "Low shelf", -25, 65)
draw.line((1170, 120, 1207, 120), fill="#61b8ff", width=3)
draw.text((1215, 109), "Predicted", fill="#cdd8e5", font=small)
draw.ellipse((1167,139,1173,145), fill="#ffb454")
draw.text((1215, 133), "Measured", fill="#cdd8e5", font=small)

path = OUT_DIR / "sigmaq_dsp_response.png"
img.save(path)
print(path)
