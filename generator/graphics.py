#!/usr/bin/env python3
"""Render the modgui images of one plugin from its JSON description.

Writes into <bundle-dir>/modgui/:
  face.png                  milky casing, knob labels, divider, title, brand
  knobs/boxy/white.png      knob filmstrip: 101 square frames of 191 px, minimum to maximum
  pedals/onoff_white.png    footswitch 64 x 128: top frame bypassed, bottom frame active
  screenshot.png            face with jacks, knobs and footswitch (mod-ui plugin lists)
  thumbnail.png             the screenshot at 64 px height
Files are rewritten only when their content changes. jpgen.py --modgui writes the matching
modgui.ttl, icon template and stylesheet; the paths of the filmstrip and the footswitch are
the ones that stylesheet references.

The look (tokens and parameters in style.py) was worked out with documentation/Graphics/:
light from the upper left, fixed for every frame (only the hand and the value arc move), a
glowing acrylic rim in the plugin colour, information-bearing parts in colours that reach a
contrast target. The knob positions come from jpgen.knob_positions(), the same spots the
stylesheet puts the knobs. Needs numpy, Pillow and the Inter font in
generator/assets/fonts/inter/ (SIL Open Font License, OFL.txt).

Usage:
  graphics.py PLUGIN_JSON --bundle-dir DIR
"""

import argparse
import io
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpgen  # noqa: E402
from style import STYLE, acrylic_colours, contrast, hex_of, rgb01, rgb255, signal_colour  # noqa: E402

ASSETS = Path(__file__).resolve().parent / "assets"
FONT = (ASSETS / "fonts" / "inter" / "Inter.ttf", "SemiBold")   # path, named variation
OS = 4                  # oversampling, downsampled once with a box filter
FRAME = 191             # knob frame side
FRAMES = 101
SWEEP = 150.0           # degrees left and right of 12 o'clock
FOOT = jpgen.FOOTSWITCH_SIZE
FOOT_SYMBOL = (0.135, 0.052)    # footswitch symbol radius and stroke, fractions of the frame
THUMBNAIL_HEIGHT = 64
STRIP = "knobs/boxy/white.png"
FOOTSWITCH = "pedals/onoff_white.png"


class GraphicsError(Exception):
    """A resource (font, asset) is missing."""


# ---------------------------------------------------------------- canvas and shapes
class Canvas:
    """Straight-alpha RGBA in floats, composited with "over"."""

    def __init__(self, h, w):
        self.rgb = np.zeros((h, w, 3))
        self.a = np.zeros((h, w))

    def copy(self):
        c = Canvas(1, 1)
        c.rgb, c.a = self.rgb.copy(), self.a.copy()
        return c

    def over(self, rgb, alpha):
        alpha = np.clip(alpha, 0.0, 1.0)
        rgb = np.broadcast_to(rgb, self.rgb.shape)
        out = alpha + self.a * (1.0 - alpha)
        num = rgb * alpha[..., None] + self.rgb * (self.a * (1.0 - alpha))[..., None]
        self.rgb = np.where(out[..., None] > 0, num / np.maximum(out, 1e-12)[..., None], 0.0)
        self.a = out

    def image(self):
        arr = np.dstack([np.clip(self.rgb, 0, 1), np.clip(self.a, 0, 1)[..., None]])
        return Image.fromarray(np.uint8(arr * 255.0 + 0.5), "RGBA")


def grid(n):
    """Pixel centres of an n x n frame in units of the frame side, origin in the middle."""
    a = (np.arange(n) + 0.5) / n - 0.5
    return np.meshgrid(a, a)


def blur(mask, sigma_px):
    img = Image.fromarray(np.uint8(np.clip(mask, 0, 1) * 255))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(sigma_px)), float) / 255.0


def lighten(rgb, k, dark_scale=0.90):
    """k > 0 towards white, k < 0 towards a darkened shaded-edge colour."""
    k = k[..., None]
    dark = rgb01(STYLE["shaded_edge"]) * dark_scale
    return np.where(k >= 0, rgb + k * (1.0 - rgb), rgb + (-k) * (dark - rgb))


