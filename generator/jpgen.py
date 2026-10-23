#!/usr/bin/env python3
"""Generate the data files of one plugin from its JSON description.

Reads plugins/<name>/<name>.json, validates it and writes
  <header-dir>/ports.h        port table for lv2/Lv2Adapter.h (C++17)
  <bundle-dir>/manifest.ttl   plugin, binary and preset index
  <bundle-dir>/<name>.ttl     plugin description and ports (control inputs and outputs)
  <bundle-dir>/presets.ttl    presets (only if the JSON has any)
and with --modgui also
  <bundle-dir>/modgui.ttl, <bundle-dir>/modgui/jaypacks-<name>.html and .css
(the images of the modgui come from graphics.py). Files are rewritten only when their
content changes. The JSON format is described in
docs/plugin-json.md, the naming and the adapter contract in docs/architecture.md. The
generator writes data only (no wrapper code), into the build directory; ports are in port units.

Usage:
  jpgen.py PLUGIN_JSON --header-dir DIR --bundle-dir DIR [--modgui]
  jpgen.py PLUGIN_JSON --check          validate only

Standard library only; runs on Python 3.6 and newer.
"""

import argparse
import json
import math
import re
import string
import sys
from pathlib import Path

URI_BASE = "https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/"
HOMEPAGE = "https://github.com/scjurgen/jaypacks-dwarf-dsp"
AUTHOR = "Juergen Schwietering"
BRAND = "Krachpedal"
BINARY_PREFIX = "jaypacks-"
TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"

# LV2 plugin classes accepted for "plugintype" (lv2core.ttl)
PLUGIN_TYPES = {
    "AllpassPlugin", "AmplifierPlugin", "AnalyserPlugin", "BandpassPlugin", "ChorusPlugin",
    "CombPlugin", "CompressorPlugin", "ConstantPlugin", "ConverterPlugin", "DelayPlugin",
    "DistortionPlugin", "DynamicsPlugin", "EQPlugin", "EnvelopePlugin", "ExpanderPlugin",
    "FilterPlugin", "FlangerPlugin", "FunctionPlugin", "GatePlugin", "GeneratorPlugin",
    "HighpassPlugin", "InstrumentPlugin", "LimiterPlugin", "LowpassPlugin", "MixerPlugin",
    "ModulatorPlugin", "MultiEQPlugin", "OscillatorPlugin", "ParaEQPlugin", "PhaserPlugin",
    "PitchPlugin", "ReverbPlugin", "SimulatorPlugin", "SpatialPlugin", "SpectralPlugin",
    "UtilityPlugin", "WaveshaperPlugin",
}
# JSON unit -> LV2 units: name
UNITS = {"pc": "pc", "hz": "hz", "oct": "oct", "ms": "ms", "s": "s", "db": "db", "bpm": "bpm",
         "semitone": "semitone12TET", "cent": "cent", "m": "m"}
PROPERTIES = {"logarithmic", "enumeration", "integer", "toggled"}

