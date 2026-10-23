#!/usr/bin/env python3
"""Check the modgui of every bundle in an LV2 directory (ctest: host.modgui).

For each jaypacks-*.lv2 with a modgui.ttl: every file modgui.ttl names exists; the face
controls are control inputs of the plugin, indexed 0..n-1, and panel and icon template agree
on their number; the monitored outputs are exactly the control outputs of the plugin; every /resources/ image of the stylesheet exists in the resources
directory and no file there is unused; the images have the expected sizes (face as
jpgen.face_size() gives for the number of face controls, screenshot as high as the face and
wider by the jacks, thumbnail 64 px high, knob filmstrip of square frames, footswitch
64 x 128). With --require, a bundle without modgui is
an error too.

Usage:
  modgui_check.py LV2_DIR [--require]

Standard library only.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "generator"))
import jpgen  # noqa: E402

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(24)
    if len(head) < 24 or head[:8] != PNG_SIGNATURE or head[12:16] != b"IHDR":
        raise ValueError(f"{path.name}: not a PNG file")
    return struct.unpack(">II", head[16:24])


def one(pattern, text, what):
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"modgui.ttl: no {what}")
    return match.group(1)


def control_inputs(plugin_ttl):
    pattern = r'a lv2:ControlPort, lv2:InputPort ;\s*lv2:index \d+ ;\s*lv2:symbol "([^"]+)"'
    return set(re.findall(pattern, plugin_ttl))


def control_outputs(plugin_ttl):
    pattern = r'a lv2:ControlPort, lv2:OutputPort ;\s*lv2:index \d+ ;\s*lv2:symbol "([^"]+)"'
    return set(re.findall(pattern, plugin_ttl))


def check_bundle(bundle):
    """List of problems of one bundle (empty: fine)."""
    problems = []
    manifest = (bundle / "manifest.ttl").read_text(encoding="ascii")
    ttl = (bundle / "modgui.ttl").read_text(encoding="ascii")
    if "rdfs:seeAlso <modgui.ttl>" not in manifest:
        problems.append("manifest.ttl does not list modgui.ttl")
    resources = bundle / one(r"modgui:resourcesDirectory <([^>]+)>", ttl, "resourcesDirectory")
    named = {}
    for key in ("iconTemplate", "stylesheet", "screenshot", "thumbnail"):
        named[key] = bundle / one(rf"modgui:{key} <([^>]+)>", ttl, key)
        if not named[key].is_file():
            problems.append(f"{key} {named[key].name} is missing")
    if problems:
        return problems

    model = one(r'modgui:model "([^"]+)"', ttl, "model")
    panel = one(r'modgui:panel "([^"]+)"', ttl, "panel")
    ports = [(int(index), symbol) for index, symbol in
             re.findall(r'\[ lv2:index (\d+) ; lv2:symbol "([^"]+)"', ttl)]
    plugin_ttl = (bundle / one(r"rdfs:seeAlso <([^>]+\.ttl)>", manifest, "plugin TTL")).read_text(encoding="ascii")
    controls = control_inputs(plugin_ttl)
    if [index for index, _ in ports] != list(range(len(ports))):
        problems.append(f"modgui:port indices {[index for index, _ in ports]} are not 0..n-1")
    for _, symbol in ports:
        if symbol not in controls:
            problems.append(f"modgui:port '{symbol}' is not a control input of the plugin")
    monitored_block = re.search(r"modgui:monitoredOutputs ((?:\s*,?\s*\[ lv2:symbol \"[^\"]+\" \])+)", ttl)
    monitored = re.findall(r'lv2:symbol "([^"]+)"', monitored_block.group(1)) if monitored_block else []
    if set(monitored) != control_outputs(plugin_ttl) or len(set(monitored)) != len(monitored):
        problems.append(f"monitoredOutputs {monitored} differ from the control outputs "
                        f"{sorted(control_outputs(plugin_ttl))}")
    if len({symbol for _, symbol in ports}) != len(ports):
        problems.append("a control appears twice on the face")
    if len(ports) > jpgen.MAX_UI_CONTROLS:
        problems.append(f"{len(ports)} face controls, at most {jpgen.MAX_UI_CONTROLS}")
    elif panel != f"{len(ports)}-knobs":
        problems.append(f"panel '{panel}' for {len(ports)} face controls")
    html = named["iconTemplate"].read_text(encoding="ascii")
    if len(ports) <= jpgen.MAX_UI_CONTROLS and f" {jpgen.KNOB_CLASSES[len(ports)]} " not in html:
        problems.append(f"icon template lacks the class {jpgen.KNOB_CLASSES[len(ports)]}")
    for role in ("input-control-port", "bypass", "input-audio-port", "input-midi-port", "output-audio-port",
                 "output-midi-port"):
        if f'mod-role="{role}"' not in html:
            problems.append(f"icon template has no mod-role {role}")

    css = named["stylesheet"].read_text(encoding="ascii")
    used = set(re.findall(r"url\(/resources/([^){]+)\{\{\{ns\}\}\}\)", css))
    for relative in sorted(used):
        if not (resources / relative).is_file():
            problems.append(f"stylesheet image {relative} is missing")
    used |= {path.relative_to(resources).as_posix() for path in named.values()}
    for path in sorted(resources.rglob("*")):
        if path.is_file() and path.relative_to(resources).as_posix() not in used:
            problems.append(f"unused file modgui/{path.relative_to(resources).as_posix()}")

    try:
        if model != jpgen.MODEL:
            problems.append(f"model '{model}', expected '{jpgen.MODEL}'")
        if len(ports) <= jpgen.MAX_UI_CONTROLS:
            expected = jpgen.face_size(len(ports))
            if png_size(resources / "face.png") != expected:
                problems.append(f"face.png is {png_size(resources / 'face.png')}, "
                                f"{len(ports)} face controls need {expected}")
        face_width, face_height = png_size(resources / "face.png")
        shot_width, shot_height = png_size(named["screenshot"])
        if shot_height != face_height or shot_width <= face_width:
            problems.append(f"screenshot {shot_width}x{shot_height} does not fit the face {face_width}x{face_height}")
        thumbnail = png_size(named["thumbnail"])
        if thumbnail != (round(64 * shot_width / shot_height), 64):
            problems.append(f"thumbnail is {thumbnail}")
        strip_width, strip_height = png_size(resources / "knobs/boxy/white.png")
        if strip_width % strip_height or strip_width // strip_height < 2:
            problems.append(f"knob filmstrip {strip_width}x{strip_height} is not a row of square frames")
        if png_size(resources / "pedals/onoff_white.png") != (64, 128):
            problems.append("footswitch image is not 64 x 128")
    except (OSError, ValueError) as error:
        problems.append(str(error))
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("lv2_dir", type=Path)
    parser.add_argument("--require", action="store_true", help="every bundle must have a modgui")
    args = parser.parse_args(argv)
    bundles = sorted(args.lv2_dir.glob("jaypacks-*.lv2"))
    if not bundles:
        print(f"modgui_check: no bundles in {args.lv2_dir}")
        return 1
    failed = 0
    for bundle in bundles:
        if not (bundle / "modgui.ttl").exists():
            problems = ["no modgui.ttl"] if args.require else []
            if not args.require:
                print(f"modgui_check: {bundle.name}: no modgui, skipped")
        else:
            try:
                problems = check_bundle(bundle)
            except (OSError, ValueError) as error:
                problems = [str(error)]
        for problem in problems:
            print(f"modgui_check: {bundle.name}: {problem}")
        if problems:
            failed += 1
        else:
            print(f"modgui_check: {bundle.name}: ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