def unit_grain(h, w, seed):
    """Seeded normal noise with std 1, softened by a small wrap-around blur (tileable)."""
    rng = np.random.default_rng(seed)
    small = rng.standard_normal((h, w))
    sigma = STYLE["noise_blur"]
    if sigma > 0:
        taps = np.arange(-math.ceil(3 * sigma), math.ceil(3 * sigma) + 1)
        kernel = np.exp(-taps ** 2 / (2 * sigma ** 2))
        kernel /= kernel.sum()
        for axis in (0, 1):
            small = sum(weight * np.roll(small, int(t), axis=axis) for t, weight in zip(taps, kernel))
    return small / max(small.std(), 1e-9)


def grain(h, w, pixel, seed):
    """Additive luminance noise for RGB in 0..1, one value per `pixel` oversampled pixels
    (one display pixel), std STYLE["noise"]."""
    if STYLE["noise"] <= 0:
        return np.zeros((h, w))
    small = unit_grain(math.ceil(h / pixel), math.ceil(w / pixel), seed)
    return np.kron(small, np.ones((pixel, pixel)))[:h, :w] * STYLE["noise"]


def light_dir():
    lx, ly = STYLE["light"]
    n = math.hypot(lx, ly)
    return lx / n, ly / n


def angle_deg(x, y):
    """0 at 12 o'clock, clockwise positive."""
    return np.degrees(np.arctan2(x, -y))


def band(r, inner, outer):
    return (r >= inner) & (r <= outer)


def arc_mask(x, y, inner, outer, start, end):
    """Band between the radii from angle start to end with round caps."""
    r = np.hypot(x, y)
    th = angle_deg(x, y)
    mask = band(r, inner, outer) & (th >= start) & (th <= end)
    mid, half = (inner + outer) / 2, (outer - inner) / 2
    for a in (start, end):
        cx, cy = mid * math.sin(math.radians(a)), -mid * math.cos(math.radians(a))
        mask |= np.hypot(x - cx, y - cy) <= half
    return mask


def segment_mask(x, y, angle, r0, r1, width):
    """Round-capped line along `angle` from radius r0 to r1 (r0 may be negative)."""
    dx, dy = math.sin(math.radians(angle)), -math.cos(math.radians(angle))
    t = np.clip(x * dx + y * dy, r0, r1)
    return np.hypot(x - t * dx, y - t * dy) <= width / 2


def composite_on(image, colour):
    """RGB in 0..1 of `image` composited on the hex colour."""
    bg = Image.new("RGBA", image.size, rgb255(rgb01(colour)) + (255,))
    return np.asarray(Image.alpha_composite(bg, image).convert("RGB"), float) / 255.0


# ---------------------------------------------------------------- knob
def knob_geometry(arc):
    """Radii (fractions of the frame side): value-arc track, acrylic rim, body. arc "track":
    value arc outside the rim (the design); "rim": inside the rim; None: hand only."""
    if arc == "track":
        return {"track": (0.412, 0.486), "rim": (0.318, 0.392), "body": 0.318}
    return {"track": None, "rim": (0.318, 0.392), "body": 0.318}


def satin_body(cv, x, y, r, radius, colour):
    """Satin cap: broad gradient, rounded bevel lit from the upper left, soft highlight,
    grain at display-pixel size (the 191 px strip is shown at 60 px, the footswitch at 64)."""
    lx, ly = light_dir()
    body = r <= radius
    base = np.broadcast_to(rgb01(colour), cv.rgb.shape).copy()
    k = STYLE["face_gradient"] * (x * lx + y * ly) / radius
    t = np.clip((r - (radius - STYLE["bevel_width"])) / STYLE["bevel_width"], 0, 1)
    slope = np.sin(np.pi / 2 * t)
    lit = (x * lx + y * ly) / np.maximum(r, 1e-9)
    k = k + STYLE["bevel_strength"] * slope * lit - STYLE["case_occlusion"] * slope ** 3
    spot = np.exp(-((x + 0.35 * radius) ** 2 + (y + 0.35 * radius) ** 2) / (2 * (0.30 * radius) ** 2))
    k = k + STYLE["satin_spot"] * spot
    n = cv.a.shape[0]
    rgb = lighten(base, k) + grain(n, n, OS * max(1, round(n / OS / 60)), seed=7)[..., None]
    cv.over(np.clip(rgb, 0, 1), body.astype(float))