# named face colours; "color" may also be "#rrggbb". The names come from the old
# createBox.py; each colour keeps the hue of the old one at HLS saturation 1, lightness 0.4
# (Wah stays grey: no hue)
COLORS = {
    "Red": "#cc0000", "Orange": "#cc7500", "Chorus": "#0073cc", "Compressor": "#ccbd00", "CrossOver": "#00b7cc",
    "Delay": "#006acc", "Driver": "#cc1000", "Enhance": "#0000cc", "Filter": "#cc004d", "Flanger": "#cc0091",
    "Formant": "#cc0000", "GEQ": "#0000cc", "Gate": "#cc8300", "Megatap": "#00cc4e", "Midi": "#cc6d00",
    "Mixer": "#0041cc", "MultiCompressor": "#cc0007", "MultiDelay": "#0019cc", "Multiplexer": "#00cc00",
    "PEQ": "#cc5e00", "PanTrem": "#00afcc", "Phaser": "#0fcc00", "Pitch": "#cc9100", "PlexDelay": "#00adcc",
    "RTA": "#005bcc", "Reso": "#9500cc", "Reverb": "#6200cc", "Ringmod": "#cc0013", "Rotary": "#cc7100",
    "Synth": "#2fcc00", "TMA": "#00cc78", "Tentap": "#cc007b", "Vocoder": "#00ccbd", "VolPan": "#ccaa00",
    "Wah": "#494949",
}
COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")
# controls on the pedal face: room for six; CSS class per count as mod-ui names them
MAX_UI_CONTROLS = 6
# modgui face in pixels: fixed height, the width grows with the number of face controls.
# modgui:model stays "boxy", the model name of the old stylesheet
MODEL = "boxy"
FACE_HEIGHT = 431
FACE_WIDTHS = {0: 190, 1: 190, 2: 230, 3: 230, 4: 230, 5: 280, 6: 280}
# face areas: the knobs above the divider; title, footswitch and brand below it
KNOB_AREA = (16.0, 206.0)       # top, bottom
DIVIDER_Y = 214.0
LOWER_AREA = (218.0, 420.0)     # top, bottom
KNOB_PAD_X = 12.0               # keeps knobs off the bevel and the rounded corners
LABEL_BLOCK = 18.0              # gap under a knob plus one label line
KNOB_SIZES = {1: 98, 2: 90, 3: 68, 4: 60, 5: 60, 6: 60}
KNOB_ROWS = {1: [1], 2: [2], 3: [2, 1], 4: [2, 2], 5: [3, 2], 6: [3, 3]}
FOOTSWITCH_SIZE = 64
TITLE_HEIGHT = 26.0
BRAND_HEIGHT = 14.0
PORTS_TOP = 50                  # top of the audio jacks (old stylesheet)


def face_size(count):
    """(width, height) of the face for `count` face controls."""
    return FACE_WIDTHS[count], FACE_HEIGHT


def knob_positions(count, width):
    """(left, top, size) per face control in face pixels; the stylesheet places the knobs
    there and graphics.py draws the labels and the screenshot knobs at the same spots.

    Rows per count (KNOB_ROWS), centred horizontally: the knobs of a row are spread evenly
    over the padded width of the widest row, narrower rows sit in its gaps (5: three over
    two). Three knobs form an equilateral triangle, spaced so that the upper labels clear the
    lower knob. The block is centred in the knob area with equal gaps between the rows."""
    if count == 0:
        return []
    size = KNOB_SIZES[count]
    rows = KNOB_ROWS[count]
    cell = size + LABEL_BLOCK
    top, bottom = KNOB_AREA
    height = bottom - top
    widest = max(rows)
    pitch_x = size + (width - 2 * KNOB_PAD_X - widest * size) / (widest + 1)
    if count == 3:
        pitch_x = max(pitch_x, (size + LABEL_BLOCK + 4) / (math.sqrt(3) / 2))
        pitch_y = pitch_x * math.sqrt(3) / 2
    elif len(rows) > 1:
        pitch_y = cell + (height - len(rows) * cell) / (len(rows) + 1)
    else:
        pitch_y = 0.0
    first = top + (height - (len(rows) - 1) * pitch_y - cell) / 2
    knobs = []
    for row, n in enumerate(rows):
        for j in range(n):
            centre = width / 2 + (j - (n - 1) / 2) * pitch_x
            knobs.append((centre - size / 2, first + row * pitch_y, size))
    return knobs


def lower_layout():
    """Lower area, spread evenly from top to bottom: title (centre y), footswitch (top,
    whole pixels for the stylesheet), brand (centre y)."""
    top, bottom = LOWER_AREA
    items = [TITLE_HEIGHT, FOOTSWITCH_SIZE, BRAND_HEIGHT]
    gap = (bottom - top - sum(items)) / (len(items) + 1)
    title = top + gap
    footswitch = title + TITLE_HEIGHT + gap
    brand = footswitch + FOOTSWITCH_SIZE + gap
    return {"title_y": title + TITLE_HEIGHT / 2, "footswitch_top": round(footswitch),
            "brand_y": brand + BRAND_HEIGHT / 2, "divider_y": DIVIDER_Y}


KNOB_CLASSES = ["mod-no-knobs", "mod-one-knob", "mod-two-knobs", "mod-three-knobs", "mod-four-knobs",
                "mod-five-knobs", "mod-six-knobs"]

