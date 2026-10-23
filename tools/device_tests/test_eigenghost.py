#!/usr/bin/env python3
"""Device integration test of eigenghost on the Dwarf (roadmap step 16).

Needs siggen, meter and eigenghost installed (tools/jp.py test-device eigenghost deploys them)
and the Mac test build for the expected wet levels (build-tests, target eigenghost_expected).
Empties the graph and builds
  siggen/out -> eigenghost/in, eigenghost/outL -> meter/signal, siggen/out -> meter/reference
  eigenghost/outR -> meterR/signal, eigenghost/outL -> meterR/reference
(nothing goes to the audio outputs), then checks with the meters' readings:
  - CPU: mean mod-ui CPU with eigenghost at its defaults against the same graph before it
    was added,
  - wet at its minimum gives exactly the input on the left output; bypass on gives exactly
    the input, off does not; width 0 gives exactly the left output on the right one, width
    100 % decorrelates the high bands,
  - the wet level of white noise relative to the input at decay 0.5, 3 and 10 s against the
    pedal on the Mac (eigenghost_expected),
  - pitch lines a fifth up raise the level at the fifth of a 237 Hz tone,
  - mod depth 20 cents raises the residual beside a held 1 kHz tone against depth 0,
  - jumps between dry only and wet only every 50 ms stay below the sum of the largest steps of
    the two steady outputs (largest sample step, the zipper metric; during a jump the output
    is a crossfade of two sines of different phase, which can briefly be louder than either),
  - decay jumps and size sweeps, every control at its minimum and at its maximum, and silence
    after noise give no non-finite and no denormal samples (the click measure of decay and
    size changes is the Mac host test: the meter's bands cannot separate a click from the
    reverb's own change),
  - knob 1 -> wet, knob 2 -> decay and footswitch B -> bypass addressed, confirmed by mod-ui's
    hw_map messages,
  - xruns unchanged while the graph processes, no problem lines in the jack2 journal,
then leaves the board loaded with the addressing (with --restore: reloads the previous
pedalboard). Raw messages go to build-dwarf/scratch/test_eigenghost.log.

Usage:
  test_eigenghost.py [--restore]
"""

import argparse
import json
import math
import subprocess
import sys
import time

import devicetest
from devicetest import FLOOR_DB, REPO_DIR, SILENCE, SINE, URI_BASE, WHITE_NOISE, modui

sys.path.insert(0, str(REPO_DIR / "generator"))
import jpgen  # noqa: E402

PREFIX = "test_eigenghost"
SAMPLE_RATE = 48000.0
EXPECTED_TOOL = REPO_DIR / "build-tests" / "tests" / "host" / "eigenghost_expected"
EIGENGHOST_JSON = REPO_DIR / "plugins" / "eigenghost" / "eigenghost.json"
BANDS_HZ = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
# smoothers (20 ms) snap to the target once less than 1e-7 is left: about 0.34 s after a full
# jump; checks that expect bit-identical output wait longer than that
SETTLE_EXACT = 1.0
# measured 9.8 points with this method on 2026-10-16, 11.0 after the refinements (modulated
# lines, pre-delay); tools/jp.py perf measures the plugin alone: docs/plugins/eigenghost.md
CPU_CEILING_POINTS = 14.0
# the wet level of noise over 6 s scatters by about 0.1 dB between noise sequences
LEVEL_TOLERANCE_DB = 0.3
DECAYS = [0.5, 3.0, 10.0]
LEVEL_WINDOW = 6.0
PITCH_RISE_DB = 20.0
# measured 2026-10-16: residual against tone level -108.5 dB at depth 0, +4.4 dB at 20 cents
MODULATION_RISE_DB = 40.0
HIGH_BAND_COHERENCE = 0.5
ZIPPER_FACTOR = 1.1


def settle_for(decay):
    return max(2.0, 1.5 * decay)


def expected_levels():
    """{decay: wet level of noise relative to the input in dB} from the pedal on the Mac."""
    if not EXPECTED_TOOL.exists():
        subprocess.run(["cmake", "--build", str(REPO_DIR / "build-tests"), "--target", "eigenghost_expected"],
                       check=True, capture_output=True)
    levels = {}
    for decay in DECAYS:
        text = subprocess.run([str(EXPECTED_TOOL), str(SAMPLE_RATE), str(settle_for(decay)), str(LEVEL_WINDOW),
                               str(decay)], check=True, capture_output=True, text=True).stdout
        levels[decay] = next(iter(json.loads(text)["wetLevelDb"].values()))
    return levels


def eigenghost_controls():
    desc = jpgen.validate(jpgen.load(EIGENGHOST_JSON))
    return {port["symbol"]: jpgen.control_range(port) for port in desc["ports-control"]}, \
        {port["symbol"]: port["default"] for port in desc["ports-control"]}


