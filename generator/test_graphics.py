"""Unit tests for graphics.py and style.py (run by ctest as generator.graphics with the Python
of JP_GRAPHICS_PYTHON, or with .venv/bin/python3 -m unittest discover -s generator
-p test_graphics.py). Needs numpy, Pillow and the Inter font in generator/assets/fonts/."""

import copy
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import graphics  # noqa: E402
import jpgen  # noqa: E402
import style  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SHIPAHOY = ROOT / "generator" / "fixtures" / "shipahoy.json"  # the two-knob fixture, as in test_jpgen
# ctest points this at <build>/scratch; otherwise the system temp directory is used
SCRATCH = os.environ.get("JP_SCRATCH_DIR")


def shipahoy():
    with open(SHIPAHOY, encoding="ascii") as handle:
        return jpgen.validate(json.load(handle))


def with_face_controls(desc, count):
    desc = copy.deepcopy(desc)
    for k, port in enumerate(desc["ports-control"]):
        port["hasUI"] = 1 if k < count else 0
    return desc


def image(data):
    return Image.open(io.BytesIO(data))


def accents():
    """Every distinct colour of jpgen.COLORS."""
    return {hexcolour.lower(): style.rgb01(hexcolour) for hexcolour in jpgen.COLORS.values()}


class ContrastTest(unittest.TestCase):
    def test_reference_values(self):
        self.assertAlmostEqual(style.contrast(style.rgb01("#000000"), style.rgb01("#FFFFFF")), 21.0)
        self.assertAlmostEqual(style.contrast(style.rgb01("#777777"), style.rgb01("#777777")), 1.0)
        self.assertAlmostEqual(style.contrast(style.rgb01("#767676"), style.rgb01("#FFFFFF")), 4.54, places=2)

    def test_signal_colour_keeps_hue_and_reaches_the_target(self):
        yellow = style.rgb01("#CCBD00")
        track = style.rgb01(style.STYLE["track"])
        signal = style.signal_colour(yellow, 3.5, track)
        self.assertGreaterEqual(style.contrast(signal, track), 3.5)
        hue = lambda rgb: __import__("colorsys").rgb_to_hls(*rgb)[0]  # noqa: E731
        self.assertAlmostEqual(hue(signal), hue(yellow), places=2)
        blue = style.rgb01("#0000CC")
        self.assertTrue(np.array_equal(style.signal_colour(blue, 3.5, track), blue))


class RenderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = graphics.render(shipahoy())

    def test_image_sizes(self):
        self.assertEqual(image(self.files["face.png"]).size, (230, 431))
        self.assertEqual(image(self.files["screenshot.png"]).size, (262, 431))
        self.assertEqual(image(self.files["thumbnail.png"]).size, (39, 64))
        strip = image(self.files[graphics.STRIP])
        self.assertEqual(strip.size, (graphics.FRAME * graphics.FRAMES, graphics.FRAME))
        self.assertEqual(strip.mode, "RGBA")
        self.assertEqual(image(self.files[graphics.FOOTSWITCH]).size, (64, 128))

    def test_screenshot_uses_the_midi_plugs_for_midi_ports(self):
        def plugs(desc):
            face = graphics.render_face(desc)
            return graphics.render_screenshot(desc, face, graphics.Knob(graphics.accent_of(desc)).strip(),
                                              graphics.footswitch(graphics.accent_of(desc)))

        midi_in = graphics.open_image(graphics.ASSETS / "midiInput.png").convert("RGBA")
        midi_out = graphics.open_image(graphics.ASSETS / "midiOutput.png").convert("RGBA")
        desc = copy.deepcopy(shipahoy())
        desc["audio-ports-in"], desc["audio-ports-out"] = [], []
        desc["midi-in"] = desc["midi-out"] = True
        shot = plugs(desc)
        pad = 16
        self.assertEqual(shot.size[0], graphics.render_face(desc).size[0] + 2 * pad)
        self.assertEqual(midi_in.size, (16, 56))
        left = shot.crop((pad - midi_in.size[0], graphics.JACK_TOP, pad, graphics.JACK_TOP + midi_in.size[1]))
        x = shot.size[0] - pad
        right = shot.crop((x, graphics.JACK_TOP, x + midi_out.size[0], graphics.JACK_TOP + midi_out.size[1]))
        self.assertEqual(left.tobytes(), midi_in.tobytes())
        self.assertEqual(right.tobytes(), midi_out.tobytes())
        # an audio-only plugin keeps its layout
        self.assertEqual(image(self.files["screenshot.png"]).size[0], 262)

    def test_every_count_renders_at_its_width(self):
        for count in range(jpgen.MAX_UI_CONTROLS + 1):
            desc = with_face_controls(shipahoy(), count)
            self.assertEqual(graphics.render_face(desc).size, jpgen.face_size(count))

    def test_nothing_is_clipped_at_the_frame_borders(self):
        strip = image(self.files[graphics.STRIP])
        for f in (0, 50, 100):
            frame = strip.crop((f * graphics.FRAME, 0, (f + 1) * graphics.FRAME, graphics.FRAME))
            self.assertLessEqual(graphics.border_alpha(frame), 2, f)
        foot = image(self.files[graphics.FOOTSWITCH])
        for row in (0, 1):
            self.assertLessEqual(graphics.border_alpha(foot.crop((0, row * 64, 64, row * 64 + 64))), 2, row)

    def test_rendering_is_deterministic(self):
        accent = graphics.accent_of(shipahoy())
        self.assertEqual(graphics.png_bytes(graphics.Knob(accent).frame(37)),
                         graphics.png_bytes(graphics.Knob(accent).frame(37)))
        self.assertEqual(graphics.render_face(shipahoy()).tobytes(), graphics.render_face(shipahoy()).tobytes())
        self.assertEqual(graphics.footswitch(accent).tobytes(), graphics.footswitch(accent).tobytes())

    def test_labels_sit_under_their_knobs_in_one_size(self):
        for count in range(1, jpgen.MAX_UI_CONTROLS + 1):
            desc = with_face_controls(shipahoy(), count)
            knobs = jpgen.knob_positions(count, jpgen.face_size(count)[0])
            result = graphics.labels(desc)
            self.assertEqual(len({px for _, _, _, px in result}), 1, count)
            for (text, x, y, px), (left, top, size) in zip(result, knobs):
                self.assertAlmostEqual(x, left + size / 2)
                self.assertEqual(y, top + size + 2)
                self.assertLessEqual(px, style.STYLE["label_px"])

    def test_missing_font_is_reported(self):
        saved = graphics.FONT
        graphics.FONT = (Path("NoSuchFont.ttf"), None)
        try:
            with self.assertRaises(graphics.GraphicsError):
                graphics.render_face(shipahoy())
        finally:
            graphics.FONT = saved