TOP_KEYS = {"name", "displayname", "status", "plugintype", "color", "major", "minor", "micro",
            "shortdescription", "description", "audio-ports-in", "audio-ports-out", "ports-control",
            "ports-control-out", "presets", "CPP", "midi-in", "midi-out", "worker"}
TOP_REQUIRED = TOP_KEYS - {"presets", "status", "ports-control-out", "midi-in", "midi-out", "worker"}
# the MIDI input port (an atom sequence that supports midi:MidiEvent) comes after the control outputs, the MIDI
# output port after it
MIDI_SYMBOL = "midiIn"
MIDI_NAME = "MIDI In"
MIDI_OUT_SYMBOL = "midiOut"
MIDI_OUT_NAME = "MIDI Out"
AUDIO_KEYS = {"name", "symbol"}
CONTROL_KEYS = {"hasUI", "display", "symbol", "default", "minimum", "maximum", "unit", "property",
                "listitems", "designation"}
# a control input the host fills with the board's tempo or transport state (lv2:designation of the LV2 time extension)
DESIGNATIONS = {"beatsPerMinute": "time:beatsPerMinute", "speed": "time:speed"}
CONTROL_REQUIRED = {"hasUI", "display", "symbol", "default"}
# control outputs (measurements the plugin reports): no default, no face control
OUTPUT_KEYS = {"display", "symbol", "minimum", "maximum", "unit", "property"}
OUTPUT_REQUIRED = {"display", "symbol", "minimum", "maximum"}
OUTPUT_PROPERTIES = {"integer"}
CPP_KEYS = {"CLASS_NAME", "INCLUDE"}

NAME_RE = re.compile(r"^[a-z][a-z0-9]*$")
SYMBOL_RE = re.compile(r"^[a-z][A-Za-z0-9]*$")
CLASS_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
INCLUDE_RE = re.compile(r"^[A-Za-z0-9_./-]+\.h$")
MOD_LABEL_MAX = 16


class ValidationError(Exception):
    """The JSON description is not valid; the message names the field."""


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def check_ascii(where, text):
    if not isinstance(text, str):
        raise ValidationError(f"{where}: expected a string")
    if any(ord(c) > 126 or (ord(c) < 32) for c in text):
        raise ValidationError(f"{where}: only printable ASCII is allowed")


def check_keys(where, item, allowed, required):
    if not isinstance(item, dict):
        raise ValidationError(f"{where}: expected an object")
    unknown = sorted(set(item) - allowed)
    if unknown:
        raise ValidationError(f"{where}: unknown keys {unknown}")
    missing = sorted(required - set(item))
    if missing:
        raise ValidationError(f"{where}: missing keys {missing}")


def control_range(port):
    """(minimum, maximum) of a control; enumerations span their list."""
    if port.get("property") == "enumeration":
        return 0, len(port["listitems"]) - 1
    return port["minimum"], port["maximum"]


def is_discrete(port):
    return port.get("property") in ("enumeration", "integer", "toggled")