def no_bad_samples(check, graph, what):
    check.near(f"{what}: non-finite samples", graph.out("meter", "nonFinite"), 0.0, 0.0)
    check.near(f"{what}: denormal samples", graph.out("meter", "denormals"), 0.0, 0.0)


def measure_both(graph, window):
    """One window on both meters, started together."""
    left = graph.start_measure("meter")
    right = graph.start_measure("meterR")
    graph.wait_result(left, window, "meter")
    graph.wait_result(right, window, "meterR")


def check_exact(graph, check, defaults):
    window = 1.0
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12, "on": 1})
    graph.set("meter", {"window": window})
    graph.set("meterR", {"window": window})
    graph.set("eigenghost", dict(defaults, wet=-60))
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window)
    check.near("wet -60: left diff peak dB (output = input)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    graph.set("eigenghost", defaults)
    graph.bypass("eigenghost", True)
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window)
    check.near("bypass on: left diff peak dB (output = input)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    graph.bypass("eigenghost", False)
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window)
    check.above("bypass off: left diff peak dB", graph.out("meter", "diffPeak"), -40.0)
    graph.set("eigenghost", {"width": 0})
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window, "meterR")
    check.near("width 0: right - left diff peak dB (right = left)", graph.out("meterR", "diffPeak"), FLOOR_DB, 0.0)
    window = 4.0
    graph.set("meterR", {"window": window})
    graph.set("eigenghost", {"width": 100, "dry": -60, "wet": 0})
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window, "meterR")
    coherences = [graph.out("meterR", f"band{k + 1}Coherence") for k in range(len(BANDS_HZ))]
    check.info("width 100 %, wet only: coherence right/left per band "
               + ", ".join(f"{hz} Hz {c:.3f}" for hz, c in zip(BANDS_HZ, coherences) if c is not None))
    check.below("width 100 %: coherence right/left at 8 kHz", coherences[-1], HIGH_BAND_COHERENCE)
    graph.set("eigenghost", defaults)


def check_levels(graph, check, defaults, expected):
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    graph.set("meter", {"window": LEVEL_WINDOW})
    for decay in DECAYS:
        graph.set("eigenghost", dict(defaults, dry=-60, wet=0, shimmer=0, decay=decay))
        graph.reader.poll(settle_for(decay))
        graph.measure(LEVEL_WINDOW)
        rms = graph.out("meter", "rms")
        reference = graph.out("meter", "refRms")
        level = None if rms is None or reference is None else rms - reference
        check.near(f"decay {decay} s: wet level of noise - input dB (Mac {expected[decay]:.3f})", level,
                   expected[decay], LEVEL_TOLERANCE_DB)
        no_bad_samples(check, graph, f"noise at decay {decay} s")
    graph.set("eigenghost", defaults)


def check_pitch(graph, check, defaults):
    window = 4.0
    fifth = 237.0 * 2.0 ** (7.0 / 12.0)
    graph.set("siggen", {"waveform": SINE, "frequency": 237, "level": -12})
    graph.set("meter", {"window": window, "band1Hz": fifth})
    levels = {}
    for shimmer in (0, 100):
        graph.set("eigenghost", dict(defaults, dry=-60, wet=0, decay=4, pitch1=7, pitch2=7, shimmer=shimmer))
        graph.reader.poll(3.0)
        graph.measure(window)
        levels[shimmer] = graph.out("meter", "band1Level")
    check.info(f"level at the fifth ({fifth:.1f} Hz): {levels[0]} dB without, {levels[100]} dB with pitch lines")
    rise = None if None in levels.values() else levels[100] - levels[0]
    check.above("pitch lines a fifth up: rise at the fifth dB", rise, PITCH_RISE_DB)
    graph.set("meter", {"band1Hz": BANDS_HZ[0]})
    graph.set("eigenghost", defaults)


def check_modulation(graph, check, defaults):
    """A held 1 kHz tone through the wet path without pitch shifting: at mod depth 0 the
    output is a pure tone, the modulated lines add content beside it (the meter's tone
    residual against its tone level)."""
    window = 4.0
    graph.set("siggen", {"waveform": SINE, "frequency": 1000, "level": -12})
    graph.set("meter", {"window": window, "toneHz": 1000})
    spread = {}
    for cents in (0, 20):
        graph.set("eigenghost", dict(defaults, dry=-60, wet=0, shimmer=0, decay=3, modDepth=cents))
        graph.reader.poll(6.0)
        graph.measure(window)
        level = graph.out("meter", "toneLevel")
        residual = graph.out("meter", "toneResidual")
        spread[cents] = None if level is None or residual is None else residual - level
    check.info(f"held 1 kHz tone, residual against tone level: {spread[0]} dB at depth 0, {spread[20]} dB at 20 cents")
    rise = None if None in spread.values() else spread[20] - spread[0]
    check.above("mod depth 20 cents: rise of the tone residual dB", rise, MODULATION_RISE_DB)
    graph.set("eigenghost", defaults)


