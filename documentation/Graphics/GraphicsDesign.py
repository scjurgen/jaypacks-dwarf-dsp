#!/usr/bin/env python3
"""Design tool for the modgui graphics (exploration, not part of the build).

Renders pedals with the generator's own code (generator/graphics.py, generator/style.py,
the layout of generator/jpgen.py) plus design variants (knob, font, casing bevel, lower
area), measures the contrast of the information-bearing parts and writes interactive pages
into generated/ (gitignored), shown on the mod-ui wood background. See README.md.

Usage:
  .venv/bin/python3 documentation/Graphics/GraphicsDesign.py fetch             font and wood background
  .venv/bin/python3 documentation/Graphics/GraphicsDesign.py pedals [--quick]  generated/Pedals.html
  .venv/bin/python3 documentation/Graphics/GraphicsDesign.py colours           generated/Colours.html

pedals --quick renders only the generator's knob (variant B) and skips the comparison grids
and the accent table (fast iteration).
"""

import argparse
import colorsys
import copy
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "generator"))
import graphics as g  # noqa: E402
import jpgen  # noqa: E402
from style import STYLE, acrylic_colours, contrast, hex_of, rgb01, rgb255, signal_colour  # noqa: E402

GENERATED = HERE / "generated"
OUT = GENERATED / "images"
SHIPAHOY = REPO / "plugins/shipahoy/shipahoy.json"
GRAIN_TILE_MAX = 0.04   # std of the page's live grain overlay at slider maximum

# knob variants: where the value arc is (B is the generator's)
VARIANTS = {
    "B": {"label": "B: hand + value arc outside the rim", "arc": "track"},
    "A": {"label": "A: hand only, acrylic rim", "arc": None},
    "C": {"label": "C: hand + value arc inside the acrylic rim", "arc": "rim"},
}
DEFAULT_VARIANT = "B"

# casing bevel: width in face pixels and strength (medium is the generator's)
CASE_BEVELS = {
    "medium": {"label": "medium bevel", "bevel": (STYLE["case_bevel_width"], STYLE["case_bevel_strength"])},
    "subtle": {"label": "subtle bevel", "bevel": (8.0, 0.7)},
    "strong": {"label": "strong bevel", "bevel": (14.0, 1.3)},
}

FONTS = {
    "inter": {"label": "Inter SemiBold", "spec": g.FONT},
    "atkinson": {"label": "Atkinson Hyperlegible Bold",
                 "spec": (GENERATED / "fonts/atkinson/AtkinsonHyperlegible-Bold.ttf", None)},
}

# lower area (title-top is the generator's)
BOTTOMS = {
    "title-top": {"label": "divider, title, footswitch, brand"},
    "classic-swapped": {"label": "classic, title between lines, brand at the bottom"},
    "title-bottom": {"label": "divider, brand, footswitch, title"},
    "classic": {"label": "classic (brand between lines)"},
}


# ---------------------------------------------------------------- design variants of the face
def lower_layout(bottom, height=jpgen.FACE_HEIGHT):
    """Title and brand centre y, footswitch top and lines or divider per lower-area variant."""
    if bottom == "title-top":
        lay = jpgen.lower_layout()
        return {"divider": lay["divider_y"], "title_y": lay["title_y"], "foot_top": lay["footswitch_top"],
                "brand_y": lay["brand_y"]}
    if bottom == "classic":
        return {"lines": (height * 0.50, height * 0.63), "brand_y": height * 0.565, "foot_top": 293,
                "title_y": height * 0.905}
    if bottom == "classic-swapped":
        return {"lines": (height * 0.53, height * 0.66), "title_y": height * 0.595, "foot_top": 304,
                "brand_y": height * 0.92}
    top, end = jpgen.LOWER_AREA       # title-bottom: brand, footswitch, title spread evenly
    items = [jpgen.BRAND_HEIGHT, jpgen.FOOTSWITCH_SIZE, jpgen.TITLE_HEIGHT]
    gap = (end - top - sum(items)) / (len(items) + 1)
    brand = top + gap
    foot = brand + items[0] + gap
    title = foot + items[1] + gap
    return {"divider": jpgen.DIVIDER_Y, "brand_y": brand + items[0] / 2, "foot_top": round(foot),
            "title_y": title + items[2] / 2}


