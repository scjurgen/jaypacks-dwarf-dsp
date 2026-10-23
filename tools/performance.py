#!/usr/bin/env python3
"""Performance data of the plugins on the MOD Dwarf and the tables made from it.

Every measured plugin has plugins/<name>/performance.json, written by
tools/device_tests/perf.py (tools/jp.py perf). The tables in README.md and in
docs/plugins/<name>.md are generated from these files and stand between the lines
<!-- perf:begin --> and <!-- perf:end -->; tools/test_performance.py fails when a table
differs from what the JSON files give, so they cannot go stale.

The memory figure is what the plugin allocates, counted on the Mac by its memory probe
(build-tests/memprobe/jaypacks-<name>-memprobe, tests/host/MemoryProbe.cpp, docs/testing.md):
the device cannot show it, because jackd's allocator serves a new plugin from memory freed
earlier in the process (docs/device.md).

As a script:
  performance.py update     rewrite the tables from the JSON files
  performance.py check      exit 1 if a table is out of date
  performance.py memory     run the memory probes, write the memory into the JSON files, update

Standard library only.
"""

import datetime
import json
import subprocess
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
BUILD_TESTS = REPO_DIR / "build-tests"
BEGIN = "<!-- perf:begin -->"
END = "<!-- perf:end -->"


def load_all(repo=REPO_DIR):
    """{plugin name: performance data} for every plugins/<name>/performance.json."""
    return {path.parent.name: json.loads(path.read_text()) for path in sorted(repo.glob("plugins/*/performance.json"))}


def points(value):
    return f"{value:.1f}"


def kb(value):
    """A size in kB (1024 bytes, as the binary size): one decimal below 10 kB."""
    return f"{value:.1f}" if value < 10 else f"{value:.0f}"


def probe_memory(name, build_dir=BUILD_TESTS):
    """{"allocated": kB, "peak": kB} of a plugin from its memory probe (built first). Raises
    when run() allocates or cleanup() leaks: the probe's --check."""
    target = f"jaypacks-{name}-memprobe"
    subprocess.run(["cmake", "--build", str(build_dir), "--target", target], check=True, capture_output=True)
    result = subprocess.run([str(build_dir / "memprobe" / target), "--check"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{target}: {result.stderr.strip()}")
    data = json.loads(result.stdout)
    return {"allocated": round(data["allocatedBytes"] / 1024, 1), "peak": round(data["peakBytes"] / 1024, 1),
            "counted": datetime.date.today().isoformat()}


def plugin_table(data):
    """Markdown of one plugin's measurement: CPU per setting, memory, conditions."""
    lines = [
        "| Settings | CPU (points of mod-ui's DSP load) | Range over the cycles |",
        "|---|---|---|",
    ]
    for row in data["cpu_points"]:
        lines.append(f"| {row['settings']} | {points(row['median'])} | {points(row['min'])} to {points(row['max'])} |")
    memory = data["memory_kb"]
    device = data["device"]
    lines += [
        "",
        f"Memory: the plugin allocates {kb(memory['allocated'])} kB when it is created (at 48 kHz, peak "
        f"{kb(memory['peak'])} kB, nothing while it runs; counted by its memory probe on {memory['counted']}); "
        f"the binary is {memory['binary']} kB. Xruns while processing: {data['xruns']}.",
        "",
        f"Measured {data['date']} with tools/jp.py perf: plugin {data['version']}, commit "
        f"{data['commit']}, {device['firmware']}, jackd {device['jack']}; input {data['source']}; "
        f"median of {data['cycles']} cycles of {data['window_s']:g} s with and without the plugin.",
    ]
    return "\n".join(lines)


def summary_table(all_data):
    """Markdown table with one row per measured plugin, for README.md."""
    lines = [
        "| Plugin | Version | CPU, defaults | CPU, highest setting | Memory | Binary | Measured |",
        "|---|---|---|---|---|---|---|",
    ]
    for name, data in all_data.items():
        rows = data["cpu_points"]
        default = rows[0]
        highest = max(rows, key=lambda row: row["median"])
        memory = data["memory_kb"]
        lines.append(
            f"| `{name}` | {data['version']} | {points(default['median'])} | "
            f"{points(highest['median'])} ({highest['settings']}) | {kb(memory['allocated'])} kB | {memory['binary']} kB | "
            f"{data['date']} |"
        )
    return "\n".join(lines)


def replace_marked(text, block):
    """text with the part between BEGIN and END replaced by block."""
    start = text.find(BEGIN)
    end = text.find(END)
    if start < 0 or end < start:
        raise ValueError("perf markers missing")
    return text[: start + len(BEGIN)] + "\n" + block + "\n" + text[end:]


def expected_docs(repo=REPO_DIR):
    """{path: content as it should be} for README.md and each measured plugin's page."""
    all_data = load_all(repo)
    result = {}
    readme = repo / "README.md"
    if all_data:
        result[readme] = replace_marked(readme.read_text(), summary_table(all_data))
    for name, data in all_data.items():
        page = repo / "docs" / "plugins" / f"{name}.md"
        if page.exists():
            result[page] = replace_marked(page.read_text(), plugin_table(data))
    return result


def stale_docs(repo=REPO_DIR):
    return [path for path, text in expected_docs(repo).items() if path.read_text() != text]


def update_docs(repo=REPO_DIR):
    """Rewrite the stale tables; returns the paths written."""
    changed = []
    for path, text in expected_docs(repo).items():
        if path.read_text() != text:
            path.write_text(text)
            changed.append(path)
    return changed


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if args == ["update"]:
        for path in update_docs():
            print(f"updated {path.relative_to(REPO_DIR)}")
        return 0
    if args == ["memory"]:
        for path in sorted(REPO_DIR.glob("plugins/*/performance.json")):
            data = json.loads(path.read_text())
            data["memory_kb"] = dict(probe_memory(path.parent.name), binary=data["memory_kb"]["binary"])
            path.write_text(json.dumps(data, indent=2) + "\n")
            print(f"wrote {path.relative_to(REPO_DIR)}: {kb(data['memory_kb']['allocated'])} kB")
        for path in update_docs():
            print(f"updated {path.relative_to(REPO_DIR)}")
        return 0
    if args == ["check"]:
        stale = stale_docs()
        for path in stale:
            print(f"out of date: {path.relative_to(REPO_DIR)} (run tools/performance.py update)")
        return 1 if stale else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
