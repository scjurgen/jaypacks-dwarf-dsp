#!/usr/bin/env python3
"""Check on the Dwarf that control output values reach a script through mod-ui.

Needs the probe plugin installed (tools/jp.py deploy probe). Saves the loaded pedalboard,
empties the graph, adds one probe (audio unconnected), and as the only websocket client of
this script:
  1. records every message for a few seconds: message kinds, how often "seconds" arrives
     and how its value advances against the wall clock,
  2. sets echoIn to several values and measures the time until echoOut reports each one,
  3. sets a value with many digits to see the precision of the reported text,
then leaves the probe loaded (with --restore: reloads the previous pedalboard). Raw messages
go to build-dwarf/scratch/.

Usage:
  probe_readback.py [--seconds S] [--restore]
"""

import argparse
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modui  # noqa: E402

URI = "https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/probe"
INSTANCE = "probe"
SCRATCH = Path(__file__).resolve().parent.parent.parent / "build-dwarf" / "scratch"


def summary(values):
    if not values:
        return "none"
    return (f"n={len(values)} median={statistics.median(values) * 1000:.1f} ms "
            f"min={min(values) * 1000:.1f} ms max={max(values) * 1000:.1f} ms")


def run(seconds, restore):
    if not modui.plugin_installed(URI):
        print(f"probe_readback: {URI} is not installed (tools/jp.py deploy probe)")
        return 1
    report = []
    with modui.preserved_pedalboard(restore) as board:
        print(f"probe_readback: saved pedalboard {board or '(none)'}")
        modui.reset()
        modui.add_plugin(INSTANCE, URI)
        ws = modui.WebSocket()
        reader = modui.OutputReader(ws)
        try:
            reader.poll(seconds)
            kinds = {}
            for _, message in reader.log:
                kind = message.split(" ", 1)[0]
                kinds[kind] = kinds.get(kind, 0) + 1
            report.append(f"message kinds in {seconds} s: {dict(sorted(kinds.items()))}")
            ticks = [(arrival, modui.parse_output(message)) for arrival, message in reader.log]
            ticks = [(arrival, out[2]) for arrival, out in ticks if out and out[:2] == (INSTANCE, "seconds")]
            if len(ticks) >= 2:
                gaps = [b[0] - a[0] for a, b in zip(ticks, ticks[1:])]
                audio = ticks[-1][1] - ticks[0][1]
                wall = ticks[-1][0] - ticks[0][0]
                report.append(f"'seconds' updates: {len(ticks)}, interval {summary(gaps)}")
                report.append(f"'seconds' advanced {audio:.3f} s in {wall:.3f} s wall time")
            else:
                report.append(f"'seconds' updates: {len(ticks)} (no readback?)")
            sample = [message for _, message in reader.log if message.startswith("output_set")][:3]
            report.append(f"first output_set messages: {sample}")

            latencies = []
            for k in range(1, 11):
                value = round(k * 12.5 + 0.25, 4)
                start = time.monotonic()
                ws.param_set(INSTANCE, "echoIn", value)
                got = reader.wait_for(INSTANCE, "echoOut", lambda v, x=value: abs(v - x) < 1e-3, 3.0)
                if got is not None:
                    latencies.append(reader.values[(INSTANCE, "echoOut")][1] - start)
                else:
                    report.append(f"echoOut never showed {value}")
            report.append(f"param_set -> echoOut: {summary(latencies)}")

            ws.param_set(INSTANCE, "echoIn", 123.456789)
            reader.wait_for(INSTANCE, "echoOut", lambda v: abs(v - 123.456789) < 1e-2, 3.0)
            texts = [m for _, m in reader.log if m.startswith(f"output_set /graph/{INSTANCE} echoOut")]
            report.append(f"echoIn 123.456789 is reported as: {texts[-1] if texts else 'nothing'}")
        finally:
            SCRATCH.mkdir(parents=True, exist_ok=True)
            log = SCRATCH / "probe_readback.log"
            t0 = reader.log[0][0] if reader.log else 0.0
            log.write_text("".join(f"{arrival - t0:9.3f} {message}\n" for arrival, message in reader.log))
            ws.close()
            report.append(f"raw messages: {log}")
    print("\n".join("probe_readback: " + line for line in report))
    print("probe_readback: " + ("previous pedalboard restored" if restore else "probe left loaded"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--seconds", type=float, default=5.0, help="recording time for step 1")
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.seconds, args.restore)


if __name__ == "__main__":
    sys.exit(main())
