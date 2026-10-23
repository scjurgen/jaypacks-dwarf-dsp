"""Unit tests for jpgen.py (run by ctest as generator.unittest, or with
python3 -m unittest discover -s generator)."""

import copy
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jpgen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
# a plain filter blueprint (one audio in, one out, nine controls, no MIDI, no worker, no designation, no control
# outputs) frozen as the sample for the generator tests; the live plugins grow beyond it
SHIPAHOY = ROOT / "generator" / "fixtures" / "shipahoy.json"
# ctest points this at <build>/scratch; otherwise the system temp directory is used
SCRATCH = os.environ.get("JP_SCRATCH_DIR")


def scratch_dir():
    if SCRATCH:
        Path(SCRATCH).mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=SCRATCH)


def shipahoy():
    with open(SHIPAHOY, encoding="ascii") as handle:
        return json.load(handle)


def control(desc, symbol):
    return next(p for p in desc["ports-control"] if p["symbol"] == symbol)


class ValidationTest(unittest.TestCase):
    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_shipahoy_is_valid(self):
        jpgen.validate(shipahoy())

    def test_unknown_top_level_key(self):
        desc = shipahoy()
        desc["colour"] = "red"
        self.assertInvalid(desc, "unknown keys ['colour']")

    def test_missing_key(self):
        desc = shipahoy()
        del desc["plugintype"]
        self.assertInvalid(desc, "missing keys ['plugintype']")

    def test_default_outside_range(self):
        desc = shipahoy()
        control(desc, "speed")["default"] = 20
        self.assertInvalid(desc, "'speed'.default: 20 outside")

    def test_duplicate_symbol(self):
        desc = shipahoy()
        control(desc, "speed")["symbol"] = "mix"
        self.assertInvalid(desc, "symbol used twice")

    def test_control_symbol_clashing_with_audio_port(self):
        desc = shipahoy()
        control(desc, "speed")["symbol"] = "in"
        self.assertInvalid(desc, "symbol used twice")

    def test_logarithmic_needs_positive_minimum(self):
        desc = shipahoy()
        control(desc, "frequency")["minimum"] = 0
        self.assertInvalid(desc, "logarithmic port needs minimum > 0")

    def test_enumeration_needs_items(self):
        desc = shipahoy()
        del control(desc, "filterType")["listitems"]
        self.assertInvalid(desc, "listitems: required")

    def test_enumeration_value_must_be_integer(self):
        desc = shipahoy()
        desc["presets"][0]["values"]["filterType"] = 1.5
        self.assertInvalid(desc, "must be an integer")

    def test_preset_with_unknown_control(self):
        desc = shipahoy()
        desc["presets"][0]["values"]["volume"] = 1
        self.assertInvalid(desc, "unknown control 'volume'")

    def test_duplicate_preset_name(self):
        desc = shipahoy()
        desc["presets"].append(copy.deepcopy(desc["presets"][0]))
        self.assertInvalid(desc, "is used twice")

    def test_non_ascii_text(self):
        desc = shipahoy()
        desc["description"][0] = "Ship Ahoy" + chr(0x2122)
        self.assertInvalid(desc, "only printable ASCII")

    def test_label_length(self):
        desc = shipahoy()
        desc["displayname"] = "Ship Ahoy deluxe edition"
        self.assertInvalid(desc, "1 to 16 characters")

    def test_plugin_type(self):
        desc = shipahoy()
        desc["plugintype"] = "Filter"
        self.assertInvalid(desc, "not an LV2 plugin class")

    def test_bool_is_not_a_number(self):
        desc = shipahoy()
        control(desc, "mix")["default"] = True
        self.assertInvalid(desc, "number required")

    def test_face_colour(self):
        desc = shipahoy()
        desc["boxy"] = "boxy"
        self.assertInvalid(desc, "unknown keys ['boxy']")
        desc = shipahoy()
        desc["color"] = "Mauve"
        self.assertInvalid(desc, "color:")
        desc["color"] = "#12aB34"
        jpgen.validate(desc)
        del desc["color"]
        self.assertInvalid(desc, "missing keys ['color']")

    def test_at_most_six_face_controls(self):
        desc = shipahoy()
        for port in desc["ports-control"]:
            port["hasUI"] = 1
        self.assertInvalid(desc, "at most 6 controls with hasUI 1")