_CASINGS = {}


def render_face(desc, font_key, accent, bevel_key, bottom):
    """The face in a design variant; title-top with the generator's code."""
    spec, bevel = FONTS[font_key]["spec"], CASE_BEVELS[bevel_key]["bevel"]
    if bottom == "title-top":
        return g.render_face(desc, accent, spec, bevel)
    width, height = jpgen.face_size(len(jpgen.ui_controls(desc)))
    if (width, height, bevel_key) not in _CASINGS:
        _CASINGS[(width, height, bevel_key)] = g.casing(width, height, bevel).image()
    image = _CASINGS[(width, height, bevel_key)].copy()
    draw = ImageDraw.Draw(image)
    g.draw_labels(draw, desc, spec)
    OS, w = g.OS, width * g.OS
    text = rgb255(rgb01(STYLE["text"]))
    signal = rgb255(signal_colour(np.array(accent), STYLE["signal_target"], rgb01(STYLE["panel"])))
    lay = lower_layout(bottom, height)
    if "lines" in lay:
        y1, y2 = lay["lines"]
        line = round(5.5 * OS)
        draw.line((w / 2 - 10.5 * OS, y1 * OS, w - 5.5 * OS, y1 * OS), fill=signal, width=line)
        draw.line((6 * OS, y2 * OS, w / 2 + 10.5 * OS, y2 * OS), fill=signal, width=line)
        draw.text((w / 2, lay["brand_y"] * OS), jpgen.BRAND, font=g.font(24 * OS, spec), fill=text, anchor="mm")
    else:
        y = lay["divider"] * OS
        draw.line((26 * OS, y, w - 26 * OS, y), fill=rgb255(rgb01(STYLE["shaded_edge"]) * 0.72), width=round(1.3 * OS))
        draw.line((26 * OS, y + 1.3 * OS, w - 26 * OS, y + 1.3 * OS), fill=(255, 255, 255), width=round(1.0 * OS))
        by = lay["brand_y"] * OS
        total = g.tracked(draw, (w / 2, by), jpgen.BRAND.upper(), g.font(STYLE["brand_px"] * OS, spec), text,
                          STYLE["brand_tracking"])
        for sign in (-1, 1):
            xa = w / 2 + sign * (total / 2 + 8 * OS)
            draw.line((xa, by, xa + sign * 18 * OS, by), fill=signal, width=round(2.0 * OS))
    title = g.fitted(STYLE["title_px"] * OS, desc["displayname"], (width - 44) * OS, spec)
    draw.text((w / 2, lay["title_y"] * OS), desc["displayname"], font=title, fill=text, anchor="mm")
    return image.resize((width, height), Image.Resampling.BOX)


# ---------------------------------------------------------------- page helpers
def grain_tile(size=256, seed=11):
    """RGBA tile for the pages' live grain overlay: white or black pixels whose alpha adds
    +-d to the panel colour (d with std GRAIN_TILE_MAX); CSS opacity scales d linearly."""
    d = g.unit_grain(size, size, seed) * GRAIN_TILE_MAX
    b = float(np.mean(rgb01(STYLE["panel"])))
    alpha = np.where(d > 0, d / (1 - b), -d / b)
    white = (d > 0)[..., None] * np.ones(3)
    arr = np.dstack([white, np.clip(alpha, 0, 1)[..., None]])
    return Image.fromarray(np.uint8(arr * 255 + 0.5), "RGBA")


def grain_data(prefix):
    return {"tile": write(grain_tile(), f"{prefix}grain_tile.png"), "max": GRAIN_TILE_MAX,
            "knob_body": g.knob_geometry("track")["body"], "foot_body": 0.315}


def load(path):
    with open(path, encoding="ascii") as handle:
        return jpgen.validate(json.load(handle))