def acrylic_ring(cv, x, y, r, inner, outer, accent, opacity=None, glow=True):
    """Tinted acrylic ring lit from inside: bright saturated core, coloured halo, narrow
    upper-left reflection; glow=False: flat translucent tint. The outer and inner edges are
    opaque, in the accent darkened to edge_target against the casing in the knob's shadow:
    they are the knob boundary, also for light hues."""
    lx, ly = light_dir()
    n = cv.a.shape[0]
    accent = np.array(accent)
    ring = band(r, inner, outer)
    mid, half = (inner + outer) / 2, (outer - inner) / 2
    core, edge = acrylic_colours(accent)
    if glow and STYLE["acrylic_halo"] > 0:
        halo = blur(ring.astype(float), STYLE["acrylic_halo_blur"] * n) * STYLE["acrylic_halo"]
        cv.over(core, np.where(ring, 0, halo))
    if glow:
        u = np.clip((r - mid) / half, -1, 1)
        intensity = (1.0 - u ** 2) ** 1.5
        colour = edge[None, None, :] + (core - edge)[None, None, :] * intensity[..., None]
        alpha_body = STYLE["acrylic_opacity"] if opacity is None else opacity
    else:
        colour = np.broadcast_to(accent, cv.rgb.shape).copy()
        alpha_body = opacity
    shadowed = rgb01(STYLE["panel"]) * STYLE["shadowed_casing"]
    edge_colour = signal_colour(accent, STYLE["edge_target"], shadowed)
    d = np.minimum(r - inner, outer - r)
    e = np.where(ring, np.clip(1.0 - d / STYLE["edge_width"], 0, 1), 0)
    w = np.clip(e * 2.0, 0, 1)[..., None]
    colour = colour * (1.0 - w) + edge_colour * w
    lit = (x * lx + y * ly) / np.maximum(r, 1e-9)
    colour = colour + 0.08 * np.clip(lit, 0, 1)[..., None] * (1 - colour)
    alpha = np.where(ring, alpha_body + (1 - alpha_body) * np.minimum(1.0, e * 2.0), 0)
    cv.over(colour, alpha)
    th = angle_deg(x, y)
    window = np.clip(np.cos(np.radians((th + 45.0) * 2.2)), 0, 1)
    window = np.where(np.abs(th + 45.0) < 41, window, 0)
    rad = np.exp(-((r - mid) ** 2) / (2 * (STYLE["reflection_width"] / 2) ** 2))
    cv.over(np.ones(3), np.where(ring, STYLE["reflection"] * window * rad, 0))


def contact_shadow(cv, x, y, r_outer, n):
    off = STYLE["shadow_offset"]
    disc = (np.hypot(x - off, y - off) <= r_outer).astype(float)
    cv.over(np.zeros(3), blur(disc, STYLE["shadow_blur"] * n) * STYLE["shadow_opacity"])


def drop_shadow(cv, x, y, r_outer, n, scale=1.0):
    """Soft grey shadow towards the lower right; `scale` shrinks offset and blur where the
    frame has less room (footswitch)."""
    off = STYLE["drop_offset"] * scale / math.sqrt(2)
    disc = (np.hypot(x - off, y - off) <= r_outer).astype(float)
    cv.over(rgb01("#3A3F42"), blur(disc, STYLE["drop_blur"] * scale * n) * STYLE["drop_opacity"])


