"""Style of the modgui graphics: tokens, parameters and the colour rules of graphics.py.

The values were worked out with documentation/Graphics/ (design pages and contrast report):
a milky casing lit from the upper left, satin knob bodies, a glowing acrylic rim tinted in
the plugin colour (the accent), a dark position hand and a value arc in the "signal" colour.
Decoration may glow; whatever carries information (hand, value arc, ring edges, accent
lines, footswitch symbol) is darkened until it reaches a contrast target (WCAG 2.2 contrast
ratio, used as an engineering benchmark).

Needs numpy.
"""

import colorsys

import numpy as np

STYLE = {
    # surfaces and text
    "panel": "#EAE8E3",
    "knob_face": "#F1F0EC",
    "shaded_edge": "#D3D5D1",
    "text": "#292F33",
    "track": "#D2D4CF",
    "hand": "#292F33",
    "off_ring": "#E6E5E0",
    "off_ring_edge": "#7D8380",
    # light from the upper left (x right, y down)
    "light": (-1.0, -1.0),
    # knob, fractions of the frame side
    "bevel_width": 0.050,
    "bevel_strength": 0.75,
    "face_gradient": 0.025,
    "satin_spot": 0.18,
    "shadow_offset": 0.020,     # contact shadow
    "shadow_blur": 0.022,
    "shadow_opacity": 0.38,
    "drop_offset": 0.070,       # drop shadow towards the lower right (extruded look)
    "drop_blur": 0.022,
    "drop_opacity": 0.40,
    "edge_width": 0.020,        # dark outer and inner edge of the acrylic (the knob boundary)
    "shadowed_casing": 0.68,    # casing brightness next to the rim, inside the knob's shadows
    "edge_target": 4.5,         # contrast of the edge colour against that shadowed casing
    "reflection": 0.65,
    "reflection_width": 0.014,
    "hand_width": 0.060,
    # acrylic: lit from inside
    "acrylic_saturation": 1.45,  # factor on the HLS saturation of the accent
    "acrylic_glow": 0.75,        # brightness of the core of the ring (0 = flat tint)
    "acrylic_opacity": 0.85,
    "acrylic_halo": 0.30,        # coloured light spilling onto casing and knob edge
    "acrylic_halo_blur": 0.014,
    "rim_opacity_c": 0.20,       # arc inside the rim (design variant C): pale rim
    # signal colour: the accent darkened until it reaches this ratio against what it sits on
    "signal_target": 3.5,
    "symbol_target": 4.5,       # footswitch symbol (active) against the button face
    # casing, face pixels
    "case_bevel_width": 11.0,
    "case_bevel_strength": 1.0,
    "case_gradient": 0.03,
    "case_occlusion": 0.18,     # darkening of the rounded edge independent of the light
    "noise": 0.01,             # grain: std of an additive luminance noise, fraction of full scale
    "noise_blur": 0.6,          # display pixels: soft grain instead of digital speckle
    # face text, face pixels
    "label_px": 16,
    "label_min_px": 9,
    "title_px": 26,
    "brand_px": 12,
    "brand_tracking": 0.22,     # letter spacing of the brand, fraction of the font size
}


def rgb01(hexcolour):
    """'#rrggbb' -> numpy RGB in 0..1."""
    return np.array([int(hexcolour[i:i + 2], 16) for i in (1, 3, 5)], float) / 255.0


def hex_of(rgb):
    return "#" + "".join(f"{int(round(c * 255)):02X}" for c in np.clip(rgb, 0, 1))


def rgb255(rgb):
    return tuple(int(round(c * 255)) for c in np.clip(rgb, 0, 1))


def luminance(rgb):
    """sRGB relative luminance (WCAG 2.2) of an RGB array in 0..1 (last axis RGB)."""
    c = np.asarray(rgb, float)
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]


def contrast(a, b):
    """WCAG contrast ratio of two colours, 1 to 21."""
    la, lb = float(luminance(a)), float(luminance(b))
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def signal_colour(accent, target, against):
    """The accent darkened in HLS lightness (hue and saturation kept) until it reaches
    `target` against the colour `against`; the accent itself where it already does."""
    h, l, s = colorsys.rgb_to_hls(*accent)
    rgb = np.array(accent, float)
    while contrast(rgb, against) < target and l > 0:
        l = max(0.0, l - 0.005)
        rgb = np.array(colorsys.hls_to_rgb(h, l, s))
    return rgb


def acrylic_colours(accent, glow=None):
    """(core, edge) of the glowing acrylic: saturation raised, the core lighter as if lit
    from inside, the edge darker."""
    glow = STYLE["acrylic_glow"] if glow is None else glow
    h, l, s = colorsys.rgb_to_hls(*accent)
    s = min(1.0, s * STYLE["acrylic_saturation"])
    core = np.array(colorsys.hls_to_rgb(h, l + (0.72 - l) * glow * 0.6, s))
    edge = np.array(colorsys.hls_to_rgb(h, l * 0.55, s))
    return core, edge
