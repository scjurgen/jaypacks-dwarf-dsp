#!/usr/bin/env python3
"""Device integration test of jaystwin on the Dwarf (Jays Twin step 3).

Needs siggen, meter and jaystwin installed (tools/jp.py test-device jaystwin deploys them) and
the Mac test build for the expected values (build-tests, target jaystwin_expected). Empties
the graph and builds
  siggen/out -> jaystwin/in, jaystwin/out -> meter/signal, siggen/out -> meter/reference
(nothing goes to the audio outputs), then checks with the meter's readings:
  - CPU: mean mod-ui CPU with jaystwin at its defaults against the same graph before it was
    added,
  - bypass on gives exactly the input, off does not,
  - the transfer function: band gains of white noise at -80 dBFS (where the drive is linear)
    for five settings against the pedal on the Mac with the meter's analysis
    (jaystwin_expected transfer): within 0.1 dB from 250 Hz up, within 0.5 dB at 125 Hz, the
    63 Hz band reported only (the cabinet's steep low cut, see LOW_BAND_HZ),
  - the drive: level of a 237 Hz sine at -12 dBFS relative to the input at gain 0, 5, 10 and
    11 against the Mac (jaystwin_expected level),
  - treble jumping between 0 and 10 and gain between 0 and 11 every 50 ms: largest sample step at most 1.3
    times (+2.3 dB) the largest steady step at 0, 5 and 10 (the zipper metric; the Mac host
    test measures up to 1.17 times),
  - every control at its minimum and at its maximum, and silence after noise at gain 11:
    no non-finite and no denormal samples,
  - knob 1 -> gain, knob 2 -> treble, knob 3 -> level and footswitch B -> bypass addressed,
    confirmed by mod-ui's hw_map messages,
  - xruns unchanged while the graph processes, no problem lines in the jack2 journal,
then leaves the board loaded with the addressing (with --restore: reloads the previous
pedalboard). Raw messages go to build-dwarf/scratch/test_jaystwin.log.

Usage:
  test_jaystwin.py [--restore]
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

PREFIX = "test_jaystwin"
SAMPLE_RATE = 48000.0
EXPECTED_TOOL = REPO_DIR / "build-tests" / "tests" / "host" / "jaystwin_expected"
JAYSTWIN_JSON = REPO_DIR / "plugins" / "jaystwin" / "jaystwin.json"
BANDS_HZ = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
# smoothers (20 ms) snap to the target once less than 1e-7 is left: about 0.34 s after a full
# jump; the filters' tails need a little longer
SETTLE = 1.0
CPU_CEILING_POINTS = 5.0
NOISE_DB = -80.0
TRANSFER_WINDOW = 4.0
TRANSFER_TOLERANCE_DB = 0.1
TRANSFER_COHERENCE = 0.999
# 63 and 125 Hz lie on the cabinet's steep low cut: across the meter's Hann bin (+-3 Hz) the
# response changes by about 1 dB, so the band coherence drops and the H1 estimate scatters
# between noise windows (measured 2026-10-16: at 63 Hz coherence 0.88 to 0.99, -16.31 to
# -16.76 dB on the device and -16.29 to -16.66 dB on the Mac for the defaults, up to 0.78 dB
# apart with treble 10 and bright; at 125 Hz coherence 0.995 and up, at most 0.34 dB apart).
# 63 Hz is reported only; 125 Hz is checked with a wider tolerance.
LOW_BAND_HZ = 125
LOW_BAND_TOLERANCE_DB = 0.5
LOW_BAND_COHERENCE = 0.99
LEVEL_WINDOW = 2.0
LEVEL_TOLERANCE_DB = 0.05
ZIPPER_FACTOR = 1.3
TRANSFER_SETTINGS = [
    {},
    {"bass": 10, "mid": 0, "treble": 0},
    {"bass": 0, "mid": 10, "treble": 0, "level": -6},
    {"treble": 10, "bright": 1, "gain": 2},
    {"gain": 11, "bass": 7, "mid": 2, "treble": 7},
]
LEVEL_GAINS = [0, 5, 10, 11]


def describe(settings):
    return ", ".join(f"{k} {v}" for k, v in settings.items()) or "defaults"


def expected(mode, *args, settings=None):
    if not EXPECTED_TOOL.exists():
        subprocess.run(["cmake", "--build", str(REPO_DIR / "build-tests"), "--target", "jaystwin_expected"],
                       check=True, capture_output=True)
    command = [str(EXPECTED_TOOL), mode, str(SAMPLE_RATE), str(SETTLE), *[str(a) for a in args]]
    command += [f"{k}={v}" for k, v in (settings or {}).items()]
    return json.loads(subprocess.run(command, check=True, capture_output=True, text=True).stdout)


def jaystwin_controls():
    desc = jpgen.validate(jpgen.load(JAYSTWIN_JSON))
    return {port["symbol"]: jpgen.control_range(port) for port in desc["ports-control"]}, \
        {port["symbol"]: port["default"] for port in desc["ports-control"]}


def no_bad_samples(check, graph, what):
    check.near(f"{what}: non-finite samples", graph.out("meter", "nonFinite"), 0.0, 0.0)
    check.near(f"{what}: denormal samples", graph.out("meter", "denormals"), 0.0, 0.0)


def check_bypass(graph, check, defaults):
    window = 1.0
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12, "on": 1})
    graph.set("meter", {"window": window})
    graph.set("jaystwin", defaults)
    graph.bypass("jaystwin", True)
    graph.reader.poll(SETTLE)
    graph.measure(window)
    check.near("bypass on: diff peak dB (output = input)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    graph.bypass("jaystwin", False)
    graph.reader.poll(SETTLE)
    graph.measure(window)
    check.above("bypass off: diff peak dB", graph.out("meter", "diffPeak"), -40.0)


def check_transfer(graph, check, defaults):
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": NOISE_DB})
    graph.set("meter", {"window": TRANSFER_WINDOW})
    for settings in TRANSFER_SETTINGS:
        mac = expected("transfer", TRANSFER_WINDOW, NOISE_DB, settings=settings)
        graph.set("jaystwin", dict(defaults, **settings))
        graph.reader.poll(SETTLE)
        graph.measure(TRANSFER_WINDOW)
        gains = [graph.out("meter", f"band{k + 1}Gain") for k in range(len(BANDS_HZ))]
        coherences = [graph.out("meter", f"band{k + 1}Coherence") for k in range(len(BANDS_HZ))]
        check.info(f"{describe(settings)}: band gains dB (coherence) "
                   + ", ".join(f"{hz} Hz {g:.2f} ({c:.4f}, Mac {r:.2f})"
                               for hz, g, c, r in zip(BANDS_HZ, gains, coherences, mac["bandGainDb"])
                               if g is not None and c is not None))
        low = BANDS_HZ.index(LOW_BAND_HZ)
        for name, indices, tolerance, floor in (
                (f"{LOW_BAND_HZ} Hz", range(low, low + 1), LOW_BAND_TOLERANCE_DB, LOW_BAND_COHERENCE),
                ("250 Hz to 8 kHz", range(low + 1, len(BANDS_HZ)), TRANSFER_TOLERANCE_DB, TRANSFER_COHERENCE)):
            values = [(gains[k], coherences[k], mac["bandGainDb"][k]) for k in indices]
            complete = all(g is not None and c is not None for g, c, _ in values)
            worst = max(abs(g - r) for g, _, r in values) if complete else None
            lowest = min(c for _, c, _ in values) if complete else None
            check.below(f"{describe(settings)}, {name}: worst band gain - Mac dB", worst, tolerance)
            check.above(f"{describe(settings)}, {name}: lowest band coherence", lowest, floor)
    graph.set("jaystwin", defaults)


def check_levels(graph, check, defaults):
    graph.set("siggen", {"waveform": SINE, "frequency": 237, "level": -12})
    graph.set("meter", {"window": LEVEL_WINDOW})
    for gain in LEVEL_GAINS:
        mac = expected("level", LEVEL_WINDOW, 237, -12, settings={"gain": gain})["levelDb"]
        graph.set("jaystwin", dict(defaults, gain=gain))
        graph.reader.poll(SETTLE)
        graph.measure(LEVEL_WINDOW)
        rms = graph.out("meter", "rms")
        reference = graph.out("meter", "refRms")
        level = None if rms is None or reference is None else rms - reference
        check.near(f"gain {gain}: level of a 237 Hz sine - input dB (Mac {mac:.3f})", level, mac, LEVEL_TOLERANCE_DB)
    graph.set("jaystwin", defaults)


def check_jumps(graph, check, defaults):
    window = 2.0
    graph.set("siggen", {"waveform": SINE, "frequency": 237, "level": -12})
    graph.set("meter", {"window": window})
    for symbol, top in (("treble", 10), ("gain", 11)):
        steady = []
        for value in (0, top // 2, top):
            graph.set("jaystwin", dict(defaults, **{symbol: value}))
            graph.reader.poll(SETTLE)
            graph.measure(window)
            steady.append(graph.out("meter", "maxStep"))
        if None in steady:
            check.record(False, f"{symbol}: no steady step readings")
            continue
        graph.set("jaystwin", dict(defaults, **{symbol: 0}))
        graph.reader.poll(SETTLE)
        measure_id = graph.start_measure()
        high = True
        jumps = 0
        end = time.monotonic() + window + 5.0
        while graph.out("meter", "resultId") != measure_id and time.monotonic() < end:
            graph.set("jaystwin", {symbol: top if high else 0})
            high = not high
            jumps += 1
            graph.reader.poll(0.05)
        graph.wait_result(measure_id, window)
        jumping = graph.out("meter", "maxStep")
        limit = max(steady) + 20.0 * math.log10(ZIPPER_FACTOR)
        check.info(f"{symbol}: largest sample step steady at 0, {top // 2}, {top}: "
                   + ", ".join(f"{s:.3f}" for s in steady) + f" dB; jumping ({jumps} jumps) {jumping} dB")
        check.below(f"{symbol} jumping 0 <-> {top} every 50 ms: largest sample step dB", jumping, limit)
        no_bad_samples(check, graph, f"{symbol} jumps")
    graph.set("jaystwin", defaults)


def check_extremes(graph, check, ranges, defaults):
    window = 2.0
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    graph.set("meter", {"window": window})
    for side, pick in (("minimum", 0), ("maximum", 1)):
        graph.set("jaystwin", {symbol: limits[pick] for symbol, limits in ranges.items()})
        graph.reader.poll(SETTLE)
        graph.measure(window)
        no_bad_samples(check, graph, f"every control at its {side}")
        check.below(f"every control at its {side}: peak dB", graph.out("meter", "peak"), 20.0)
        check.info(f"every control at its {side}: peak {graph.out('meter', 'peak')} dB")
    graph.set("jaystwin", dict(defaults, gain=11, bass=10))
    graph.reader.poll(SETTLE)
    graph.set("siggen", {"waveform": SILENCE})
    graph.reader.poll(1.0)
    graph.measure(window)
    no_bad_samples(check, graph, "silence after noise at gain 11, bass 10")
    graph.set("jaystwin", defaults)


def check_cpu(graph, check, baseline_window, defaults):
    """Mean CPU before and after jaystwin joins the graph (siggen and the meter run in both).
    Returns the time from which the graph only processes (no more plugins added)."""
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    before, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    modui.add_plugin("jaystwin", URI_BASE + "jaystwin", x=350, y=200)
    modui.connect("siggen/out", "jaystwin/in")
    modui.connect("jaystwin/out", "meter/signal")
    graph.set("jaystwin", defaults)
    graph.reader.poll(1.0)
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    after, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    if not before or not after:
        check.record(False, "no stats messages for the CPU comparison")
        return start
    mean_before = sum(before) / len(before)
    mean_after = sum(after) / len(after)
    check.info(f"cpu without jaystwin {mean_before:.1f} %, with jaystwin (defaults) {mean_after:.1f} %")
    check.below("cpu added by jaystwin (percentage points)", mean_after - mean_before, CPU_CEILING_POINTS)
    return start


def check_addressing(check):
    modui.address("/graph/jaystwin/gain", "/hmi/knob1", "Gain", 0.0, 11.0, 3.0, 111)
    modui.address("/graph/jaystwin/treble", "/hmi/knob2", "Treble", 0.0, 10.0, 5.0, 101)
    modui.address("/graph/jaystwin/level", "/hmi/knob3", "Level", -30.0, 12.0, 0.0, 85)
    modui.address("/graph/jaystwin/:bypass", "/hmi/footswitch1", "Jays Twin", 0.0, 1.0, 0.0, 33)
    fresh = modui.WebSocket()  # a new client gets the whole state, hw_map messages included
    try:
        maps = {modui.parse_hw_map(message) for message in fresh.drain(2.0)}
    finally:
        fresh.close()
    check.record(("jaystwin", "gain", "/hmi/knob1") in maps, "hw_map: knob 1 -> jaystwin gain")
    check.record(("jaystwin", "treble", "/hmi/knob2") in maps, "hw_map: knob 2 -> jaystwin treble")
    check.record(("jaystwin", "level", "/hmi/knob3") in maps, "hw_map: knob 3 -> jaystwin level")
    check.record(("jaystwin", ":bypass", "/hmi/footswitch1") in maps, "hw_map: footswitch B -> jaystwin bypass")


def run(restore):
    missing = devicetest.missing_plugins(["siggen", "meter", "jaystwin"])
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py test-device jaystwin deploys them)")
        return 1
    ranges, defaults = jaystwin_controls()
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"{PREFIX}: pedalboard before the test: {board or '(none)'}")
        cursor = devicetest.journal_cursor()
        modui.reset()
        modui.add_plugin("siggen", URI_BASE + "siggen", x=50, y=200)
        modui.add_plugin("meter", URI_BASE + "meter", x=650, y=200)
        modui.connect("siggen/out", "meter/reference")
        graph = devicetest.Graph()
        start = time.monotonic()
        try:
            graph.reader.poll(1.0)
            print(f"{PREFIX}: cpu ...", flush=True)
            processing = check_cpu(graph, check, 5.0, defaults)
            steps = [
                ("bypass", lambda: check_bypass(graph, check, defaults)),
                ("transfer function", lambda: check_transfer(graph, check, defaults)),
                ("drive levels", lambda: check_levels(graph, check, defaults)),
                ("jumps", lambda: check_jumps(graph, check, defaults)),
                ("extremes", lambda: check_extremes(graph, check, ranges, defaults)),
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
               else "test board left loaded (knob 1 = gain, knob 2 = treble, knob 3 = level, footswitch B = bypass)")
    return check.report(PREFIX)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.restore)


if __name__ == "__main__":
    sys.exit(main())