class Knob:
    """Knob filmstrip of one accent colour. The static layers (track, shadows, acrylic rim,
    satin body) are rendered once; per frame only the value arc and the hand are drawn on
    top, so the lighting does not turn with the value."""

    def __init__(self, accent, arc="track"):
        self.arc = arc
        self.accent = np.array(accent)
        self.n = FRAME * OS
        self.x, self.y = grid(self.n)
        self.r = np.hypot(self.x, self.y)
        g = knob_geometry(arc)
        self.g = g
        cv = Canvas(self.n, self.n)
        if g["track"]:
            cv.over(rgb01(STYLE["track"]), arc_mask(self.x, self.y, *g["track"], -SWEEP, SWEEP).astype(float))
        drop_shadow(cv, self.x, self.y, g["rim"][1], self.n)
        contact_shadow(cv, self.x, self.y, g["rim"][1], self.n)
        if arc == "rim":
            acrylic_ring(cv, self.x, self.y, self.r, *g["rim"], self.accent, STYLE["rim_opacity_c"], glow=False)
        else:
            acrylic_ring(cv, self.x, self.y, self.r, *g["rim"], self.accent)
        satin_body(cv, self.x, self.y, self.r, g["body"], STYLE["knob_face"])
        self.static = cv
        self.signal = signal_colour(self.accent, STYLE["signal_target"], rgb01(STYLE["track"]))
        if arc is not None:
            # the value arc's colour reaches the target against what it actually sits on
            under = np.median(composite_on(cv.image(), STYLE["panel"])[self.arc_region()], axis=0)
            self.signal = signal_colour(self.accent, STYLE["signal_target"], under)

    def arc_region(self):
        """Where the value arc can be: the whole track, or the rim without its edges."""
        g = self.g
        if self.arc == "rim":
            inset = STYLE["edge_width"] * 0.9
            return band(self.r, g["rim"][0] + inset, g["rim"][1] - inset)
        return arc_mask(self.x, self.y, *g["track"], -SWEEP, SWEEP)

    def value_angle(self, frame):
        return -SWEEP + 2 * SWEEP * frame / (FRAMES - 1)

    def masks(self, frame):
        """(hand, value arc or None) of a frame, oversampled."""
        a = self.value_angle(frame)
        g = self.g
        hand = segment_mask(self.x, self.y, a, 0.06, g["body"] - 0.035, STYLE["hand_width"])
        arc = None
        if self.arc == "track":
            ti, to = g["track"]
            arc = arc_mask(self.x, self.y, ti + 0.004, to - 0.004, -SWEEP, a)
        elif self.arc == "rim":
            ri, ro = g["rim"]
            inset = STYLE["edge_width"] * 0.9
            arc = arc_mask(self.x, self.y, ri + inset, ro - inset, -SWEEP, a)
        return hand, arc

    def frame_canvas(self, frame):
        cv = self.static.copy()
        hand, arc = self.masks(frame)
        if arc is not None:
            cv.over(self.signal, arc.astype(float))
        cv.over(rgb01(STYLE["hand"]), hand.astype(float))
        return cv

    def frame(self, frame):
        return self.frame_canvas(frame).image().resize((FRAME, FRAME), Image.Resampling.BOX)

    def strip(self):
        strip = Image.new("RGBA", (FRAME * FRAMES, FRAME), 0)
        for f in range(FRAMES):
            strip.paste(self.frame(f), (f * FRAME, 0))
        return strip


def border_alpha(image):
    """Largest alpha (0..255) in the outermost pixel ring: more than 2 means a clipped
    shadow or glow."""
    a = np.asarray(image.getchannel("A"))
    return int(max(a[0].max(), a[-1].max(), a[:, 0].max(), a[:, -1].max()))


# ---------------------------------------------------------------- footswitch
def power_symbol(x, y, radius, width):
    """The classic power symbol: interrupted circle with a bar from the top (bypassed)."""
    r = np.hypot(x, y)
    th = angle_deg(x, y)
    ring = band(r, radius - width / 2, radius + width / 2) & (np.abs(th) >= 38)
    for a in (38, -38):
        cx, cy = radius * math.sin(math.radians(a)), -radius * math.cos(math.radians(a))
        ring |= np.hypot(x - cx, y - cy) <= width / 2
    return ring | segment_mask(x, y, 0.0, 0.0, radius * 1.15, width)


def active_symbol(x, y, radius, width):
    """Closed circle with a short centred vertical line (active)."""
    ring = band(np.hypot(x, y), radius - width / 2, radius + width / 2)
    return ring | segment_mask(x, y, 0.0, -radius * 0.5, radius * 0.5, width)


