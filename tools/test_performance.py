"""Unit tests for tools/performance.py, and the check that the performance tables in the docs
match the measured JSON files (run by ctest as tools.unittest)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import performance  # noqa: E402

SAMPLE = {
    "plugin": "demo",
    "version": "0.1.0",
    "date": "2026-10-15",
    "commit": "abc1234",
    "device": {"firmware": "MOD OS v1.13.5.3315", "jack": "48000 Hz, 128 frames"},
    "source": "siggen white noise at -12 dBFS",
    "cycles": 3,
    "window_s": 6,
    "cpu_points": [
        {"settings": "defaults", "median": 2.44, "min": 2.1, "max": 2.8},
        {"settings": "preset Fast", "median": 3.05, "min": 2.9, "max": 3.3},
    ],
    "memory_kb": {"allocated": 1647.5, "peak": 1647.5, "counted": "2026-10-16", "binary": 180},
    "xruns": 0,
}


class TablesTest(unittest.TestCase):
    def test_plugin_table(self):
        table = performance.plugin_table(SAMPLE)
        self.assertIn("| defaults | 2.4 | 2.1 to 2.8 |", table)
        self.assertIn("| preset Fast | 3.0 | 2.9 to 3.3 |", table)
        self.assertIn("allocates 1648 kB when it is created", table)
        self.assertIn("peak 1648 kB", table)
        self.assertIn("median of 3 cycles of 6 s", table)

    def test_summary_names_the_highest_setting(self):
        table = performance.summary_table({"demo": SAMPLE})
        self.assertIn("| `demo` | 0.1.0 | 2.4 | 3.0 (preset Fast) | 1648 kB | 180 kB | 2026-10-15 |", table)

    def test_small_sizes_keep_a_decimal(self):
        self.assertEqual(performance.kb(0.9), "0.9")
        self.assertEqual(performance.kb(9.94), "9.9")
        self.assertEqual(performance.kb(12.4), "12")

    def test_replace_marked(self):
        text = f"head\n{performance.BEGIN}\nold\n{performance.END}\ntail\n"
        self.assertEqual(performance.replace_marked(text, "new"),
                         f"head\n{performance.BEGIN}\nnew\n{performance.END}\ntail\n")
        with self.assertRaises(ValueError):
            performance.replace_marked("no markers", "new")

    def test_update_writes_readme_and_page(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "plugins" / "demo").mkdir(parents=True)
            (repo / "docs" / "plugins").mkdir(parents=True)
            (repo / "plugins" / "demo" / "performance.json").write_text(json.dumps(SAMPLE))
            marked = f"x\n{performance.BEGIN}\n{performance.END}\n"
            (repo / "README.md").write_text(marked)
            (repo / "docs" / "plugins" / "demo.md").write_text(marked)
            self.assertEqual(len(performance.stale_docs(repo)), 2)
            self.assertEqual(len(performance.update_docs(repo)), 2)
            self.assertEqual(performance.stale_docs(repo), [])


class RepositoryTest(unittest.TestCase):
    def test_tables_in_the_docs_are_current(self):
        stale = [str(path.relative_to(performance.REPO_DIR)) for path in performance.stale_docs()]
        self.assertEqual(stale, [], "run tools/performance.py update")


if __name__ == "__main__":
    unittest.main()