def validate(desc):
    """Raise ValidationError for the first problem found; return desc unchanged."""
    check_keys("plugin", desc, TOP_KEYS, TOP_REQUIRED)
    if not isinstance(desc["name"], str) or not NAME_RE.match(desc["name"]):
        raise ValidationError("name: lower case letters and digits, starting with a letter")
    check_ascii("displayname", desc["displayname"])
    if not desc["displayname"] or len(desc["displayname"]) > MOD_LABEL_MAX:
        raise ValidationError(f"displayname: 1 to {MOD_LABEL_MAX} characters (mod:label)")
    if desc["plugintype"] not in PLUGIN_TYPES:
        raise ValidationError(f"plugintype: '{desc['plugintype']}' is not an LV2 plugin class")
    for key in ("major", "minor", "micro"):
        if not isinstance(desc[key], int) or isinstance(desc[key], bool) or desc[key] < 0:
            raise ValidationError(f"{key}: expected an integer >= 0")
    if "status" in desc:
        check_ascii("status", desc["status"])
    if not isinstance(desc["color"], str) or not (desc["color"] in COLORS or COLOR_RE.match(desc["color"])):
        raise ValidationError("color: a name from COLORS in jpgen.py or #rrggbb")
    check_ascii("shortdescription", desc["shortdescription"])
    if not isinstance(desc["description"], list) or not desc["description"]:
        raise ValidationError("description: expected a non-empty list of strings")
    for i, line in enumerate(desc["description"]):
        check_ascii(f"description[{i}]", line)

    symbols = set()
    for key, symbol in (("midi-in", MIDI_SYMBOL), ("midi-out", MIDI_OUT_SYMBOL)):
        if key in desc:
            if desc[key] is not True and desc[key] is not False:
                raise ValidationError(f"{key}: true or false")
            if desc[key]:
                symbols.add(symbol)
    if "worker" in desc and desc["worker"] is not True and desc["worker"] is not False:
        raise ValidationError("worker: true or false")
    for group in ("audio-ports-in", "audio-ports-out"):
        # a signal source has no audio input; every plugin has an audio output unless it has a MIDI output
        needs_output = group == "audio-ports-out" and not desc.get("midi-out", False)
        if not isinstance(desc[group], list) or (needs_output and not desc[group]):
            raise ValidationError(f"{group}: expected a {'non-empty ' if needs_output else ''}list")
        for i, port in enumerate(desc[group]):
            where = f"{group}[{i}]"
            check_keys(where, port, AUDIO_KEYS, AUDIO_KEYS)
            check_ascii(f"{where}.name", port["name"])
            if not isinstance(port["symbol"], str) or not SYMBOL_RE.match(port["symbol"]):
                raise ValidationError(f"{where}.symbol: letters and digits, starting lower case")
            if port["symbol"] in symbols:
                raise ValidationError(f"{where}.symbol: '{port['symbol']}' is used twice")
            symbols.add(port["symbol"])

    controls = desc["ports-control"]
    if not isinstance(controls, list):
        raise ValidationError("ports-control: expected a list")
    designations = set()
    for i, port in enumerate(controls):
        where = f"ports-control[{i}]"
        check_keys(where, port, CONTROL_KEYS, CONTROL_REQUIRED)
        symbol = port["symbol"]
        if not isinstance(symbol, str) or not SYMBOL_RE.match(symbol):
            raise ValidationError(f"{where}.symbol: letters and digits, starting lower case")
        where = f"ports-control '{symbol}'"
        if symbol in symbols:
            raise ValidationError(f"{where}: symbol used twice")
        symbols.add(symbol)
        check_ascii(f"{where}.display", port["display"])
        if port["hasUI"] not in (0, 1) or isinstance(port["hasUI"], bool):
            raise ValidationError(f"{where}.hasUI: 0 or 1")
        prop = port.get("property")
        if prop is not None and prop not in PROPERTIES:
            raise ValidationError(f"{where}.property: one of {sorted(PROPERTIES)}")
        if "unit" in port and port["unit"] not in UNITS:
            raise ValidationError(f"{where}.unit: one of {sorted(UNITS)}")
        if "designation" in port:
            if port["designation"] not in DESIGNATIONS:
                raise ValidationError(f"{where}.designation: one of {sorted(DESIGNATIONS)}")
            if port["designation"] in designations:
                raise ValidationError(f"{where}.designation: '{port['designation']}' is used twice")
            designations.add(port["designation"])
        if prop == "enumeration":
            items = port.get("listitems")
            if not isinstance(items, list) or not items:
                raise ValidationError(f"{where}.listitems: required for an enumeration")
            for k, item in enumerate(items):
                check_ascii(f"{where}.listitems[{k}]", item)
            if len(set(items)) != len(items):
                raise ValidationError(f"{where}.listitems: duplicate entries")
            for key in ("minimum", "maximum"):
                if key in port and port[key] != control_range(port)[key == "maximum"]:
                    raise ValidationError(f"{where}.{key}: an enumeration spans 0..len(listitems)-1")
        else:
            if "listitems" in port:
                raise ValidationError(f"{where}.listitems: only for property 'enumeration'")
            for key in ("minimum", "maximum"):
                if key not in port or not is_number(port[key]):
                    raise ValidationError(f"{where}.{key}: number required")
            if port["minimum"] >= port["maximum"]:
                raise ValidationError(f"{where}: minimum must be below maximum")
            if prop == "logarithmic" and port["minimum"] <= 0:
                raise ValidationError(f"{where}: a logarithmic port needs minimum > 0")
        check_value(where + ".default", port, port["default"])
    if len(ui_controls(desc)) > MAX_UI_CONTROLS:
        raise ValidationError(f"ports-control: at most {MAX_UI_CONTROLS} controls with hasUI 1 (pedal face)")

    outputs = desc.get("ports-control-out", [])
    if not isinstance(outputs, list):
        raise ValidationError("ports-control-out: expected a list")
    for i, port in enumerate(outputs):
        where = f"ports-control-out[{i}]"
        check_keys(where, port, OUTPUT_KEYS, OUTPUT_REQUIRED)
        symbol = port["symbol"]
        if not isinstance(symbol, str) or not SYMBOL_RE.match(symbol):
            raise ValidationError(f"{where}.symbol: letters and digits, starting lower case")
        where = f"ports-control-out '{symbol}'"
        if symbol in symbols:
            raise ValidationError(f"{where}: symbol used twice")
        symbols.add(symbol)
        check_ascii(f"{where}.display", port["display"])
        if "property" in port and port["property"] not in OUTPUT_PROPERTIES:
            raise ValidationError(f"{where}.property: one of {sorted(OUTPUT_PROPERTIES)}")
        if "unit" in port and port["unit"] not in UNITS:
            raise ValidationError(f"{where}.unit: one of {sorted(UNITS)}")
        for key in ("minimum", "maximum"):
            if not is_number(port[key]):
                raise ValidationError(f"{where}.{key}: number required")
        if port["minimum"] >= port["maximum"]:
            raise ValidationError(f"{where}: minimum must be below maximum")

    names = set()
    for i, preset in enumerate(desc.get("presets", [])):
        where = f"presets[{i}]"
        check_keys(where, preset, {"name", "values"}, {"name", "values"})
        check_ascii(f"{where}.name", preset["name"])
        if preset["name"] in names:
            raise ValidationError(f"{where}.name: '{preset['name']}' is used twice")
        names.add(preset["name"])
        by_symbol = {p["symbol"]: p for p in controls}
        if not isinstance(preset["values"], dict):
            raise ValidationError(f"{where}.values: expected an object")
        for symbol, value in preset["values"].items():
            if symbol not in by_symbol:
                raise ValidationError(f"{where}.values: unknown control '{symbol}'")
            check_value(f"{where}.values.{symbol}", by_symbol[symbol], value)

    check_keys("CPP", desc["CPP"], CPP_KEYS, CPP_KEYS)
    if not CLASS_RE.match(desc["CPP"]["CLASS_NAME"]):
        raise ValidationError("CPP.CLASS_NAME: a C++ class name")
    if not INCLUDE_RE.match(desc["CPP"]["INCLUDE"]):
        raise ValidationError("CPP.INCLUDE: a header path relative to the repository root")
    return desc