class GenerationTest(unittest.TestCase):
    def setUp(self):
        self.files = jpgen.generate(jpgen.validate(shipahoy()), "plugins/shipahoy/shipahoy.json")

    def test_writes_header_manifest_plugin_and_presets(self):
        self.assertEqual(sorted(self.files), ["bundle/manifest.ttl", "bundle/presets.ttl", "bundle/shipahoy.ttl",
                                              "header/ports.h"])

    def test_ports_header_lists_every_control_with_its_setter(self):
        header = self.files["header/ports.h"]
        self.assertIn('{2, "mix", -100.0f, 100.0f, 100.0f, &Pedal::setMix},', header)
        self.assertIn('{10, "filterType", 0.0f, 14.0f, 1.0f, &Pedal::setFilterType},', header)
        self.assertIn("std::array<ControlInput<Pedal>, 9>", header)
        self.assertIn('#include "plugins/shipahoy/ShipAhoyPedal.h"', header)

    def test_plugin_ttl_uses_the_right_namespaces(self):
        ttl = self.files["bundle/shipahoy.ttl"]
        self.assertIn("<https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/shipahoy>", ttl)
        self.assertEqual(ttl.count("lv2:portProperty pprops:logarithmic"), 2)
        self.assertNotIn("lv2:logarithmic", ttl)
        self.assertEqual(ttl.count("lv2:portProperty lv2:integer, lv2:enumeration"), 2)
        self.assertIn('[ rdfs:label "4 pole phaser" ; rdf:value 14 ]', ttl)
        self.assertIn('mod:brand "Krachpedal"', ttl)
        self.assertNotIn("requiredFeature", ttl)
        self.assertNotIn("extensionData", ttl)
        self.assertNotIn("majorVersion", ttl)

    def test_description_is_escaped(self):
        self.assertIn('Frank Zappa\'s \\"Ship Ahoy\\"', self.files["bundle/shipahoy.ttl"])

    def test_manifest_names_binary_and_presets(self):
        manifest = self.files["bundle/manifest.ttl"]
        self.assertIn("lv2:binary <jaypacks-shipahoy.so>", manifest)
        self.assertIn("#preset-ship-ahoy>", manifest)
        self.assertEqual(manifest.count("a pset:Preset"), 3)

    def test_preset_values_are_floats(self):
        presets = self.files["bundle/presets.ttl"]
        self.assertIn('[ lv2:symbol "filterType" ; pset:value 1.0 ]', presets)

    def test_all_output_is_ascii(self):
        for name, text in jpgen.generate(shipahoy(), "x.json", modgui=True).items():
            self.assertTrue(all(ord(c) < 128 for c in text), name)

    def test_modgui_files_only_on_request(self):
        self.assertNotIn("modgui.ttl", self.files["bundle/manifest.ttl"])
        files = jpgen.generate(shipahoy(), "x.json", modgui=True)
        self.assertEqual(sorted(set(files) - set(self.files)),
                         ["bundle/modgui.ttl", "bundle/modgui/jaypacks-shipahoy.css",
                          "bundle/modgui/jaypacks-shipahoy.html"])
        self.assertIn("rdfs:seeAlso <shipahoy.ttl> ;\n    rdfs:seeAlso <modgui.ttl> .", files["bundle/manifest.ttl"])

    def test_the_icon_template_has_a_jack_for_the_midi_input(self):
        # without it mod-ui shows no MIDI socket on the pedal and nothing can be wired to the plugin
        files = jpgen.generate(shipahoy(), "x.json", modgui=True)
        html = files["bundle/modgui/jaypacks-shipahoy.html"]
        self.assertIn('{{#effect.ports.midi.input}}', html)
        self.assertIn('mod-role="input-midi-port"', html)

    def test_the_icon_template_has_a_jack_for_the_midi_output(self):
        # the port is in the TTL and in the JACK graph, but without this mod-ui draws no socket to cable from
        files = jpgen.generate(shipahoy(), "x.json", modgui=True)
        html = files["bundle/modgui/jaypacks-shipahoy.html"]
        self.assertIn('{{#effect.ports.midi.output}}', html)
        self.assertIn('mod-role="output-midi-port"', html)

    def test_modgui_lists_the_face_controls(self):
        files = jpgen.generate(shipahoy(), "x.json", modgui=True)
        ttl = files["bundle/modgui.ttl"]
        self.assertIn('modgui:port [ lv2:index 0 ; lv2:symbol "mix" ; lv2:name "Mix" ],', ttl)
        self.assertIn('[ lv2:index 1 ; lv2:symbol "speed" ; lv2:name "Speed" ]', ttl)
        self.assertIn('modgui:panel "2-knobs"', ttl)
        self.assertIn("modgui:iconTemplate <modgui/jaypacks-shipahoy.html>", ttl)
        self.assertIn("mod-two-knobs mod-boxy", files["bundle/modgui/jaypacks-shipahoy.html"])
        self.assertIn('modgui:brand "Krachpedal"', ttl)
        css = files["bundle/modgui/jaypacks-shipahoy.css"]
        self.assertIn("background-size:230px 431px;", css)
        self.assertIn("url(/resources/face.png{{{ns}}})", css)
        # one absolute rule per knob, at jpgen.knob_positions()
        (left, top, size), _ = jpgen.knob_positions(2, 230)
        self.assertIn(".mod-pedal-boxy{{{cns}}} .mod-control-group .mod-knob:nth-child(1) {\n"
                      f"    height:90px;\n    left:{left:.1f}px;\n    top:{top:.1f}px;\n    width:90px;\n}}", css)
        self.assertIn("nth-child(2) .mod-knob-image {\n    background-size:auto 90px;", css)
        self.assertNotIn("nth-child(3)", css)
        self.assertIn(f"    left:83px;\n    margin:0;\n    position:absolute !important;\n"
                      f"    top:{jpgen.lower_layout()['footswitch_top']}px;", css)
        self.assertNotIn("${", css)

    def test_face_lets_the_drag_handle_through(self):
        # mod-ui moves a pedal through its drag handle (z-index 20 over the whole pedal); the
        # knob group lies above it over the whole face, so only the knobs may take the mouse
        css = jpgen.generate(shipahoy(), "x.json", modgui=True)["bundle/modgui/jaypacks-shipahoy.css"]
        group = css.split(".mod-pedal-boxy{{{cns}}} .mod-control-group {", 1)[1].split("}", 1)[0]
        knob = css.split(".mod-pedal-boxy{{{cns}}} .mod-control-group .mod-knob {", 1)[1].split("}", 1)[0]
        self.assertIn("pointer-events:none;", group)
        self.assertIn("pointer-events:auto;", knob)

    def test_face_width_follows_the_control_count(self):
        desc = shipahoy()
        for k, port in enumerate(desc["ports-control"]):
            port["hasUI"] = 1 if k < 6 else 0
        css = jpgen.generate(jpgen.validate(desc), "x.json", modgui=True)["bundle/modgui/jaypacks-shipahoy.css"]
        self.assertIn("background-size:280px 431px;", css)
        self.assertIn("nth-child(6) .mod-knob-image", css)

    def test_modgui_without_face_controls(self):
        desc = shipahoy()
        for port in desc["ports-control"]:
            port["hasUI"] = 0
        files = jpgen.generate(desc, "x.json", modgui=True)
        self.assertNotIn("modgui:port", files["bundle/modgui.ttl"])
        self.assertIn('modgui:panel "0-knobs"', files["bundle/modgui.ttl"])
        self.assertIn("mod-no-knobs", files["bundle/modgui/jaypacks-shipahoy.html"])


