"""Compose the pedals of generated/Pedals.html into PNGs in generated/ (review outside the browser)."""
import json, re, sys
from pathlib import Path
from PIL import Image
D = Path(__file__).resolve().parent
data = json.loads(re.search(r"const DATA = (\{.*?\});\nconst", (D / "generated/Pedals.html").read_text(), re.S).group(1))
wood = Image.open(D / "generated/mod-ui/background.jpg").convert("RGBA")
variant, font, bevel = "B", "inter", "medium"

def pedal(p, bottom, frame, on=True):
    im = Image.open(D / "generated" / p["faces"][font][bevel][bottom]).convert("RGBA").copy()
    st = Image.open(D / "generated" / p["strips"][variant]).convert("RGBA"); side = st.size[1]
    for k in p["knobs"]:
        fr = st.crop((frame * side, 0, frame * side + side, side)).resize((k["size"], k["size"]), Image.Resampling.LANCZOS)
        im.alpha_composite(fr, (round(k["left"]), round(k["top"])))
    ft = Image.open(D / "generated" / p["footswitch"]).convert("RGBA").crop((0, 0 if on else 64, 64, 64 if on else 128))
    im.alpha_composite(ft, (p["foot"][bottom]["left"], p["foot"][bottom]["top"]))
    return im

for bottom in data["bottoms"]:
    canvas = wood.crop((0, 0, sum(p["w"] + 12 for p in data["pedals"]) + 10, 460)).copy(); x = 10
    for p in data["pedals"]:
        canvas.alpha_composite(pedal(p, bottom, 70, on=p["name"] != "siggen"), (x, 15)); x += p["w"] + 12
    canvas.save(D / "generated" / f"preview_{bottom}.png")
