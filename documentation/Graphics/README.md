# Graphics: design tool for the pedal faces

Kind: exploration. An offline tool for working on the look of the modgui graphics (pedal
face, knob filmstrip, footswitch). It renders with the generator's own code
(`generator/graphics.py`, `generator/style.py`, the layout in `generator/jpgen.py`), adds
design variants beside the generator's choice, measures the contrast of the
information-bearing features and writes interactive pages that show the pedals on mod-ui's
wood background. Nothing here is part of the build; the pages and images stay in
`generated/` (gitignored). The current design (variant B, Inter SemiBold, medium bevel,
lower area "divider, title, footswitch, brand") is the generator's; a change agreed on the
pages goes into `generator/style.py` or `jpgen.py` and then shows in the build.

## Commands

```
.venv/bin/python3 documentation/Graphics/GraphicsDesign.py fetch             # once
.venv/bin/python3 documentation/Graphics/GraphicsDesign.py pedals [--quick]  # about 55 s
.venv/bin/python3 documentation/Graphics/GraphicsDesign.py colours           # about 140 s
```

- `fetch` downloads the comparison font (Atkinson Hyperlegible, SIL Open Font
  License, from the Google Fonts repository) and copies mod-ui's wood background from the
  device (`ssh`, `DWARF_HOST`, default 192.168.51.1). Both stay in `generated/`; without the
  device the pages fall back to a dark grey background.
- `pedals` writes `generated/Pedals.html`: the pedals with 1 to 6 knobs at actual size,
  with selectors for knob variant, font, casing bevel and lower-area layout, a value slider
  (and sweep), a live grain overlay, zoom (0.5 is what mod-ui shows at its 100 %, 1, 4 for
  detail), greyscale and simulated colour-vision deficiencies (Machado et al. 2009,
  SVG colour matrices), the footswitch states, comparison grids, the accent table of
  `jpgen.COLORS` and the contrast report. `--quick` renders only the default knob variant
  and skips the grids and the accent table.
- `colours` writes `generated/Colours.html`: base colours in 30 degree hue steps at three
  lightness levels, each as the accent of a two-knob pedal, with the derived colours and
  checks, and the current `jpgen.COLORS` with their nearest base hue.
- `GraphicsPreview.py` composes the pedals of `Pedals.html` into PNGs in `generated/` for
  a review outside the browser.

Open the pages with `open documentation/Graphics/generated/Pedals.html` (or `Colours.html`).
The pages remember their settings in the browser (local storage).

## Tuning

Tokens and parameters are in `STYLE` in `generator/style.py`: surface and text colours,
light direction (upper left), bevels, shadows (contact and drop), acrylic (saturation,
inner glow, opacity, halo), ring edges, grain, hand width and the contrast targets. Layout:
`FACE_WIDTHS`, `KNOB_AREA`, `KNOB_SIZES`, `KNOB_ROWS`, `LOWER_AREA` in `generator/jpgen.py`.
The tool's own variants are `VARIANTS`, `FONTS`, `CASE_BEVELS` and `BOTTOMS` in
`GraphicsDesign.py`. The pages render without grain and overlay it live (Grain slider,
`GRAIN_TILE_MAX`); the generator bakes `STYLE["noise"]` (0.4 %).

## Contrast measurement

WCAG 2.2 contrast (sRGB relative luminance) is used as an engineering benchmark, not as a
compliance claim. For each feature the solid core (pixels with at least 90 % coverage) is
compared with the adjacent region (within a few pixels, coverage 0), both composited on the
casing colour, by median colour; antialiased fringes are not required to pass and gaps are
not averaged away. Sizes: 191 (strip frame), 60 (smallest knob in CSS pixels) and 30 (that
knob as mod-ui shows it at 100 %). Targets: text and position hand 4.5:1, value arc, knob
boundary and footswitch cues 3:1. The footswitch tells its state by the symbol's shape
(mod-ui: class "on" = bypassed = top frame with the interrupted power symbol, "off" = active
= bottom frame with the closed circle and line), so on and off are not required to differ
in luminance; each symbol must reach 3:1 against the button face. Ring edges use the accent
darkened to `edge_target` against the casing in the knob's shadow, so light hues (yellow to
cyan) keep a visible boundary. The signal colour (value arc, accent lines) is the accent
darkened in HLS lightness until it reaches 3.5:1 against what it sits on. Greyscale and the
simulations are visual checks only.