def with_outputs(desc):
    desc["ports-control-out"] = [
        {"display": "Level", "symbol": "level", "minimum": -240, "maximum": 40, "unit": "db"},
        {"display": "Count", "symbol": "count", "minimum": 0, "maximum": 16777216, "property": "integer"},
    ]
    return desc


class ControlOutputTest(unittest.TestCase):
    """ports-control-out and plugins without audio input."""

    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_every_plugin_json_is_valid(self):
        # plugins/<name>/<name>.json; other JSON files there (performance.json) are not descriptions
        paths = [path for path in (ROOT / "plugins").glob("*/*.json") if path.stem == path.parent.name]
        self.assertTrue(paths)
        for path in sorted(paths):
            with self.subTest(path.name):
                desc = jpgen.validate(jpgen.load(path))
                self.assertEqual(desc["name"], path.stem)

    def test_outputs_follow_the_control_inputs(self):
        files = jpgen.generate(jpgen.validate(with_outputs(shipahoy())), "x.json", modgui=True)
        ttl = files["bundle/shipahoy.ttl"]
        self.assertIn('a lv2:ControlPort, lv2:OutputPort ;\n        lv2:index 11 ;\n        lv2:symbol "level"', ttl)
        self.assertIn('lv2:index 12 ;\n        lv2:symbol "count" ;\n        lv2:name "Count" ;\n'
                      '        lv2:minimum 0 ;\n        lv2:maximum 16777216 ;\n        lv2:portProperty lv2:integer', ttl)
        header = files["header/ports.h"]
        self.assertIn("std::array<ControlOutput<Pedal>, 2> c_controlOutputs", header)
        self.assertIn('{11, "level", &Pedal::getLevel},', header)
        self.assertIn('{12, "count", &Pedal::getCount},', header)
        self.assertIn('modgui:monitoredOutputs [ lv2:symbol "level" ],\n            [ lv2:symbol "count" ]',
                      files["bundle/modgui.ttl"])

    def test_no_outputs_no_monitored_outputs(self):
        files = jpgen.generate(shipahoy(), "x.json", modgui=True)
        self.assertNotIn("monitoredOutputs", files["bundle/modgui.ttl"])
        self.assertIn("std::array<ControlOutput<Pedal>, 0> c_controlOutputs", files["header/ports.h"])

    def test_output_validation(self):
        cases = [
            (lambda d: d["ports-control-out"][0].update(default=0), "unknown keys ['default']"),
            (lambda d: d["ports-control-out"][0].pop("maximum"), "missing keys ['maximum']"),
            (lambda d: d["ports-control-out"][0].update(minimum=50), "minimum must be below maximum"),
            (lambda d: d["ports-control-out"][0].update(symbol="mix"), "symbol used twice"),
            (lambda d: d["ports-control-out"][0].update(property="logarithmic"), "property: one of ['integer']"),
            (lambda d: d["ports-control-out"][0].update(unit="volt"), "unit: one of"),
            (lambda d: d.update({"ports-control-out": {}}), "ports-control-out: expected a list"),
        ]
        for change, fragment in cases:
            with self.subTest(fragment):
                desc = with_outputs(shipahoy())
                change(desc)
                self.assertInvalid(desc, fragment)

    def test_a_source_has_no_audio_input_but_needs_an_output(self):
        desc = shipahoy()
        desc["audio-ports-in"] = []
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        self.assertIn("c_audioInputs{0}", files["header/ports.h"])
        self.assertIn('lv2:index 0 ;\n        lv2:symbol "out"', files["bundle/shipahoy.ttl"])
        desc["audio-ports-out"] = []
        self.assertInvalid(desc, "audio-ports-out: expected a non-empty list")