def check_dry_wet_jumps(graph, check, defaults):
    window = 2.0
    graph.set("siggen", {"waveform": SINE, "frequency": 237, "level": -12})
    graph.set("meter", {"window": window, "toneHz": 237})
    dry_only = dict(defaults, shimmer=0, dry=0, wet=-60)
    wet_only = dict(defaults, shimmer=0, dry=-60, wet=6)
    steps = {}
    for name, values in (("dry", dry_only), ("wet", wet_only)):
        graph.set("eigenghost", values)
        graph.reader.poll(2.0)
        graph.measure(window)
        steps[name] = graph.out("meter", "maxStep")
    graph.set("eigenghost", dry_only)
    graph.reader.poll(1.0)
    measure_id = graph.start_measure()
    to_wet = True
    jumps = 0
    end = time.monotonic() + window + 5.0
    while graph.out("meter", "resultId") != measure_id and time.monotonic() < end:
        graph.set("eigenghost", {"dry": -60 if to_wet else 0, "wet": 6 if to_wet else -60})
        to_wet = not to_wet
        jumps += 1
        graph.reader.poll(0.05)
    graph.wait_result(measure_id, window)
    jumping = graph.out("meter", "maxStep")
    limit = 20.0 * math.log10(ZIPPER_FACTOR * (10.0 ** (steps["dry"] / 20.0) + 10.0 ** (steps["wet"] / 20.0)))
    check.info(f"largest sample step: dry only {steps['dry']:.3f} dB, wet only {steps['wet']:.3f} dB, jumping "
               f"({jumps} jumps sent during the window) {jumping} dB")
    check.below("dry/wet jumping every 50 ms: largest sample step dB", jumping, limit)
    no_bad_samples(check, graph, "dry/wet jumps")
    graph.set("eigenghost", defaults)


def check_changes_and_extremes(graph, check, ranges, defaults):
    window = 2.0
    graph.set("siggen", {"waveform": SINE, "frequency": 237, "level": -12})
    graph.set("meter", {"window": window})
    graph.set("eigenghost", dict(defaults, dry=-60, wet=0, shimmer=0))
    graph.reader.poll(1.0)
    for name, symbol, values in (("decay jumps", "decay", (0.5, 10)), ("size sweep", "size", (20, 6, 33, 12, 3, 40))):
        measure_id = graph.start_measure()
        index = 0
        end = time.monotonic() + window + 5.0
        while graph.out("meter", "resultId") != measure_id and time.monotonic() < end:
            graph.set("eigenghost", {symbol: values[index % len(values)]})
            index += 1
            graph.reader.poll(0.1)
        graph.wait_result(measure_id, window)
        no_bad_samples(check, graph, name)
        check.info(f"{name} every 100 ms: peak {graph.out('meter', 'peak')} dB, 8 kHz band "
                   f"{graph.out('meter', 'band8Level')} dB")
        graph.set("eigenghost", dict(defaults, dry=-60, wet=0, shimmer=0))
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    for side, pick in (("minimum", 0), ("maximum", 1)):
        graph.set("eigenghost", {symbol: limits[pick] for symbol, limits in ranges.items()})
        graph.reader.poll(2.0)
        graph.measure(window)
        no_bad_samples(check, graph, f"every control at its {side}")
        check.below(f"every control at its {side}: peak dB", graph.out("meter", "peak"), 20.0)
        check.info(f"every control at its {side}: peak {graph.out('meter', 'peak')} dB")
    graph.set("eigenghost", dict(defaults, decay=30, shimmer=100))
    graph.reader.poll(2.0)
    graph.set("siggen", {"waveform": SILENCE})
    graph.reader.poll(1.0)
    graph.measure(window)
    no_bad_samples(check, graph, "silence after noise at decay 30 s, pitch amount 100 %")
    graph.set("eigenghost", defaults)