def footswitch_symbols(x, y):
    """(bypassed, active) symbol masks."""
    return power_symbol(x, y, *FOOT_SYMBOL), active_symbol(x, y, *FOOT_SYMBOL)


def footswitch(accent):
    """64 x 128 as mod-ui uses it: the element has class "on" while the plugin is bypassed
    (top frame), "off" while it is active (bottom frame). Bypassed: unlit pale ring with a
    grey outline, the interrupted power symbol in grey. Active: glowing acrylic ring, the
    closed circle with a line in the accent darkened to symbol_target. The state reads from
    the symbol's shape, not from colour alone."""
    accent = np.array(accent)
    n = FOOT * OS
    x, y = grid(n)
    r = np.hypot(x, y)
    bypassed_symbol, active = footswitch_symbols(x, y)
    symbol_colour = signal_colour(accent, STYLE["symbol_target"], rgb01(STYLE["knob_face"]))
    image = Image.new("RGBA", (FOOT, 2 * FOOT), 0)
    for row, state in enumerate(("bypassed", "active")):
        cv = Canvas(n, n)
        drop_shadow(cv, x, y, 0.40, n, scale=0.8)
        contact_shadow(cv, x, y, 0.40, n)
        if state == "active":
            acrylic_ring(cv, x, y, r, 0.315, 0.40, accent)
        else:
            cv.over(rgb01(STYLE["off_ring"]), band(r, 0.315, 0.40).astype(float))
            cv.over(rgb01(STYLE["off_ring_edge"]), band(r, 0.40 - STYLE["edge_width"] * 1.6, 0.40).astype(float))
        satin_body(cv, x, y, r, 0.315, STYLE["knob_face"])
        if state == "active":
            cv.over(symbol_colour, active.astype(float))
        else:
            cv.over(rgb01(STYLE["off_ring_edge"]), bypassed_symbol.astype(float))
        image.paste(cv.image().resize((FOOT, FOOT), Image.Resampling.BOX), (0, row * FOOT))
    return image


# ---------------------------------------------------------------- face
def font(px, spec=None):
    """Inter SemiBold (or `spec` = (path, named variation or None)) at px pixels."""
    path, variation = spec or FONT
    try:
        f = ImageFont.truetype(str(path), px)
    except OSError as error:
        raise GraphicsError(f"font {path} not found ({error})")
    if variation:
        f.set_variation_by_name(variation)
    return f


def accent_of(desc):
    """The plugin colour ("color": a name from jpgen.COLORS or #rrggbb), RGB in 0..1."""
    return rgb01(jpgen.COLORS.get(desc["color"], desc["color"]))


def labels(desc, spec=None):
    """(text, centre x, top y, px) per face control: centred under its knob, one size per
    pedal, the largest at which every label fits between its neighbours."""
    ui = jpgen.ui_controls(desc)
    width = jpgen.face_size(len(ui))[0]
    knobs = jpgen.knob_positions(len(ui), width)
    distances = [b[0] - a[0] for a, b in zip(knobs, knobs[1:]) if a[1] == b[1]]
    room = (min(distances) if distances else width - 2 * 20) - 2
    px = STYLE["label_px"]
    while px > STYLE["label_min_px"] and any(font(px, spec).getlength(p["display"]) > room for p in ui):
        px -= 1
    return [(p["display"], left + size / 2, top + size + 2, px) for p, (left, top, size) in zip(ui, knobs)]