class MidiInputTest(unittest.TestCase):
    """midi-in: an atom sequence port that supports MIDI, after the control outputs."""

    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_a_plugin_without_midi_has_no_atom_port_and_no_required_feature(self):
        files = jpgen.generate(shipahoy(), "x.json")
        self.assertNotIn("atom:AtomPort", files["bundle/shipahoy.ttl"])
        self.assertNotIn("requiredFeature", files["bundle/shipahoy.ttl"])
        self.assertIn("c_midiInput{false}", files["header/ports.h"])
        self.assertIn("c_midiIndex{0}", files["header/ports.h"])

    def test_the_midi_port_follows_the_control_outputs(self):
        desc = with_outputs(shipahoy())
        desc["midi-in"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        ttl = files["bundle/shipahoy.ttl"]
        self.assertIn('a atom:AtomPort, lv2:InputPort ;\n        lv2:index 13 ;\n'
                      '        atom:bufferType atom:Sequence ;\n        atom:supports midi:MidiEvent ;\n'
                      '        lv2:symbol "midiIn" ;\n        lv2:name "MIDI In"', ttl)
        self.assertIn("lv2:requiredFeature urid:map ;", ttl)
        self.assertIn("@prefix midi:   <http://lv2plug.in/ns/ext/midi#> .", ttl)
        self.assertIn("c_midiInput{true}", files["header/ports.h"])
        self.assertIn("c_midiIndex{13}", files["header/ports.h"])

    def test_without_control_outputs_it_follows_the_control_inputs(self):
        desc = shipahoy()
        desc["midi-in"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        self.assertIn("c_midiIndex{11}", files["header/ports.h"])

    def test_midi_false_is_the_same_as_no_key(self):
        desc = shipahoy()
        desc["midi-in"] = False
        self.assertEqual(jpgen.generate(jpgen.validate(desc), "x.json"), jpgen.generate(shipahoy(), "x.json"))

    def test_validation(self):
        desc = shipahoy()
        desc["midi-in"] = "yes"
        self.assertInvalid(desc, "midi-in: true or false")
        desc["midi-in"] = 1
        self.assertInvalid(desc, "midi-in: true or false")
        desc = shipahoy()
        desc["midi-in"] = True
        control(desc, "mix")["symbol"] = "midiIn"
        self.assertInvalid(desc, "symbol used twice")


class MidiOutputTest(unittest.TestCase):
    """midi-out: an atom sequence output port that supports MIDI, after the MIDI input."""

    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_a_plugin_without_midi_out_has_no_output_atom_port(self):
        files = jpgen.generate(shipahoy(), "x.json")
        self.assertNotIn("midiOut", files["bundle/shipahoy.ttl"])
        self.assertIn("c_midiOutput{false}", files["header/ports.h"])
        self.assertIn("c_midiOutIndex{0}", files["header/ports.h"])

    def test_the_output_port_follows_the_midi_input(self):
        desc = with_outputs(shipahoy())
        desc["midi-in"] = True
        desc["midi-out"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        ttl = files["bundle/shipahoy.ttl"]
        self.assertIn('a atom:AtomPort, lv2:OutputPort ;\n        lv2:index 14 ;\n'
                      '        atom:bufferType atom:Sequence ;\n        atom:supports midi:MidiEvent ;\n'
                      '        lv2:symbol "midiOut" ;\n        lv2:name "MIDI Out"', ttl)
        self.assertIn("lv2:requiredFeature urid:map ;", ttl)
        self.assertIn("c_midiIndex{13}", files["header/ports.h"])
        self.assertIn("c_midiOutput{true}", files["header/ports.h"])
        self.assertIn("c_midiOutIndex{14}", files["header/ports.h"])

    def test_without_a_midi_input_it_follows_the_control_outputs(self):
        desc = shipahoy()
        desc["midi-out"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        self.assertIn("c_midiInput{false}", files["header/ports.h"])
        self.assertIn("c_midiOutIndex{11}", files["header/ports.h"])

    def test_a_midi_processor_has_no_audio_ports(self):
        desc = shipahoy()
        desc["audio-ports-in"] = []
        desc["audio-ports-out"] = []
        self.assertInvalid(desc, "audio-ports-out: expected a non-empty list")
        desc["midi-in"] = True
        desc["midi-out"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        self.assertIn("c_audioOutputs{0}", files["header/ports.h"])
        self.assertIn("c_midiIndex{9}", files["header/ports.h"])
        self.assertIn("c_midiOutIndex{10}", files["header/ports.h"])

    def test_validation(self):
        desc = shipahoy()
        desc["midi-out"] = "yes"
        self.assertInvalid(desc, "midi-out: true or false")
        desc = shipahoy()
        desc["midi-out"] = True
        control(desc, "mix")["symbol"] = "midiOut"
        self.assertInvalid(desc, "symbol used twice")


class DesignationTest(unittest.TestCase):
    """designation: the host fills a control input with the board's tempo or transport state."""

    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_a_designated_port_says_so_in_the_ttl(self):
        desc = shipahoy()
        control(desc, "mix")["designation"] = "beatsPerMinute"
        ttl = jpgen.generate(jpgen.validate(desc), "x.json")["bundle/shipahoy.ttl"]
        self.assertIn("lv2:designation time:beatsPerMinute", ttl)
        self.assertEqual(ttl.count("lv2:designation"), 1)
        self.assertIn("@prefix time:", ttl)

    def test_speed_is_the_other_designation(self):
        desc = shipahoy()
        control(desc, "mix")["designation"] = "speed"
        self.assertIn("lv2:designation time:speed", jpgen.generate(jpgen.validate(desc), "x.json")["bundle/shipahoy.ttl"])

    def test_an_unknown_designation_is_refused(self):
        desc = shipahoy()
        control(desc, "mix")["designation"] = "bar"
        self.assertInvalid(desc, "designation: one of")

    def test_a_designation_is_used_once(self):
        desc = shipahoy()
        control(desc, "mix")["designation"] = "speed"
        other = next(p for p in desc["ports-control"] if p["symbol"] != "mix")
        other["designation"] = "speed"
        self.assertInvalid(desc, "'speed' is used twice")

    def test_a_plugin_without_a_designation_has_none(self):
        self.assertNotIn("lv2:designation", jpgen.generate(shipahoy(), "x.json")["bundle/shipahoy.ttl"])


class WorkerTest(unittest.TestCase):
    """worker: the LV2 worker extension, required as a host feature and offered as extension data."""

    def assertInvalid(self, desc, fragment):
        with self.assertRaises(jpgen.ValidationError) as caught:
            jpgen.validate(desc)
        self.assertIn(fragment, str(caught.exception))

    def test_a_plugin_without_a_worker_has_no_worker_feature(self):
        files = jpgen.generate(shipahoy(), "x.json")
        self.assertNotIn("work:", files["bundle/shipahoy.ttl"].replace("@prefix work:", ""))
        self.assertIn("c_worker{false}", files["header/ports.h"])

    def test_the_worker_is_a_required_feature_and_extension_data(self):
        desc = shipahoy()
        desc["worker"] = True
        files = jpgen.generate(jpgen.validate(desc), "x.json")
        ttl = files["bundle/shipahoy.ttl"]
        self.assertIn("lv2:requiredFeature work:schedule ;\n    lv2:extensionData work:interface ;", ttl)
        self.assertIn("@prefix work:   <http://lv2plug.in/ns/ext/worker#> .", ttl)
        self.assertIn("c_worker{true}", files["header/ports.h"])

    def test_urid_map_and_the_worker_are_listed_together(self):
        desc = shipahoy()
        desc["worker"] = True
        desc["midi-in"] = True
        ttl = jpgen.generate(jpgen.validate(desc), "x.json")["bundle/shipahoy.ttl"]
        self.assertIn("lv2:requiredFeature urid:map, work:schedule ;", ttl)

    def test_worker_false_is_the_same_as_no_key(self):
        desc = shipahoy()
        desc["worker"] = False
        self.assertEqual(jpgen.generate(jpgen.validate(desc), "x.json"), jpgen.generate(shipahoy(), "x.json"))

    def test_validation(self):
        desc = shipahoy()
        desc["worker"] = "yes"
        self.assertInvalid(desc, "worker: true or false")


class KnobLayoutTest(unittest.TestCase):
    """jpgen.knob_positions() and lower_layout(): where the stylesheet puts knobs and
    footswitch and graphics.py draws labels, title and brand."""

    def knobs(self, count):
        return jpgen.knob_positions(count, jpgen.face_size(count)[0])

    def rows(self, count):
        knobs = self.knobs(count)
        tops = sorted({round(top, 6) for _, top, _ in knobs})
        return [sum(1 for _, top, _ in knobs if round(top, 6) == row) for row in tops]

    def test_rows_and_widths_per_count(self):
        self.assertEqual(jpgen.knob_positions(0, 190), [])
        self.assertEqual([self.rows(n) for n in range(1, 7)], [[1], [2], [2, 1], [2, 2], [3, 2], [3, 3]])
        self.assertEqual([jpgen.face_size(n)[0] for n in range(7)], [190, 190, 230, 230, 230, 280, 280])

    def test_knobs_and_labels_stay_in_the_knob_area_and_rows_are_centred(self):
        top_limit, bottom_limit = jpgen.KNOB_AREA
        for count in range(1, jpgen.MAX_UI_CONTROLS + 1):
            width = jpgen.face_size(count)[0]
            knobs = self.knobs(count)
            self.assertEqual(len(knobs), count)
            for left, top, size in knobs:
                self.assertGreaterEqual(left, jpgen.KNOB_PAD_X, count)
                self.assertLessEqual(left + size, width - jpgen.KNOB_PAD_X, count)
                self.assertGreaterEqual(top, top_limit, count)
                self.assertLessEqual(top + size + jpgen.LABEL_BLOCK, bottom_limit + 1e-9, count)
            for row in {round(top, 6) for _, top, _ in knobs}:
                lefts = [left for left, top, _ in knobs if round(top, 6) == row]
                self.assertAlmostEqual(min(lefts) + max(lefts) + knobs[0][2], width, msg=count)

    def test_block_is_centred_vertically(self):
        top_limit, bottom_limit = jpgen.KNOB_AREA
        for count in range(1, jpgen.MAX_UI_CONTROLS + 1):
            knobs = self.knobs(count)
            first = min(top for _, top, _ in knobs)
            last = max(top + size for _, top, size in knobs) + jpgen.LABEL_BLOCK
            self.assertAlmostEqual(first - top_limit, bottom_limit - last, msg=count)

    def test_three_knobs_form_an_equilateral_triangle(self):
        centres = [(left + size / 2, top + size / 2) for left, top, size in self.knobs(3)]
        pairs = ((centres[0], centres[1]), (centres[1], centres[2]), (centres[2], centres[0]))
        sides = [((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5 for (ax, ay), (bx, by) in pairs]
        self.assertAlmostEqual(min(sides), max(sides))

    def test_five_knobs_sit_in_the_gaps(self):
        knobs = self.knobs(5)
        upper = [left for left, _, _ in knobs[:3]]
        lower = [left for left, _, _ in knobs[3:]]
        self.assertAlmostEqual(lower[0], (upper[0] + upper[1]) / 2)
        self.assertAlmostEqual(lower[1], (upper[1] + upper[2]) / 2)

    def test_lower_area_is_spread_evenly(self):
        lay = jpgen.lower_layout()
        top, bottom = jpgen.LOWER_AREA
        title_top = lay["title_y"] - jpgen.TITLE_HEIGHT / 2
        brand_bottom = lay["brand_y"] + jpgen.BRAND_HEIGHT / 2
        gaps = [title_top - top, lay["footswitch_top"] - title_top - jpgen.TITLE_HEIGHT,
                lay["brand_y"] - jpgen.BRAND_HEIGHT / 2 - lay["footswitch_top"] - jpgen.FOOTSWITCH_SIZE,
                bottom - brand_bottom]
        for gap in gaps:
            self.assertAlmostEqual(gap, gaps[0], delta=0.6)  # the footswitch top is rounded
        self.assertGreater(top, lay["divider_y"])


class CommandLineTest(unittest.TestCase):
    def test_writes_files_and_rewrites_only_changes(self):
        with scratch_dir() as tmp:
            header = Path(tmp) / "gen"
            bundle = Path(tmp) / "bundle"
            args = [str(SHIPAHOY), "--header-dir", str(header), "--bundle-dir", str(bundle)]
            self.assertEqual(jpgen.main(args), 0)
            ports = header / "ports.h"
            first = ports.stat().st_mtime_ns
            self.assertEqual(jpgen.main(args), 0)
            self.assertEqual(ports.stat().st_mtime_ns, first)
            self.assertTrue((bundle / "presets.ttl").exists())

    def test_invalid_file_returns_one(self):
        with scratch_dir() as tmp:
            broken = Path(tmp) / "shipahoy.json"
            desc = shipahoy()
            desc["minor"] = -1
            broken.write_text(json.dumps(desc), encoding="ascii")
            self.assertEqual(jpgen.main([str(broken), "--check"]), 1)


if __name__ == "__main__":
    unittest.main()