def ui_controls(desc):
    """Controls shown on the pedal face, in JSON order."""
    return [port for port in desc["ports-control"] if port["hasUI"] == 1]


def check_value(where, port, value):
    if not is_number(value):
        raise ValidationError(f"{where}: number required")
    low, high = control_range(port)
    if not low <= value <= high:
        raise ValidationError(f"{where}: {value} outside [{low}, {high}]")
    if is_discrete(port) and value != int(value):
        raise ValidationError(f"{where}: {value} must be an integer")


def turtle_string(text):
    """Contents of a Turtle "..." literal."""
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def number(value, integer=False):
    if integer:
        return str(int(value))
    text = repr(float(value))
    return text


def uri_of(desc):
    return URI_BASE + desc["name"]


def preset_uri(desc, preset):
    slug = re.sub(r"[^a-z0-9]+", "-", preset["name"].lower()).strip("-")
    return f"{uri_of(desc)}#preset-{slug}"


def audio_port_ttl(index, port, direction):
    return (f'[\n        a lv2:AudioPort, lv2:{direction} ;\n        lv2:index {index} ;\n'
            f'        lv2:symbol "{port["symbol"]}" ;\n        lv2:name "{turtle_string(port["name"])}"\n    ]')


def control_port_ttl(index, port):
    low, high = control_range(port)
    discrete = is_discrete(port)
    lines = [
        "a lv2:ControlPort, lv2:InputPort",
        f"lv2:index {index}",
        f'lv2:symbol "{port["symbol"]}"',
        f'lv2:name "{turtle_string(port["display"])}"',
        f"lv2:default {number(port['default'], discrete)}",
        f"lv2:minimum {number(low, discrete)}",
        f"lv2:maximum {number(high, discrete)}",
    ]
    if "unit" in port:
        lines.append(f"units:unit units:{UNITS[port['unit']]}")
    if "designation" in port:
        lines.append(f"lv2:designation {DESIGNATIONS[port['designation']]}")
    prop = port.get("property")
    if prop == "enumeration":
        lines.append("lv2:portProperty lv2:integer, lv2:enumeration")
        points = ",\n            ".join(
            f'[ rdfs:label "{turtle_string(item)}" ; rdf:value {k} ]' for k, item in enumerate(port["listitems"]))
        lines.append(f"lv2:scalePoint {points}")
    elif prop == "logarithmic":
        lines.append("lv2:portProperty pprops:logarithmic")
    elif prop in ("integer", "toggled"):
        lines.append(f"lv2:portProperty lv2:{prop}")
    body = " ;\n        ".join(lines)
    return f"[\n        {body}\n    ]"


