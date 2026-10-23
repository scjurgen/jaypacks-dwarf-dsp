"""Shared parts of the device tests in this directory (docs/test-plugins.md): pass/fail
bookkeeping, a graph steered and read over one mod-ui websocket with the meter's measure
protocol, CPU and xruns from mod-ui's stats messages, and the jack2 journal.

Standard library only.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modui  # noqa: E402

URI_BASE = "https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/"
REPO_DIR = Path(__file__).resolve().parent.parent.parent
SCRATCH = REPO_DIR / "build-dwarf" / "scratch"
FLOOR_DB = -240.0
# siggen menu (siggen.json)
SILENCE, SINE, WHITE_NOISE = 0.0, 1.0, 4.0
# the meter's results of one window arrive within a few mod-ui updates (about 12.5 ms each)
SETTLE = 0.3
# journal lines of the jack2 unit (jackd and mod-host) that count as problems
JOURNAL_PROBLEMS = ("xrun", "error", "segfault", "assert", "crash", "killed")


class Check:
    """Collects pass/fail lines and informational lines."""

    def __init__(self):
        self.lines = []
        self.failed = 0

    def near(self, name, value, expected, tolerance):
        ok = value is not None and abs(value - expected) <= tolerance
        self.record(ok, f"{name} = {value} (expected {expected} +- {tolerance})")

    def below(self, name, value, limit):
        self.record(value is not None and value < limit, f"{name} = {value} (expected < {limit})")

    def above(self, name, value, limit):
        self.record(value is not None and value > limit, f"{name} = {value} (expected > {limit})")

    def record(self, ok, text):
        self.failed += 0 if ok else 1
        self.lines.append(("ok   " if ok else "FAIL ") + text)

    def info(self, text):
        self.lines.append("info " + text)

    def report(self, prefix):
        print("\n".join(f"{prefix}: {line}" for line in self.lines))
        print(f"{prefix}: {'FAILED, ' + str(self.failed) + ' checks' if self.failed else 'all checks passed'}")
        return 1 if self.failed else 0


class Graph:
    """The current mod-ui graph, steered and read over one websocket."""

    def __init__(self):
        self.ws = modui.WebSocket()
        self.reader = modui.OutputReader(self.ws)
        self.measure_id = 0

    def set(self, instance, values):
        for symbol, value in values.items():
            self.ws.param_set(instance, symbol, value)

    def bypass(self, instance, on):
        self.ws.param_set(instance, ":bypass", 1 if on else 0)

    def start_measure(self, meter="meter"):
        """Send a new measure value; returns it."""
        self.measure_id += 1
        self.ws.param_set(meter, "measure", self.measure_id)
        return self.measure_id

    def wait_result(self, measure_id, window, meter="meter"):
        got = self.reader.wait_for(meter, "resultId", lambda v: v == measure_id, window + 5.0)
        if got is None:
            raise modui.DeviceError(f"no meter result for measure {measure_id}")
        self.reader.poll(SETTLE)

    def measure(self, window, meter="meter"):
        """A new measure value, then wait for its result."""
        self.wait_result(self.start_measure(meter), window, meter)

    def out(self, instance, symbol):
        return self.reader.value(instance, symbol)

    def save_log(self, name, start):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        log = SCRATCH / name
        log.write_text("".join(f"{arrival - start:9.3f} {message}\n" for arrival, message in self.reader.log))
        return log

    def close(self):
        self.ws.close()


def stats_from_log(log, since=None, until=None):
    """CPU percentages and xrun counts of the "stats <cpu> <xruns>" messages, optionally only
    those that arrived between two time.monotonic() values."""
    cpu, xruns = [], []
    for arrival, message in log:
        if (since is not None and arrival < since) or (until is not None and arrival > until):
            continue
        parts = message.split()
        if parts and parts[0] == "stats" and len(parts) >= 3:
            cpu.append(float(parts[1]))
            xruns.append(int(float(parts[2])))
    return cpu, xruns


def journal_cursor():
    """Cursor of the newest journal entry: the device clock is not reliable, so later entries
    are read after the cursor instead of after a time."""
    for line in modui.ssh("journalctl -u jack2 -n 1 --show-cursor --no-pager").splitlines():
        if line.startswith("-- cursor: "):
            return line[len("-- cursor: "):].strip()
    return ""


def journal_lines(cursor):
    """jack2 journal lines (jackd and mod-host) after the cursor."""
    after = f"--after-cursor='{cursor}'" if cursor else "-n 200"
    return modui.ssh(f"journalctl -u jack2 {after} --no-pager -o cat").splitlines()


def journal_problems(lines):
    """The lines that mention a problem word."""
    return [line for line in lines if any(word in line.lower() for word in JOURNAL_PROBLEMS)]


def missing_plugins(names):
    return [name for name in names if not modui.plugin_installed(URI_BASE + name)]