def with_face_controls(desc, count):
    desc = copy.deepcopy(desc)
    for k, port in enumerate(desc["ports-control"]):
        port["hasUI"] = 1 if k < count else 0
    desc["displayname"] = f"{desc['displayname']} {count}"
    return desc


def pedal_setups():
    names = ("meter", "probe", "shipahoy", "siggen")
    setups = [(name, load(REPO / "plugins" / name / f"{name}.json")) for name in names]
    ship = load(SHIPAHOY)
    setups += [(f"shipahoy{count}", with_face_controls(ship, count)) for count in (3, 4, 5, 6)]
    return setups


def write(image, name):
    image.save(OUT / name)
    return f"images/{name}"


def report_row(group, row):
    feature, adjacent, size, result, need = row
    return {"group": group, "feature": feature, "adjacent": adjacent, "size": size, "need": need,
            "result": None if result is None else {"ratio": round(result[0], 2), "a": result[1], "b": result[2]}}


def clean(prefix_colours):
    """Remove the images of one page: colours ("c_" prefix) or pedals (everything else)."""
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        if old.name.startswith("c_") == prefix_colours:
            old.unlink()


def write_page(template, data):
    page = (HERE / template).read_text(encoding="ascii")
    (GENERATED / template).write_text(page.replace("__DATA__", json.dumps(data)), encoding="ascii")


def check_fonts():
    missing = [str(spec["spec"][0]) for spec in FONTS.values() if not Path(spec["spec"][0]).exists()]
    if missing:
        raise SystemExit("design: fonts missing (" + ", ".join(missing) + "); run: GraphicsDesign.py fetch")


def page_style():
    """The pages overlay the grain live (Grain slider), so the images are rendered without."""
    STYLE["noise"] = 0.0