def casing(width, height, bevel=None):
    """Milky casing, oversampled: rounded box (radius 60 px as the stylesheet clips it),
    quarter-round perimeter bevel lit from the upper left, broad gradient, grain, a crisp
    outer line for the dark pedalboard. `bevel`: (width px, strength), default from STYLE."""
    bw, strength = bevel or (STYLE["case_bevel_width"], STYLE["case_bevel_strength"])
    w, h = width * OS, height * OS
    radius = 60.0 * OS
    ys, xs = np.mgrid[0:h, 0:w] + 0.5
    qx, qy = np.abs(xs - w / 2) - (w / 2 - radius), np.abs(ys - h / 2) - (h / 2 - radius)
    sdf = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - radius
    depth = -sdf / OS                      # face pixels inside the edge
    gy, gx = np.gradient(sdf)
    gn = np.maximum(np.hypot(gx, gy), 1e-9)
    lx, ly = light_dir()
    lit = (gx / gn) * lx + (gy / gn) * ly
    slope = np.cos(np.pi / 2 * np.clip(depth / bw, 0, 1))
    k = strength * slope * lit - STYLE["case_occlusion"] * slope ** 3
    k = k + STYLE["case_gradient"] * ((xs / w - 0.5) * lx + (ys / h - 0.5) * ly) * 2
    base = np.broadcast_to(rgb01(STYLE["panel"]), (h, w, 3))
    rgb = np.clip(lighten(base, k, dark_scale=0.78) + grain(h, w, OS, seed=3)[..., None], 0, 1)
    rgb = np.where((depth < 1.0)[..., None], rgb * 0.80, rgb)
    cv = Canvas(h, w)
    cv.over(rgb, (depth >= 0).astype(float))
    return cv


def tracked(draw, xy, text, fnt, fill, tracking):
    """Text centred at xy (vertically by its middle) with extra letter spacing; returns
    its width."""
    x, y = xy
    extra = fnt.size * tracking
    widths = [fnt.getlength(c) for c in text]
    total = sum(widths) + extra * (len(text) - 1)
    cx = x - total / 2
    for c, cw in zip(text, widths):
        draw.text((cx, y), c, font=fnt, fill=fill, anchor="lm")
        cx += cw + extra
    return total


def fitted(px, text, room, spec=None):
    """The font at px, or smaller until `text` fits into `room` pixels."""
    while px > 12 and font(px, spec).getlength(text) > room:
        px -= 1
    return font(px, spec)


def draw_labels(draw, desc, spec=None):
    text = rgb255(rgb01(STYLE["text"]))
    for label, x, y, px in labels(desc, spec):
        draw.text((x * OS, y * OS), label, font=font(px * OS, spec), fill=text, anchor="ma")


def draw_lower_area(draw, desc, width, accent, spec=None):
    """Engraved divider, title, brand in spaced capitals between two short accent lines."""
    lay = jpgen.lower_layout()
    w = width * OS
    text = rgb255(rgb01(STYLE["text"]))
    signal = rgb255(signal_colour(np.array(accent), STYLE["signal_target"], rgb01(STYLE["panel"])))
    y = lay["divider_y"] * OS
    x0, x1 = 26 * OS, w - 26 * OS
    draw.line((x0, y, x1, y), fill=rgb255(rgb01(STYLE["shaded_edge"]) * 0.72), width=round(1.3 * OS))
    draw.line((x0, y + 1.3 * OS, x1, y + 1.3 * OS), fill=(255, 255, 255), width=round(1.0 * OS))
    title = fitted(STYLE["title_px"] * OS, desc["displayname"], (width - 44) * OS, spec)
    draw.text((w / 2, lay["title_y"] * OS), desc["displayname"], font=title, fill=text, anchor="mm")
    by = lay["brand_y"] * OS
    total = tracked(draw, (w / 2, by), jpgen.BRAND.upper(), font(STYLE["brand_px"] * OS, spec), text,
                    STYLE["brand_tracking"])
    for sign in (-1, 1):
        xa = w / 2 + sign * (total / 2 + 8 * OS)
        draw.line((xa, by, xa + sign * 18 * OS, by), fill=signal, width=round(2.0 * OS))


def render_face(desc, accent=None, spec=None, bevel=None):
    """The pedal face: casing, knob labels and the lower area."""
    width, height = jpgen.face_size(len(jpgen.ui_controls(desc)))
    accent = accent_of(desc) if accent is None else accent
    image = casing(width, height, bevel).image()
    draw = ImageDraw.Draw(image)
    draw_labels(draw, desc, spec)
    draw_lower_area(draw, desc, width, accent, spec)
    return image.resize((width, height), Image.Resampling.BOX)


# ---------------------------------------------------------------- screenshot
def open_image(path):
    try:
        return Image.open(path)
    except OSError as error:
        raise GraphicsError(f"cannot read {path} ({error})")


