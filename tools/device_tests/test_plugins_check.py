#!/usr/bin/env python3
"""Check siggen and meter on the Dwarf: known signals give the expected meter readings.

Needs siggen and meter installed (tools/jp.py deploy siggen meter). Empties the graph and
builds siggen -> meter (signal and reference input; nothing goes to the audio outputs), then
measures with the measure protocol (set inputs, a new `measure` value, wait for `resultId`):
  1. sine 1 kHz at -20 dBFS: peak, RMS, frequency, tone level and residual, no difference
     between signal and reference, no non-finite or denormal samples, siggen's own RMS,
  2. white noise at -6 dBFS: RMS of uniform noise, every band flat (0 dB) with coherence 1,
  3. xruns from mod-ui's stats messages during the run (CPU as information),
then leaves siggen -> meter loaded (with --restore: reloads the previous pedalboard). Raw
messages go to build-dwarf/scratch/.

Usage:
  test_plugins_check.py [--restore]
"""

import argparse
import sys
import time

import devicetest
from devicetest import FLOOR_DB, SINE, URI_BASE, WHITE_NOISE, modui

BANDS = 8


def run_checks(graph, check):
    # 1. sine
    graph.set("siggen", {"waveform": SINE, "frequency": 1000, "level": -20, "on": 1})
    graph.set("meter", {"window": 1.0, "toneHz": 1000})
    graph.reader.poll(0.2)
    graph.measure(1.0)
    check.near("sine peak dB", graph.out("meter", "peak"), -20.0, 0.01)
    check.near("sine rms dB", graph.out("meter", "rms"), -23.0103, 0.01)
    check.near("sine frequency Hz", graph.out("meter", "frequency"), 1000.0, 0.1)
    check.near("sine tone level dB", graph.out("meter", "toneLevel"), -20.0, 0.01)
    check.below("sine tone residual dB", graph.out("meter", "toneResidual"), -100.0)
    check.near("sine diff peak dB (signal = reference)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    check.near("sine non-finite samples", graph.out("meter", "nonFinite"), 0.0, 0.0)
    check.near("sine denormal samples", graph.out("meter", "denormals"), 0.0, 0.0)
    check.near("siggen rms dB", graph.out("siggen", "rms"), -23.0103, 0.01)

    # 2. white noise
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -6})
    graph.set("meter", {"window": 4.0})
    graph.reader.poll(0.2)
    graph.measure(4.0)
    check.near("noise rms dB", graph.out("meter", "rms"), -6.0 - 4.7712, 0.05)
    for k in range(1, BANDS + 1):
        check.near(f"noise band {k} gain dB", graph.out("meter", f"band{k}Gain"), 0.0, 0.01)
        check.above(f"noise band {k} coherence", graph.out("meter", f"band{k}Coherence"), 0.999)


def run(restore):
    missing = devicetest.missing_plugins(["siggen", "meter"])
    if missing:
        print(f"test_plugins_check: not installed: {missing} (tools/jp.py deploy siggen meter)")
        return 1
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"test_plugins_check: pedalboard before the test: {board or '(none)'}")
        modui.reset()
        modui.add_plugin("siggen", URI_BASE + "siggen", x=100, y=200)
        modui.add_plugin("meter", URI_BASE + "meter", x=500, y=200)
        modui.connect("siggen/out", "meter/signal")
        modui.connect("siggen/out", "meter/reference")
        graph = devicetest.Graph()
        start = time.monotonic()
        try:
            graph.reader.poll(1.0)
            try:
                run_checks(graph, check)
            except modui.DeviceError as error:
                check.record(False, str(error))
            graph.reader.poll(1.0)
        finally:
            log = graph.save_log("test_plugins_check.log", start)
            graph.close()
    cpu, xruns = devicetest.stats_from_log(graph.reader.log)
    if cpu:
        check.info(f"cpu during the run: mean {sum(cpu) / len(cpu):.1f} %, max {max(cpu):.1f} %")
    if xruns:
        check.record(xruns[-1] == xruns[0], f"xruns {xruns[0]} -> {xruns[-1]} (expected unchanged)")
    check.info(f"raw messages: {log}")
    check.info("previous pedalboard restored" if restore else "siggen -> meter left loaded")
    return check.report("test_plugins_check")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    return run(parser.parse_args(argv).restore)


if __name__ == "__main__":
    sys.exit(main())