# ---------------------------------------------------------------- pedals page
def pedals(args):
    check_fonts()
    page_style()
    clean(prefix_colours=False)
    started = time.time()
    variants = [DEFAULT_VARIANT] if args.quick else list(VARIANTS)
    report, warnings = [], []
    strips, foots, sizes = {}, {}, {}
    setups = pedal_setups()
    accents = {}
    for _, desc in setups:
        accent = g.accent_of(desc)
        accents[hex_of(accent)[1:].lower()] = (accent, desc["color"])

    for key, (accent, name) in accents.items():
        for v in variants:
            knob = g.Knob(accent, VARIANTS[v]["arc"])
            strip = knob.strip()
            strips[(v, key)] = write(strip, f"strip_{v}_{key}.png")
            sizes[(v, key)] = (OUT / f"strip_{v}_{key}.png").stat().st_size
            clip = max(g.border_alpha(strip.crop((f * g.FRAME, 0, (f + 1) * g.FRAME, g.FRAME))) for f in (0, 50, 100))
            if clip > 2:
                warnings.append(f"knob {v}, accent {name}: alpha {clip} at the frame border (clipped)")
            report += [report_row(f"knob {v}, accent {name}", row) for row in g.measure_knob(knob)]
        fs = g.footswitch(accent)
        foots[key] = write(fs, f"foot_{key}.png")
        report += [report_row(f"footswitch, accent {name}", row) for row in g.measure_footswitch(fs)]

    pedals = []
    for name, desc in setups:
        count = len(jpgen.ui_controls(desc))
        width, height = jpgen.face_size(count)
        accent = g.accent_of(desc)
        key = hex_of(accent)[1:].lower()
        faces = {fk: {bk: {lk: write(render_face(desc, fk, accent, bk, lk), f"face_{name}_{fk}_{bk}_{lk}.png")
                           for lk in BOTTOMS} for bk in CASE_BEVELS} for fk in FONTS}
        knobs = [{"left": l, "top": t, "size": s} for l, t, s in jpgen.knob_positions(count, width)]
        foot = {lk: {"left": (width - g.FOOT) // 2, "top": lower_layout(lk, height)["foot_top"]} for lk in BOTTOMS}
        pedals.append({"name": name, "label": desc["displayname"], "colour": desc["color"], "w": width, "h": height,
                       "knobs": knobs, "foot": foot, "faces": faces, "strips": {v: strips[(v, key)] for v in variants},
                       "footswitch": foots[key]})
    for width in sorted(set(jpgen.FACE_WIDTHS.values())):
        report += [report_row(f"face text, width {width}", row) for row in g.measure_text(width, jpgen.FACE_HEIGHT)]

    grids, accent_rows = {}, []
    if not args.quick:
        ship_accent = g.accent_of(load(SHIPAHOY))
        saved = dict(STYLE)

        def grid_images(name, xkey, xs, ykey, ys):
            cells = []
            for yv in ys:
                row = []
                for xv in xs:
                    STYLE.update(saved)
                    STYLE[xkey], STYLE[ykey] = xv, yv
                    row.append({"src": write(g.Knob(ship_accent).frame(70), f"grid_{name}_{xv}_{yv}.png"),
                                "x": xv, "y": yv})
                cells.append(row)
            STYLE.update(saved)
            return {"xkey": xkey, "ykey": ykey, "rows": cells}

        grids["glow_saturation"] = grid_images("gs", "acrylic_glow", [0.45, 0.75, 1.0], "acrylic_saturation",
                                               [1.15, 1.45, 1.8])
        grids["halo_drop"] = grid_images("hd", "acrylic_halo", [0.15, 0.30, 0.50], "drop_opacity", [0.25, 0.40, 0.55])
        track = rgb01(STYLE["track"])
        for cname, chex in jpgen.COLORS.items():
            accent = rgb01(chex)
            knob = g.Knob(accent)
            accent_rows.append({"name": cname, "hex": chex.upper(), "signal": hex_of(knob.signal),
                                "raw": round(contrast(accent, track), 2), "sig": round(contrast(knob.signal, track), 2),
                                "altered": hex_of(knob.signal) != chex.upper(),
                                "src": write(knob.frame(70), f"accent_{cname}.png")})

    data = {
        "variants": {v: VARIANTS[v]["label"] for v in variants},
        "fonts": {k: v["label"] for k, v in FONTS.items()},
        "bevels": {k: v["label"] for k, v in CASE_BEVELS.items()},
        "bottoms": {k: v["label"] for k, v in BOTTOMS.items()},
        "pedals": pedals, "grids": grids, "accents": accent_rows, "report": report, "warnings": warnings,
        "strip_sizes": {f"{v}/{k}": s for (v, k), s in sizes.items()},
        "frames": g.FRAMES, "stamp": int(time.time()), "grain": grain_data(""),
    }
    write_page("Pedals.html", data)
    print(f"design: wrote {GENERATED / 'Pedals.html'} in {time.time() - started:.1f} s")
    for w in warnings:
        print("design: warning:", w)
    for r in report:
        if r["result"] is None or r["result"]["ratio"] < r["need"]:
            if not (r["size"] == 30 and r["feature"].startswith("rim outer edge")):
                print("design: below target:", r["group"], r["feature"], r["size"], r["result"])


# ---------------------------------------------------------------- colours page
HUES = [("Red", 0), ("Orange", 30), ("Yellow", 60), ("Lime", 90), ("Green", 120), ("Spring", 150),
        ("Cyan", 180), ("Azure", 210), ("Blue", 240), ("Violet", 270), ("Magenta", 300), ("Rose", 330)]
LEVELS = [("pure", 1.0, 0.50), ("deep", 1.0, 0.40), ("dark", 1.0, 0.30)]   # name, HLS saturation, lightness


def colours(args):
    """Base colours in 30 degree hue steps at three lightness levels, each as the accent of a
    two-knob pedal, plus the current jpgen.COLORS with their nearest base hue."""
    check_fonts()
    page_style()
    clean(prefix_colours=True)
    started = time.time()
    ship = load(SHIPAHOY)
    track = rgb01(STYLE["track"])
    rows = []
    for level, sat, light in LEVELS:
        row = []
        for name, hue in HUES:
            accent = np.array(colorsys.hls_to_rgb(hue / 360.0, light, sat))
            key = f"{level}_{name.lower()}"
            knob = g.Knob(accent)
            desc = copy.deepcopy(ship)
            desc["displayname"] = name
            width, height = jpgen.face_size(2)
            worst = {}
            for feature, _, size, result, need in g.measure_knob(knob):
                if size in (60, 30) and feature != "rim outer edge (boundary)":
                    ratio = 0.0 if result is None else result[0]
                    worst[size] = min(worst.get(size, 99.0), ratio / need)
            fs = g.footswitch(accent)
            foot_ok = all(r[3] is not None and r[3][0] >= r[4] for r in g.measure_footswitch(fs))
            row.append({
                "name": name, "hue": hue, "level": level, "hex": hex_of(accent),
                "signal": hex_of(knob.signal), "core": hex_of(acrylic_colours(accent)[0]),
                "raw": round(contrast(accent, track), 2), "sig": round(contrast(knob.signal, track), 2),
                "altered": hex_of(knob.signal) != hex_of(accent),
                "knob_ok": {str(k): v >= 1.0 for k, v in worst.items()}, "foot_ok": foot_ok,
                "w": width, "h": height,
                "knobs": [{"left": l, "top": t, "size": s} for l, t, s in jpgen.knob_positions(2, width)],
                "foot": {"left": (width - g.FOOT) // 2, "top": jpgen.lower_layout()["footswitch_top"]},
                "face": write(g.render_face(desc, accent), f"c_face_{key}.png"),
                "strip": write(knob.strip(), f"c_strip_{key}.png"), "footswitch": write(fs, f"c_foot_{key}.png"),
            })
        rows.append({"level": level, "saturation": sat, "lightness": light, "pedals": row})
    current = []
    for cname, chex in jpgen.COLORS.items():
        hue, light, sat = colorsys.rgb_to_hls(*rgb01(chex))
        current.append({"name": cname, "hex": chex.upper(), "hue": round(hue * 360), "lightness": round(light, 2),
                        "saturation": round(sat, 2), "nearest": HUES[int(round(hue * 12)) % 12][0]})
    write_page("Colours.html", {"rows": rows, "current": current, "stamp": int(time.time()),
                                "grain": grain_data("c_")})
    print(f"design: wrote {GENERATED / 'Colours.html'} in {time.time() - started:.1f} s")


# ---------------------------------------------------------------- fetch
FONT_URLS = {
    "atkinson/AtkinsonHyperlegible-Bold.ttf":
        "https://raw.githubusercontent.com/google/fonts/main/ofl/atkinsonhyperlegible/AtkinsonHyperlegible-Bold.ttf",
    "atkinson/OFL.txt": "https://raw.githubusercontent.com/google/fonts/main/ofl/atkinsonhyperlegible/OFL.txt",
}


def fetch(args):
    """The comparison font (Atkinson Hyperlegible, OFL, from the Google Fonts repository;
    Inter is in generator/assets/) and mod-ui's wood background from the device (ssh,
    DWARF_HOST, default 192.168.51.1); both stay in generated/."""
    for relative, url in FONT_URLS.items():
        target = GENERATED / "fonts" / relative
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            target.write_bytes(response.read())
        print(f"design: fetched {target}")
    wood = GENERATED / "mod-ui" / "background.jpg"
    if not wood.exists():
        host = os.environ.get("DWARF_HOST", "192.168.51.1")
        result = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", f"root@{host}",
                                 "cat /usr/share/mod/html/img/background.jpg"], capture_output=True)
        if result.returncode == 0 and result.stdout:
            wood.parent.mkdir(parents=True, exist_ok=True)
            wood.write_bytes(result.stdout)
            print(f"design: fetched {wood}")
        else:
            print("design: no wood background (device not reachable); the pages fall back to #111")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("fetch", help="comparison font and the mod-ui wood background into generated/")
    ped = sub.add_parser("pedals", help="generated/Pedals.html")
    ped.add_argument("--quick", action="store_true")
    sub.add_parser("colours", help="generated/Colours.html")
    args = parser.parse_args()
    {"fetch": fetch, "pedals": pedals, "colours": colours}[args.command](args)


if __name__ == "__main__":
    main()