# first jack row and the distance between rows in the screenshot (jack height 56 + margin 13, pedals.css)
JACK_TOP = 55
JACK_PITCH = 69


def knob_frame_index(count, k):
    """Filmstrip frame of knob k in the screenshot (createPreviewImage.py)."""
    return 50 if count in (3, 4) else 100 - k * 10


def render_screenshot(desc, face, strip, foot):
    """Face with input jacks left, output jacks right, the knobs of the face controls and
    the footswitch in the active state."""
    plugs = {name: open_image(ASSETS / f"{name}.png").convert("RGBA")
             for name in ("audioInput", "audioOutput", "midiInput", "midiOutput")}
    width, height = face.size
    pad = max(plug.size[0] for plug in plugs.values())
    image = Image.new("RGBA", (width + 2 * pad, height), 0)
    image.paste(face, (pad, 0), face)
    # the jacks as mod-ui draws them (img/audio-input.png etc. of the device, cropped to the jack): 56 px
    # high, 13 px apart; the MIDI jack, if any, comes after the audio jacks (as in icon.html)
    kinds = (("audioInput", len(desc["audio-ports-in"])), ("midiInput", 1 if desc.get("midi-in") else 0))
    for row, name in enumerate(name for name, count in kinds for _ in range(count)):
        image.alpha_composite(plugs[name], (pad - plugs[name].size[0], JACK_TOP + row * JACK_PITCH))
    kinds = (("audioOutput", len(desc["audio-ports-out"])), ("midiOutput", 1 if desc.get("midi-out") else 0))
    for row, name in enumerate(name for name, count in kinds for _ in range(count)):
        image.alpha_composite(plugs[name], (width + pad, JACK_TOP + row * JACK_PITCH))
    active = foot.crop((0, FOOT, FOOT, 2 * FOOT))
    image.paste(active, (pad + (width - FOOT) // 2, jpgen.lower_layout()["footswitch_top"]), active)
    count = len(jpgen.ui_controls(desc))
    for k, (left, top, size) in enumerate(jpgen.knob_positions(count, width)):
        frame = knob_frame_index(count, k)
        knob = strip.crop((frame * FRAME, 0, frame * FRAME + FRAME, FRAME))
        knob = knob.resize((size, size), resample=Image.Resampling.LANCZOS)
        image.paste(knob, (round(left) + pad, round(top)), knob)
    return image


def render_thumbnail(screenshot):
    width, height = screenshot.size
    size = (round(THUMBNAIL_HEIGHT * width / height), THUMBNAIL_HEIGHT)
    return screenshot.resize(size, resample=Image.Resampling.LANCZOS)


# ---------------------------------------------------------------- contrast measurement
def coverage(mask, size):
    img = Image.fromarray(np.uint8(mask * 255))
    return np.asarray(img.resize((size, size), Image.Resampling.BOX), float) / 255.0


def feature_contrast(rgb, feature_cov, region_cov, ring_px):
    """(ratio, core hex, adjacent hex): median colour of the solid core of a feature
    (coverage >= 0.9) against the median of the adjacent region within ring_px pixels around
    it (feature coverage 0); None when the feature has no solid pixel at this size."""
    core = feature_cov >= 0.9
    if not core.any():
        return None
    img = Image.fromarray(np.uint8((feature_cov > 0.02) * 255))
    near = np.asarray(img.filter(ImageFilter.MaxFilter(2 * ring_px + 1)), float) > 0
    adjacent = near & (feature_cov <= 0.02) & (region_cov >= 0.9)
    if not adjacent.any():
        return None
    a = np.median(rgb[core], axis=0)
    b = np.median(rgb[adjacent], axis=0)
    return contrast(a, b), hex_of(a), hex_of(b)


def measure_knob(knob, frame=50, sizes=(FRAME, 60, 30)):
    """Rows (feature, adjacent, size, result, target): hand against the knob face, value arc
    against the unfilled track (or rim), rim outer edge (the knob boundary) against the
    casing, composited on the panel colour, at the strip size and as shown in mod-ui."""
    hand, arc = knob.masks(frame)
    full = knob.frame_canvas(frame).image()
    g = knob.g
    body = knob.r <= g["body"]
    arc_region = knob.arc_region() & ~arc if arc is not None else None
    edge = band(knob.r, g["rim"][1] - STYLE["edge_width"] * 0.8, g["rim"][1])
    outside = knob.r > g["rim"][1] + 0.004
    if g["track"]:
        outside &= (knob.r < g["track"][0] - 0.002) | ~arc_mask(knob.x, knob.y, *g["track"], -SWEEP, SWEEP)
    rows = []
    for size in sizes:
        resample = Image.Resampling.BOX if size == FRAME else Image.Resampling.LANCZOS
        rgb = composite_on(full.resize((size, size), resample), STYLE["panel"])
        ring = max(1, round(size * 0.02))
        rows.append(("position hand", "knob face", size,
                     feature_contrast(rgb, coverage(hand, size), coverage(body & ~hand, size), ring), 4.5))
        if arc is not None:
            rows.append(("value arc", "unfilled " + ("track" if knob.arc == "track" else "acrylic rim"), size,
                         feature_contrast(rgb, coverage(arc, size), coverage(arc_region, size), ring), 3.0))
        rows.append(("rim outer edge (boundary)", "casing", size,
                     feature_contrast(rgb, coverage(edge, size), coverage(outside | (knob.r > 0.49), size),
                                      ring + 1), 3.0))
    return rows


def measure_footswitch(image):
    """Rows per state: symbol against the button face, ring edge against the casing. The
    state is told by the symbol's shape, so the two rings need no luminance difference."""
    n = FOOT
    x, y = grid(n * OS)
    r = np.hypot(x, y)
    outside = r > 0.405
    edge = band(r, 0.40 - STYLE["edge_width"] * 1.4, 0.40)
    rows = []
    for row, (state, symbol) in enumerate(zip(("bypassed", "active"), footswitch_symbols(x, y))):
        rgb = composite_on(image.crop((0, row * n, n, row * n + n)), STYLE["panel"])
        body = (r <= 0.315) & ~symbol
        rows.append((f"footswitch symbol ({state})", "button face", n,
                     feature_contrast(rgb, coverage(symbol, n), coverage(body, n), 2), 3.0))
        rows.append((f"footswitch ring edge ({state})", "casing", n,
                     feature_contrast(rgb, coverage(edge, n), coverage(outside, n), 4), 3.0))
    return rows


def measure_text(width, height, bevel=None):
    """Text colour against the median casing colour behind the knob labels and the title."""
    plain = np.asarray(casing(width, height, bevel).image().resize((width, height), Image.Resampling.BOX)
                       .convert("RGB"), float) / 255.0
    behind = np.median(plain[100:380, 30:width - 30].reshape(-1, 3), axis=0)
    text = rgb01(STYLE["text"])
    return [("labels, title, brand", "casing", width, (contrast(text, behind), STYLE["text"], hex_of(behind)), 4.5)]


# ---------------------------------------------------------------- files
def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return buffer.getvalue()


def render(desc):
    """{path relative to the modgui directory: PNG bytes}."""
    accent = accent_of(desc)
    face = render_face(desc, accent)
    strip = Knob(accent).strip()
    foot = footswitch(accent)
    screenshot = render_screenshot(desc, face, strip, foot)
    return {
        "face.png": png_bytes(face),
        STRIP: png_bytes(strip),
        FOOTSWITCH: png_bytes(foot),
        "screenshot.png": png_bytes(screenshot),
        "thumbnail.png": png_bytes(render_thumbnail(screenshot)),
    }


def write_if_changed(path, data):
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("json", type=Path, help="plugins/<name>/<name>.json")
    parser.add_argument("--bundle-dir", type=Path, required=True, help="LV2 bundle directory")
    args = parser.parse_args(argv)
    try:
        desc = jpgen.validate(jpgen.load(args.json))
        files = render(desc)
    except (jpgen.ValidationError, GraphicsError) as error:
        print(f"graphics: {args.json}: {error}", file=sys.stderr)
        return 1
    for relative, data in files.items():
        target = args.bundle_dir / "modgui" / relative
        if write_if_changed(target, data):
            print(f"graphics: wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