def output_port_ttl(index, port):
    integer = port.get("property") == "integer"
    lines = [
        "a lv2:ControlPort, lv2:OutputPort",
        f"lv2:index {index}",
        f'lv2:symbol "{port["symbol"]}"',
        f'lv2:name "{turtle_string(port["display"])}"',
        f"lv2:minimum {number(port['minimum'], integer)}",
        f"lv2:maximum {number(port['maximum'], integer)}",
    ]
    if "unit" in port:
        lines.append(f"units:unit units:{UNITS[port['unit']]}")
    if integer:
        lines.append("lv2:portProperty lv2:integer")
    body = " ;\n        ".join(lines)
    return f"[\n        {body}\n    ]"


def midi_port_ttl(index, output=False):
    direction, symbol, name = ("OutputPort", MIDI_OUT_SYMBOL, MIDI_OUT_NAME) if output else (
        "InputPort", MIDI_SYMBOL, MIDI_NAME)
    return (f'[\n        a atom:AtomPort, lv2:{direction} ;\n        lv2:index {index} ;\n'
            f'        atom:bufferType atom:Sequence ;\n        atom:supports midi:MidiEvent ;\n'
            f'        lv2:symbol "{symbol}" ;\n        lv2:name "{name}"\n    ]')


def getter_of(symbol):
    return "get" + symbol[0].upper() + symbol[1:]


def render(template_name, **values):
    template = string.Template((TEMPLATE_DIR / template_name).read_text(encoding="ascii"))
    return template.substitute(**values)


