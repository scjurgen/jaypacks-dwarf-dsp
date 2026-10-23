"""Unit tests for tools/script_headers.py, and the check that every factory script of Ship Ahoy says what X and Y do
and that the table in the plugin page matches (run by ctest as tools.unittest)."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import script_headers  # noqa: E402


class HeaderTest(unittest.TestCase):
    def test_lines_are_found_in_the_header(self):
        text = "-- A script.\n-- X: the glide\n-- Y: the resonance\nfunction OnStep() end\n"
        self.assertEqual(script_headers.header_lines(text), {"X": "the glide", "Y": "the resonance"})

    def test_a_script_that_uses_neither_says_so_for_both(self):
        self.assertEqual(script_headers.header_lines("-- X, Y: not used\n"), {"X": "not used", "Y": "not used"})

    def test_a_script_without_the_lines_is_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            blueprint = folder / "plugin.json"
            blueprint.write_text('{"ports-control": [{"symbol": "script", "listitems": ["Hop", "Slow drift", "User 1"]}]}')
            (folder / "hop.lua").write_text("-- X: a\n-- Y: b\n")
            (folder / "slow-drift.lua").write_text("-- X: a\n")
            problems = script_headers.missing(folder, blueprint)
            self.assertEqual(problems, ["Slow drift: no '-- Y:' line in slow-drift.lua"])

    def test_the_table_replaces_what_stands_between_the_markers(self):
        page = f"before\n{script_headers.BEGIN}\nold\n{script_headers.END}\nafter\n"
        self.assertEqual(script_headers.updated(page, "new"),
                         f"before\n{script_headers.BEGIN}\nnew\n{script_headers.END}\nafter\n")


class ShipAhoyTest(unittest.TestCase):
    def test_every_factory_script_says_what_x_and_y_do(self):
        self.assertEqual(script_headers.missing(), [])

    def test_the_table_in_the_plugin_page_is_current(self):
        text = script_headers.PAGE.read_text(encoding="ascii")
        self.assertEqual(script_headers.updated(text, script_headers.table()), text,
                         "run tools/script_headers.py update")


if __name__ == "__main__":
    unittest.main()