def check_cpu(graph, check, baseline_window, defaults):
    """Mean CPU before and after eigenghost joins the graph (siggen and the meters run in
    both). Returns the time from which the graph only processes (no more plugins added)."""
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    before, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    modui.add_plugin("eigenghost", URI_BASE + "eigenghost", x=350, y=200)
    modui.connect("siggen/out", "eigenghost/in")
    modui.connect("eigenghost/outL", "meter/signal")
    modui.connect("eigenghost/outR", "meterR/signal")
    modui.connect("eigenghost/outL", "meterR/reference")
    graph.set("eigenghost", defaults)
    graph.reader.poll(1.0)
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    after, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    if not before or not after:
        check.record(False, "no stats messages for the CPU comparison")
        return start
    mean_before = sum(before) / len(before)
    mean_after = sum(after) / len(after)
    check.info(f"cpu without eigenghost {mean_before:.1f} %, with eigenghost (defaults) {mean_after:.1f} %")
    check.below("cpu added by eigenghost (percentage points)", mean_after - mean_before, CPU_CEILING_POINTS)
    return start


def check_addressing(check):
    modui.address("/graph/eigenghost/wet", "/hmi/knob1", "Wet", -60.0, 6.0, -6.0, 67)
    modui.address("/graph/eigenghost/decay", "/hmi/knob2", "Decay", 0.2, 30.0, 3.0, 101)
    modui.address("/graph/eigenghost/:bypass", "/hmi/footswitch1", "Eigenghost", 0.0, 1.0, 0.0, 33)
    fresh = modui.WebSocket()  # a new client gets the whole state, hw_map messages included
    try:
        maps = {modui.parse_hw_map(message) for message in fresh.drain(2.0)}
    finally:
        fresh.close()
    check.record(("eigenghost", "wet", "/hmi/knob1") in maps, "hw_map: knob 1 -> eigenghost wet")
    check.record(("eigenghost", "decay", "/hmi/knob2") in maps, "hw_map: knob 2 -> eigenghost decay")
    check.record(("eigenghost", ":bypass", "/hmi/footswitch1") in maps, "hw_map: footswitch B -> eigenghost bypass")


def run(restore):
    missing = devicetest.missing_plugins(["siggen", "meter", "eigenghost"])
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py test-device eigenghost deploys them)")
        return 1
    expected = expected_levels()
    ranges, defaults = eigenghost_controls()
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"{PREFIX}: pedalboard before the test: {board or '(none)'}")
        cursor = devicetest.journal_cursor()
        modui.reset()
        modui.add_plugin("siggen", URI_BASE + "siggen", x=50, y=200)
        modui.add_plugin("meter", URI_BASE + "meter", x=650, y=100)
        modui.add_plugin("meterR", URI_BASE + "meter", x=650, y=400)
        modui.connect("siggen/out", "meter/reference")
        graph = devicetest.Graph()
        start = time.monotonic()
        try:
            graph.reader.poll(1.0)
            print(f"{PREFIX}: cpu ...", flush=True)
            processing = check_cpu(graph, check, 5.0, defaults)
            steps = [
                ("exact outputs", lambda: check_exact(graph, check, defaults)),
                ("wet levels", lambda: check_levels(graph, check, defaults, expected)),
                ("pitch lines", lambda: check_pitch(graph, check, defaults)),
                ("modulation", lambda: check_modulation(graph, check, defaults)),
                ("dry/wet jumps", lambda: check_dry_wet_jumps(graph, check, defaults)),
                ("changes and extremes", lambda: check_changes_and_extremes(graph, check, ranges, defaults)),
                ("addressing", lambda: check_addressing(check)),
            ]
            for name, step in steps:
                print(f"{PREFIX}: {name} ...", flush=True)
                try:
                    step()
                except modui.DeviceError as error:
                    check.record(False, f"{name}: {error}")
            graph.reader.poll(1.0)
        finally:
            log = graph.save_log(f"{PREFIX}.log", start)
            graph.close()
        _, xruns = devicetest.stats_from_log(graph.reader.log)
        _, processing_xruns = devicetest.stats_from_log(graph.reader.log, since=processing)
        if xruns and processing_xruns:
            check.info(f"xruns over the whole run {xruns[0]} -> {xruns[-1]} (graph changes included)")
            check.record(processing_xruns[-1] == processing_xruns[0],
                         f"xruns while processing {processing_xruns[0]} -> {processing_xruns[-1]} (expected unchanged)")
        else:
            check.record(False, "no stats messages: xruns unknown")
        lines = devicetest.journal_lines(cursor)
        problems = devicetest.journal_problems(lines)
        check.record(not problems, f"jack2 journal: {len(problems)} problem lines among {len(lines)} during the run")
        for line in problems[:10]:
            check.info(f"journal: {line}")
    check.info(f"raw messages: {log}")
    check.info("previous pedalboard restored" if restore
               else "test board left loaded (knob 1 = wet, knob 2 = decay, footswitch B = bypass)")
    return check.report(PREFIX)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.restore)


if __name__ == "__main__":
    sys.exit(main())
