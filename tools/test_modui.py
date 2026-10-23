"""Unit tests for the websocket readback of modui.py (run by ctest as tools.unittest, or with
python3 -m unittest discover -s tools). No device needed."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import modui  # noqa: E402


class FakeWebSocket:
    """Hands out prepared (arrival, message) batches, one per receive() call."""

    def __init__(self, batches):
        self.batches = list(batches)

    def receive(self, seconds):
        return self.batches.pop(0) if self.batches else []


class ParseOutputTest(unittest.TestCase):
    def test_output_set(self):
        self.assertEqual(modui.parse_output("output_set /graph/probe seconds 12.500000"), ("probe", "seconds", 12.5))

    def test_other_messages(self):
        for message in ("stats 12.0 0", "data_ready 3", "param_set /graph/probe/echoIn 1.0",
                        "output_set /graph/probe seconds", "output_set probe seconds 1.0",
                        "output_set /graph/probe seconds nan-ish"):
            with self.subTest(message):
                self.assertIsNone(modui.parse_output(message))


class ParseHwMapTest(unittest.TestCase):
    def test_assignment(self):
        message = "hw_map /graph/gain1 gain /hmi/knob1 -60.000000 12.000000 201 Gain False null 0 0 null 1 0 0"
        self.assertEqual(modui.parse_hw_map(message), ("gain1", "gain", "/hmi/knob1"))
        self.assertEqual(modui.parse_hw_map("hw_map /graph/shipahoy :bypass /hmi/footswitch1 0 1 33"),
                         ("shipahoy", ":bypass", "/hmi/footswitch1"))

    def test_other_messages(self):
        for message in ("stats 1 0", "hw_map", "hw_map gain1 gain /hmi/knob1"):
            with self.subTest(message):
                self.assertIsNone(modui.parse_hw_map(message))


class OutputReaderTest(unittest.TestCase):
    def test_keeps_the_latest_value_and_the_log(self):
        reader = modui.OutputReader(FakeWebSocket([
            [(1.0, "output_set /graph/probe seconds 1.0"), (1.1, "stats 5.0 0")],
            [(1.5, "output_set /graph/probe seconds 1.5")],
        ]))
        reader.poll(0.1)
        reader.poll(0.1)
        self.assertEqual(reader.value("probe", "seconds"), 1.5)
        self.assertEqual(reader.values[("probe", "seconds")], (1.5, 1.5))
        self.assertIsNone(reader.value("probe", "echoOut"))
        self.assertEqual(len(reader.log), 3)

    def test_wait_for_polls_until_accepted_or_timeout(self):
        reader = modui.OutputReader(FakeWebSocket([
            [(1.0, "output_set /graph/probe echoOut 0.0")],
            [(1.2, "output_set /graph/probe echoOut 7.0")],
        ]))
        self.assertEqual(reader.wait_for("probe", "echoOut", lambda v: v == 7.0, 1.0), 7.0)
        self.assertIsNone(reader.wait_for("probe", "echoOut", lambda v: v == 8.0, 0.05))


class AddFavoriteTest(unittest.TestCase):
    def test_posts_the_uri_and_reads_the_answer(self):
        calls = []
        original = modui.http_post
        try:
            for answer, expected in (("true", True), ("false", False)):
                modui.http_post = lambda path, fields, answer=answer: calls.append((path, fields)) or answer
                self.assertEqual(modui.add_favorite("urn:x"), expected)
        finally:
            modui.http_post = original
        self.assertEqual(calls, [("/favorites/add", {"uri": "urn:x"})] * 2)


LISTING = """system:midi_capture_1
   mod-host:midi_in
system:midi_playback_1
mod-midi-merger:in
   ttymidi:MIDI_in
   system:midi_capture_14
mod-midi-merger:out
   effect_0:midiIn
system:midi_capture_14
   mod-midi-merger:in
"""


class ListingHasConnection(unittest.TestCase):
    def test_a_connection_is_found_below_its_source(self):
        self.assertTrue(modui.listing_has_connection(LISTING, "system:midi_capture_1", "mod-host:midi_in"))
        self.assertTrue(modui.listing_has_connection(LISTING, "system:midi_capture_14", "mod-midi-merger:in"))

    def test_a_port_that_starts_with_the_same_name_does_not_count(self):
        # the controller on capture_14 is connected to the merger, capture_1 is not
        self.assertFalse(modui.listing_has_connection(LISTING, "system:midi_capture_1", "mod-midi-merger:in"))

    def test_a_port_name_is_no_connection(self):
        self.assertFalse(modui.listing_has_connection(LISTING, "system:midi_playback_1", "mod-midi-merger:in"))

    def test_an_empty_listing_has_none(self):
        self.assertFalse(modui.listing_has_connection("", "a", "b"))


if __name__ == "__main__":
    unittest.main()
