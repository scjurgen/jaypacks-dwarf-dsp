"""Unit tests for the checks of jp.py (run by ctest as tools.unittest, or with
python3 -m unittest discover -s tools)."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jp  # noqa: E402

# ctest points this at <build>/scratch; otherwise the system temp directory is used
SCRATCH = os.environ.get("JP_SCRATCH_DIR")

DYNSYM = """
Symbol table '.dynsym' contains 5 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     4: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND _ZdlPvm@CXXABI_1.3.9 (2)
    20: 0000000000000db8     0 FUNC    GLOBAL DEFAULT    8 _init
    21: 00000000000025d4     0 FUNC    GLOBAL DEFAULT   11 _fini
    22: 0000000000000fb0    20 FUNC    GLOBAL DEFAULT   10 lv2_descriptor
"""
VERSIONS = """
Version needs section '.gnu.version_r' contains 2 entries:
  000000: Version: 1  File: libm.so.6  Cnt: 2
  0x0010:   Name: GLIBC_2.27  Flags: none  Version: 5
  0x0020:   Name: GLIBC_2.17  Flags: none  Version: 4
  0x0030: Version: 1  File: libstdc++.so.6  Cnt: 2
  0x0040:   Name: CXXABI_1.3.9  Flags: none  Version: 2
  0x0050:   Name: GLIBCXX_3.4  Flags: none  Version: 3
"""


class BinaryCheckTest(unittest.TestCase):
    def test_good_binary(self):
        self.assertEqual(jp.binary_problems(DYNSYM + VERSIONS), [])

    def test_extra_export(self):
        extra = DYNSYM + "    23: 0000000000001000    40 FUNC    GLOBAL DEFAULT   10 _ZN2jp4Testv\n"
        self.assertEqual(jp.binary_problems(extra + VERSIONS),
                         ["exports ['_ZN2jp4Testv', 'lv2_descriptor'], expected only lv2_descriptor"])

    def test_missing_entry_point(self):
        without = "\n".join(line for line in DYNSYM.splitlines() if "lv2_descriptor" not in line)
        self.assertEqual(len(jp.binary_problems(without + VERSIONS)), 1)

    def test_too_new_versions(self):
        newer = VERSIONS.replace("GLIBC_2.27", "GLIBC_2.28").replace("GLIBCXX_3.4 ", "GLIBCXX_3.4.29 ")
        self.assertEqual(sorted(jp.binary_problems(DYNSYM + newer)),
                         ["needs GLIBCXX_3.4.29, the device has GLIBCXX_3.4.28",
                          "needs GLIBC_2.28, the device has GLIBC_2.27"])

    def test_aarch64_detection(self):
        if SCRATCH:
            Path(SCRATCH).mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            arm = Path(tmp) / "arm.so"
            arm.write_bytes(b"\x7fELF" + bytes(14) + (183).to_bytes(2, "little"))
            x86 = Path(tmp) / "x86.so"
            x86.write_bytes(b"\x7fELF" + bytes(14) + (62).to_bytes(2, "little"))
            macho = Path(tmp) / "mac.so"
            macho.write_bytes(b"\xcf\xfa\xed\xfe" + bytes(16))
            self.assertTrue(jp.is_aarch64_elf(arm))
            self.assertFalse(jp.is_aarch64_elf(x86))
            self.assertFalse(jp.is_aarch64_elf(macho))


class ScriptPathTest(unittest.TestCase):
    def test_a_script_and_its_log_live_in_the_folder_of_the_plugin(self):
        self.assertEqual(jp.script_remote_path("morphexsynth", "user-1"), "/data/jaypacks/scripts/morphexsynth/user-1.lua")
        self.assertEqual(jp.script_remote_path("morphexsynth", "user-1", ".log"),
                         "/data/jaypacks/scripts/morphexsynth/user-1.log")

    def test_names_that_could_leave_the_folder_or_reach_the_shell_are_refused(self):
        for plugin, name in (("morphexsynth", "../x"), ("morphexsynth", "a b"), ("morphexsynth", "a;rm"),
                             ("morphexsynth", ""), ("morphexsynth", "x" * 48), ("../etc", "user-1"),
                             ("Morph", "user-1"), ("a/b", "c"), ("", "c")):
            with self.assertRaises(ValueError, msg=f"{plugin!r} {name!r}"):
                jp.script_remote_path(plugin, name)

    def test_push_script_accepts_only_plain_text(self):
        Path(SCRATCH or tempfile.gettempdir()).mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            bad = Path(tmp) / "bad.lua"
            bad.write_bytes("x = 1 -- caf\u00e9\n".encode("utf-8"))
            self.assertEqual(jp.cmd_push_script("morphexsynth", "user-1", str(bad)), 1)


if __name__ == "__main__":
    unittest.main()