def modgui_files(desc, json_path):
    """{relative output name: text} of the modgui description, icon template and stylesheet."""
    name = desc["name"]
    ui = ui_controls(desc)
    count = len(ui)
    width, height = face_size(count)
    html = f"modgui/{BINARY_PREFIX}{name}.html"
    css = f"modgui/{BINARY_PREFIX}{name}.css"
    ports = ""
    if ui:
        ports = " ;\n        modgui:port " + ",\n            ".join(
            f'[ lv2:index {k} ; lv2:symbol "{port["symbol"]}" ; lv2:name "{turtle_string(port["display"])}" ]'
            for k, port in enumerate(ui))
    # mod-ui only asks mod-host for the values of output ports listed here
    outputs = desc.get("ports-control-out", [])
    if outputs:
        ports += " ;\n        modgui:monitoredOutputs " + ",\n            ".join(
            f'[ lv2:symbol "{port["symbol"]}" ]' for port in outputs)
    files = {
        "bundle/modgui.ttl": render(
            "modgui.ttl", json_path=json_path, uri=uri_of(desc), html=html, css=css, brand=BRAND,
            label=turtle_string(desc["displayname"]), model=MODEL, panel=f"{count}-knobs", ports=ports),
        "bundle/" + html: render("modgui/icon.html", json_path=json_path, knobs_class=KNOB_CLASSES[count],
                                 model=MODEL),
    }
    # absolute positions: one rule per knob (mod-ui renders the knobs as siblings in the
    # control group, in face-control order)
    knob_rules = "".join(
        f".mod-pedal-boxy{{{{{{cns}}}}}} .mod-control-group .mod-knob:nth-child({k + 1}) {{\n"
        f"    height:{size}px;\n    left:{left:.1f}px;\n    top:{top:.1f}px;\n    width:{size}px;\n}}\n"
        f".mod-pedal-boxy{{{{{{cns}}}}}} .mod-control-group .mod-knob:nth-child({k + 1}) .mod-knob-image {{\n"
        f"    background-size:auto {size}px;\n    height:{size}px;\n    width:{size}px;\n}}\n"
        for k, (left, top, size) in enumerate(knob_positions(count, width)))
    files["bundle/" + css] = render(
        "modgui/icon.css", json_path=json_path, width=width, height=height, ports_top=PORTS_TOP,
        footswitch_left=(width - FOOTSWITCH_SIZE) // 2, footswitch_top=lower_layout()["footswitch_top"],
        knob_rules=knob_rules)
    return files


def generate(desc, json_path, modgui=False):
    """Return {relative output name: text} for the bundle and the header; with modgui also
    the modgui text files (the images come from graphics.py)."""
    name = desc["name"]
    uri = uri_of(desc)
    audio_in = desc["audio-ports-in"]
    audio_out = desc["audio-ports-out"]
    controls = desc["ports-control"]
    outputs = desc.get("ports-control-out", [])

    ports = []
    index = 0
    for port in audio_in:
        ports.append(audio_port_ttl(index, port, "InputPort"))
        index += 1
    for port in audio_out:
        ports.append(audio_port_ttl(index, port, "OutputPort"))
        index += 1
    first_control = index
    for port in controls:
        ports.append(control_port_ttl(index, port))
        index += 1
    first_output = index
    for port in outputs:
        ports.append(output_port_ttl(index, port))
        index += 1
    midi_in = bool(desc.get("midi-in", False))
    midi_index = index
    if midi_in:
        ports.append(midi_port_ttl(index))
        index += 1
    midi_out = bool(desc.get("midi-out", False))
    midi_out_index = index
    if midi_out:
        ports.append(midi_port_ttl(index, output=True))
        index += 1
    worker = bool(desc.get("worker", False))
    features = (["urid:map"] if midi_in or midi_out else []) + (["work:schedule"] if worker else [])

    comment = "\n".join(desc["description"])
    files = {}
    files["bundle/" + name + ".ttl"] = render(
        "plugin.ttl", json_path=json_path, uri=uri, plugin_type=desc["plugintype"],
        display_name=turtle_string(desc["displayname"]), author=AUTHOR, homepage=HOMEPAGE, brand=BRAND,
        label=turtle_string(desc["displayname"]), minor=desc["minor"], micro=desc["micro"],
        comment=turtle_string(comment), ports=", ".join(ports),
        required_feature=(f"\n    lv2:requiredFeature {', '.join(features)} ;" if features else "") +
        ("\n    lv2:extensionData work:interface ;" if worker else ""))

    preset_index = ""
    preset_bodies = ""
    for preset in desc.get("presets", []):
        puri = preset_uri(desc, preset)
        preset_index += (f"\n<{puri}>\n    a pset:Preset ;\n    lv2:appliesTo <{uri}> ;\n"
                         f'    rdfs:label "{turtle_string(preset["name"])}" ;\n    rdfs:seeAlso <presets.ttl> .\n')
        values = ",\n        ".join(
            f'[ lv2:symbol "{symbol}" ; pset:value {number(value)} ]'
            for symbol, value in preset["values"].items())
        preset_bodies += (f"\n<{puri}>\n    a pset:Preset ;\n    lv2:appliesTo <{uri}> ;\n"
                          f'    rdfs:label "{turtle_string(preset["name"])}" ;\n    lv2:port {values} .\n')
    files["bundle/manifest.ttl"] = render(
        "manifest.ttl", json_path=json_path, uri=uri, binary=f"{BINARY_PREFIX}{name}.so",
        plugin_ttl=f"{name}.ttl", modgui=" ;\n    rdfs:seeAlso <modgui.ttl>" if modgui else "", presets=preset_index)
    if preset_bodies:
        files["bundle/presets.ttl"] = render("presets.ttl", json_path=json_path, presets=preset_bodies)

    entries = []
    for k, port in enumerate(controls):
        low, high = control_range(port)
        symbol = port["symbol"]
        setter = "set" + symbol[0].upper() + symbol[1:]
        entries.append(f'        {{{first_control + k}, "{symbol}", {cpp_float(low)}, {cpp_float(high)}, '
                       f'{cpp_float(port["default"])}, &Pedal::{setter}}},')
    output_entries = [f'        {{{first_output + k}, "{port["symbol"]}", &Pedal::{getter_of(port["symbol"])}}},'
                      for k, port in enumerate(outputs)]
    files["header/ports.h"] = render(
        "ports.h.in", json_path=json_path, name=name, include=desc["CPP"]["INCLUDE"],
        struct_name=name[0].upper() + name[1:] + "Ports", class_name=desc["CPP"]["CLASS_NAME"], uri=uri,
        version=f"{desc['major']}.{desc['minor']}.{desc['micro']}", audio_inputs=len(audio_in),
        audio_outputs=len(audio_out), control_count=len(controls), controls="\n".join(entries),
        output_count=len(outputs), outputs="\n".join(output_entries),
        midi_input="true" if midi_in else "false", midi_index=midi_index if midi_in else 0,
        midi_output="true" if midi_out else "false", midi_out_index=midi_out_index if midi_out else 0,
        worker="true" if worker else "false")
    if modgui:
        files.update(modgui_files(desc, json_path))
    return files