class KnobTest(unittest.TestCase):
    def test_lighting_does_not_turn_with_the_value(self):
        # outside the hand and the value-arc track, every frame is the same image
        knob = graphics.Knob(graphics.accent_of(shipahoy()))
        size = graphics.FRAME
        moving = graphics.coverage(knob.arc_region(), size) > 0
        frames = {}
        for f in (0, 63, 100):
            hand, _ = knob.masks(f)
            moving |= graphics.coverage(hand, size) > 0
            frames[f] = np.asarray(knob.frame(f))
        for f in (63, 100):
            self.assertTrue(np.array_equal(frames[0][~moving], frames[f][~moving]), f)

    def test_every_colour_reaches_the_contrast_targets(self):
        # hand 4.5:1 against the knob face, value arc and rim edge 3:1, at 191 and 60 px;
        # at 30 px (60 px knob in mod-ui at 100 %) the hand and the arc
        for name, accent in accents().items():
            knob = graphics.Knob(accent)
            for feature, adjacent, size, result, target in graphics.measure_knob(knob):
                if size == 30 and feature == "rim outer edge (boundary)":
                    continue  # thinner than a pixel at that size; the track marks the knob
                self.assertIsNotNone(result, (name, feature, size))
                self.assertGreaterEqual(result[0], target, (name, feature, size, result))


class FootswitchTest(unittest.TestCase):
    def test_every_colour_reaches_the_contrast_targets(self):
        for name, accent in accents().items():
            for feature, adjacent, size, result, target in graphics.measure_footswitch(graphics.footswitch(accent)):
                self.assertIsNotNone(result, (name, feature))
                self.assertGreaterEqual(result[0], target, (name, feature, result))

    def test_states_differ_in_the_symbol_shape(self):
        # active: the circle is closed where the power symbol has its gap (+-20 degrees from
        # the top) and the line reaches below the centre, where the power symbol's bar does not
        radius = graphics.FOOT_SYMBOL[0]
        points = [(radius * np.sin(np.radians(a)), -radius * np.cos(np.radians(a))) for a in (-20, 20)]
        points.append((0.0, 0.4 * radius))
        x = np.array([p[0] for p in points])
        y = np.array([p[1] for p in points])
        bypassed, active = graphics.footswitch_symbols(x, y)
        self.assertTrue(active.all())
        self.assertFalse(bypassed.any())
        foot = graphics.footswitch(graphics.accent_of(shipahoy()))
        top, bottom = np.asarray(foot.crop((0, 0, 64, 64))), np.asarray(foot.crop((0, 64, 64, 128)))
        self.assertFalse(np.array_equal(top, bottom))


class FaceTest(unittest.TestCase):
    def test_text_contrast(self):
        for width in sorted(set(jpgen.FACE_WIDTHS.values())):
            for feature, adjacent, size, result, target in graphics.measure_text(width, jpgen.FACE_HEIGHT):
                self.assertGreaterEqual(result[0], target, (width, result))

    def test_face_is_milky_for_every_colour(self):
        # the plugin colour no longer fills the face: the middle is the casing colour
        panel = style.rgb01(style.STYLE["panel"])
        for colour in ("Filter", "#104474"):
            desc = shipahoy()
            desc["color"] = colour
            face = np.asarray(graphics.render_face(desc).convert("RGB"), float) / 255.0
            self.assertLess(np.abs(face[150, 20] - panel).max(), 0.05, colour)


class CommandLineTest(unittest.TestCase):
    def test_writes_files_and_rewrites_only_changes(self):
        if SCRATCH:
            Path(SCRATCH).mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            args = [str(SHIPAHOY), "--bundle-dir", tmp]
            self.assertEqual(graphics.main(args), 0)
            face = Path(tmp) / "modgui" / "face.png"
            first = face.stat().st_mtime_ns
            self.assertEqual(graphics.main(args), 0)
            self.assertEqual(face.stat().st_mtime_ns, first)
            self.assertTrue((Path(tmp) / "modgui" / graphics.STRIP).exists())
            self.assertTrue((Path(tmp) / "modgui" / graphics.FOOTSWITCH).exists())


if __name__ == "__main__":
    unittest.main()