def cpp_float(value):
    text = repr(float(value))
    if "e" not in text and "." not in text:
        text += ".0"
    return text + "f"


def write_if_changed(path, text):
    if path.exists() and path.read_text(encoding="ascii") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")
    return True


def load(json_path):
    try:
        with open(json_path, encoding="ascii") as handle:
            return json.load(handle)
    except UnicodeDecodeError as error:
        raise ValidationError(f"{json_path}: not ASCII ({error})")
    except json.JSONDecodeError as error:
        raise ValidationError(f"{json_path}: invalid JSON ({error})")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("json", type=Path, help="plugins/<name>/<name>.json")
    parser.add_argument("--header-dir", type=Path, help="directory for ports.h")
    parser.add_argument("--bundle-dir", type=Path, help="LV2 bundle directory for the TTL files")
    parser.add_argument("--modgui", action="store_true", help="also write modgui.ttl, the icon template and the stylesheet")
    parser.add_argument("--check", action="store_true", help="validate only")
    args = parser.parse_args(argv)
    if not args.check and (args.header_dir is None or args.bundle_dir is None):
        parser.error("--header-dir and --bundle-dir are required unless --check is given")
    try:
        desc = validate(load(args.json))
        if args.json.stem != desc["name"]:
            raise ValidationError(f"name '{desc['name']}' does not match the file name '{args.json.name}'")
    except ValidationError as error:
        print(f"jpgen: {args.json}: {error}", file=sys.stderr)
        return 1
    if args.check:
        return 0
    try:
        shown_path = args.json.resolve().relative_to(Path(__file__).resolve().parent.parent).as_posix()
    except ValueError:
        shown_path = args.json.name
    files = generate(desc, shown_path, args.modgui)
    for relative, text in files.items():
        kind, filename = relative.split("/", 1)
        target = (args.header_dir if kind == "header" else args.bundle_dir) / filename
        if write_if_changed(target, text):
            print(f"jpgen: wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
