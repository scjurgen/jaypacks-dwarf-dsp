#!/usr/bin/env python3
"""Generate, build and deploy the jaypacks LV2 plugins.

Commands:
  generate [plugin...]             run the generator (validates the JSON, writes ports.h,
                                   the TTL files and the modgui into build-mac-release/)
  build-mac [plugin...] [--install]
                                   native release build on the Mac into
                                   build-mac-release/lv2/jaypacks-<name>.lv2; --install
                                   copies the bundles to ~/Library/Audio/Plug-Ins/LV2
                                   (e.g. for Reaper), replacing only jaypacks bundles; warns
                                   when a committed snapshot differs from the new screenshot
  snapshot [plugin...]             build on the Mac and copy the modgui screenshot to
                                   docs/plugins/images/<name>.png (committed); without names
                                   the existing snapshots are refreshed
  build-dwarf [plugin...]          cross-build in docker (moddwarf-new, GCC 9.4) into
                                   build-dwarf/, check the binaries (exports, GLIBC) and
                                   assemble the device bundles in build-dwarf/bundles/:
                                   the generated files of the Mac build plus the Dwarf .so
  deploy [plugin...] [--unload]    install the bundles of build-dwarf/bundles/ on the device
                                   through mod-ui and check them with lv2info; mod-ui refuses
                                   to replace a plugin used by the loaded pedalboard, --unload
                                   unloads that board and reloads it afterwards (a saved
                                   pedalboard only: an unsaved graph stays, test-device
                                   empties it)
  test-device <plugin> [--build] [--no-deploy] [--restore]
                                   device integration test: empties the graph, deploys siggen,
                                   meter and the plugin (--build cross-builds them first) and
                                   runs tools/device_tests/test_<plugin>.py, which leaves its
                                   board loaded (--restore reloads the previous one)
  push-script <plugin> <name> <file>
                                   copy a Lua script to the device, into the scripts folder of the
                                   plugin (/data/jaypacks/scripts/<plugin>/<name>.lua); a plugin
                                   with scripts reloads it within a second; name: letters, digits,
                                   - and _ (for morphexsynth and midiscript user-1 to user-8)
  script-log <plugin> <name>       print <name>.log that the plugin wrote next to the script
                                   (state, error with line, the script's own Log lines)
  perf [plugin...] [--build] [--no-deploy] [--cycles N] [--restore]
                                   measure CPU and memory on the device (deploys siggen and
                                   the plugins first) and write plugins/<name>/performance.json
                                   and the tables in README.md and docs/plugins/<name>.md

Without plugin names every plugin in plugins/ is used. The Mac build creates or updates the
venv .venv first (dev-scripts/dev-venv.sh), which the modgui images need. Logs go to
build-mac-release/scratch/ and build-dwarf/scratch/.

Environment: JP_DWARF_IMAGE (docker image, default dwarf-new), DWARF_HOST (device, default
192.168.51.1).
"""

import argparse
import filecmp
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_DIR / "tools"))
sys.path.insert(0, str(REPO_DIR / "tests" / "host"))
import modgui_check  # noqa: E402
import modui  # noqa: E402

BUILD_MAC = REPO_DIR / "build-mac-release"
BUILD_DWARF = REPO_DIR / "build-dwarf"
DWARF_BUNDLES = BUILD_DWARF / "bundles"
SNAPSHOT_DIR = REPO_DIR / "docs" / "plugins" / "images"
MAC_LV2_DIR = Path.home() / "Library" / "Audio" / "Plug-Ins" / "LV2"
URI_BASE = "https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/"
IMAGE = os.environ.get("JP_DWARF_IMAGE", "dwarf-new")
CONTAINER_REPO = "/home/modbuilder/jaypacks"
TOOLCHAIN_DIR = "/home/modbuilder/mod-plugin-builder"
READELF = "aarch64-modaudio-linux-gnu-readelf"
# newest symbol versions the device provides: glibc 2.27, libstdc++ of GCC 10 (6.0.28)
VERSION_LIMITS = {"GLIBC": (2, 27), "GLIBCXX": (3, 4, 28), "CXXABI": (1, 3, 12)}
EM_AARCH64 = 183


def available_plugins():
    return sorted(p.parent.name for p in REPO_DIR.glob("plugins/*/*.json") if p.stem == p.parent.name)


def select_plugins(requested):
    known = available_plugins()
    unknown = [name for name in requested if name not in known]
    if unknown:
        raise SystemExit(f"unknown plugins {unknown}; available: {known}")
    return requested or known


def run_logged(command, log):
    """Run a command with its output in the file `log`; return True on success."""
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "w") as out:
        result = subprocess.run(command, cwd=REPO_DIR, stdout=out, stderr=subprocess.STDOUT)
    return result.returncode == 0


def own_warnings(log, source_dir=REPO_DIR):
    """Compiler warnings in our own files (not third_party/), with paths under source_dir."""
    pattern = re.compile(r"^" + re.escape(str(source_dir)) + r"/(?!third_party/)[^:]+:\d+:\d+: warning: .*$", re.M)
    return sorted(set(pattern.findall(log.read_text(errors="replace"))))


def configure_mac():
    log = BUILD_MAC / "scratch" / "venv.log"
    if not run_logged(["bash", str(REPO_DIR / "dev-scripts" / "dev-venv.sh")], log):
        print(f"creating the venv failed, see {log}")
        return False
    if (BUILD_MAC / "CMakeCache.txt").exists():
        return True
    log = BUILD_MAC / "scratch" / "configure.log"
    if not run_logged(["cmake", "--preset", "mac-release", "-DJP_BUILD_TESTS=OFF"], log):
        print(f"configure failed, see {log}")
        return False
    return True


def build_mac(targets, log_name):
    log = BUILD_MAC / "scratch" / log_name
    ok = run_logged(["cmake", "--build", str(BUILD_MAC), "--target", *targets], log)
    for warning in own_warnings(log):
        print(warning)
    if not ok:
        print(log.read_text(errors="replace")[-4000:])
        print(f"build failed, see {log}")
    return ok


def cmd_generate(plugins):
    if not configure_mac():
        return 1
    return 0 if build_mac([f"jaypacks-{name}-gen" for name in plugins], "generate.log") else 1


def is_jaypacks_bundle(path):
    manifest = path / "manifest.ttl"
    return manifest.exists() and URI_BASE in manifest.read_text(errors="replace")


def cmd_build_mac(plugins, install):
    if not configure_mac():
        return 1
    if not build_mac([f"jaypacks-{name}" for name in plugins], "build.log"):
        return 1
    status = 0
    for name in plugins:
        bundle = BUILD_MAC / "lv2" / f"jaypacks-{name}.lv2"
        print(f"built {bundle}")
        snapshot = SNAPSHOT_DIR / f"{name}.png"
        if snapshot.exists() and not filecmp.cmp(snapshot, screenshot_of(name), shallow=False):
            print(f"warning: {snapshot.relative_to(REPO_DIR)} is out of date, run tools/jp.py snapshot {name}")
        if not install:
            continue
        target = MAC_LV2_DIR / bundle.name
        if target.exists() and not is_jaypacks_bundle(target):
            print(f"not replacing {target}: it is not a jaypacks bundle")
            status = 1
            continue
        if target.exists():
            shutil.rmtree(target)
        MAC_LV2_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copytree(bundle, target)
        print(f"installed {target}")
    return status


def screenshot_of(name):
    return BUILD_MAC / "lv2" / f"jaypacks-{name}.lv2" / "modgui" / "screenshot.png"


def cmd_snapshot(plugins):
    """Copy the generated modgui screenshots into docs/plugins/images/ (the only generated
    files that are committed: the plugin pages show them)."""
    if cmd_build_mac(plugins, install=False) != 0:
        return 1
    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
    for name in plugins:
        target = SNAPSHOT_DIR / f"{name}.png"
        if target.exists() and filecmp.cmp(target, screenshot_of(name), shallow=False):
            print(f"{target.relative_to(REPO_DIR)} is up to date")
            continue
        shutil.copyfile(screenshot_of(name), target)
        print(f"wrote {target.relative_to(REPO_DIR)}")
    return 0


def docker_build(plugins):
    """Configure and build the plugin targets in the container; write readelf reports."""
    build = f"{CONTAINER_REPO}/build-dwarf"
    targets = " ".join(f"jaypacks-{name}" for name in plugins)
    reports = "\n".join(
        f"{READELF} --dyn-syms -V -W {build}/lv2/jaypacks-{name}.lv2/jaypacks-{name}.so "
        f"> {build}/scratch/jaypacks-{name}.readelf.txt 2>&1" for name in plugins)
    script = f"""
set -e
source local.env moddwarf-new >/dev/null 2>&1
mkdir -p {build}/scratch
cmake -S {CONTAINER_REPO} -B {build} -G Ninja -DCMAKE_BUILD_TYPE=Release -DJP_BUILD_TESTS=OFF \\
    -DJP_GRAPHICS=OFF > {build}/scratch/configure.log 2>&1 || {{ echo CONFIGURE_FAILED; exit 1; }}
cmake --build {build} --target {targets} > {build}/scratch/build.log 2>&1 || {{ echo BUILD_FAILED; exit 1; }}
{reports}
"""
    result = subprocess.run(["docker", "run", "--rm", "-v", f"{REPO_DIR}:{CONTAINER_REPO}", "-w", TOOLCHAIN_DIR,
                             IMAGE, "bash", "-lc", script], capture_output=True, text=True)
    scratch = BUILD_DWARF / "scratch"
    if (scratch / "build.log").exists():
        for warning in own_warnings(scratch / "build.log", CONTAINER_REPO):
            print(warning)
    if result.returncode != 0:
        print(result.stdout[-2000:], result.stderr[-2000:])
        for log in ("configure.log", "build.log"):
            if (scratch / log).exists():
                print(f"--- {log}\n" + (scratch / log).read_text(errors="replace")[-4000:])
        return False
    return True


def version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def binary_problems(readelf_report):
    """Problems found in `readelf --dyn-syms -V -W` output: exported symbols other than
    lv2_descriptor, symbol versions newer than the device has."""
    problems = []
    exported = set()
    for line in readelf_report.splitlines():
        fields = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(fields) >= 8 and fields[0].endswith(":") and fields[4] in ("GLOBAL", "WEAK") \
                and fields[6] != "UND" and fields[3] in ("FUNC", "OBJECT"):
            exported.add(fields[7].split("@")[0])
    # _init and _fini are the entry points of the .init and .fini sections in every shared object
    exported -= {"_init", "_fini"}
    if exported != {"lv2_descriptor"}:
        problems.append(f"exports {sorted(exported)}, expected only lv2_descriptor")
    for library, version in set(re.findall(r"Name: (GLIBC|GLIBCXX|CXXABI)_([0-9.]+)", readelf_report)):
        if version_tuple(version) > VERSION_LIMITS[library]:
            limit = ".".join(map(str, VERSION_LIMITS[library]))
            problems.append(f"needs {library}_{version}, the device has {library}_{limit}")
    return problems


def assemble(name):
    """Device bundle: generated files of the Mac build plus the Dwarf binary. Returns problems."""
    problems = []
    mac_bundle = BUILD_MAC / "lv2" / f"jaypacks-{name}.lv2"
    dwarf_bundle = BUILD_DWARF / "lv2" / f"jaypacks-{name}.lv2"
    # the binary was compiled against the container's ports.h; the TTL shipped is the Mac's
    pairs = [(BUILD_MAC / "gen" / name / "ports.h", BUILD_DWARF / "gen" / name / "ports.h")]
    pairs += [(mac_bundle / f.name, f) for f in sorted(dwarf_bundle.glob("*.ttl")) if f.name != "manifest.ttl"]
    for mac_file, dwarf_file in pairs:
        if not mac_file.exists() or not filecmp.cmp(mac_file, dwarf_file, shallow=False):
            problems.append(f"{dwarf_file.name} of the Dwarf build differs from the Mac build")
    target = DWARF_BUNDLES / mac_bundle.name
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(mac_bundle, target, ignore=shutil.ignore_patterns("*.so", ".DS_Store", "._*"))
    shutil.copy2(dwarf_bundle / f"jaypacks-{name}.so", target / f"jaypacks-{name}.so")
    if (target / "modgui.ttl").exists():
        problems += [f"modgui: {problem}" for problem in modgui_check.check_bundle(target)]
    else:
        problems.append("no modgui (Mac build without JP_GRAPHICS?)")
    return problems


def cmd_build_dwarf(plugins):
    # a plugin with scripts also needs them (and the documents about its script API) in the Mac bundle that the device
    # bundle is assembled from
    targets = [f"jaypacks-{name}-gen" for name in plugins] + [
        f"jaypacks-{name}-scripts" for name in plugins if (REPO_DIR / "plugins" / name / "scripts").is_dir()]
    if not configure_mac() or not build_mac(targets, "generate.log"):
        return 1
    if not docker_build(plugins):
        return 1
    status = 0
    for name in plugins:
        report = BUILD_DWARF / "scratch" / f"jaypacks-{name}.readelf.txt"
        problems = binary_problems(report.read_text(errors="replace")) + assemble(name)
        for problem in problems:
            print(f"jaypacks-{name}: {problem}")
        if problems:
            status = 1
        else:
            print(f"built {DWARF_BUNDLES / f'jaypacks-{name}.lv2'} (exports lv2_descriptor only, "
                  f"symbol versions within the device's, modgui ok)")
    return status


def is_aarch64_elf(path):
    with open(path, "rb") as handle:
        head = handle.read(20)
    return head[:4] == b"\x7fELF" and int.from_bytes(head[18:20], "little") == EM_AARCH64


def deploy(name):
    bundle = DWARF_BUNDLES / f"jaypacks-{name}.lv2"
    binary = bundle / f"jaypacks-{name}.so"
    if not binary.exists() or not is_aarch64_elf(binary):
        print(f"jaypacks-{name}: no aarch64 binary in {bundle}, run: tools/jp.py build-dwarf {name}")
        return False
    uri = URI_BASE + name
    answer = modui.sdk_install(modui.tar_gz(bundle))
    info = modui.lv2info_name(uri)
    ok = bool(answer.get("ok")) and uri in answer.get("installed", []) and info.startswith("Name:")
    favorite = ""
    if ok:
        # the author finds uploaded plugins among the favourites in mod-ui
        favorite = ", favourite: " + ("added" if modui.add_favorite(uri) else "already")
    print(f"jaypacks-{name}: {'ok' if ok else 'FAILED'}, sdk/install {answer}, lv2info: {info or 'not found'}{favorite}")
    return ok


def cmd_deploy(plugins, unload):
    if not unload:
        return 0 if all([deploy(name) for name in plugins]) else 1
    with modui.preserved_pedalboard() as board:
        print(f"unloaded {board or '(no pedalboard)'} for the install")
        results = [deploy(name) for name in plugins]
    return 0 if all(results) else 1


def cmd_test_device(plugin, build, no_deploy, restore):
    script = REPO_DIR / "tools" / "device_tests" / f"test_{plugin}.py"
    if not script.exists():
        print(f"no device test for {plugin}: {script} is missing")
        return 1
    plugins = ["siggen", "meter", plugin]
    if build and cmd_build_dwarf(plugins) != 0:
        return 1
    if not no_deploy:
        modui.reset()  # mod-ui refuses to replace plugins that the loaded graph uses
        if not all([deploy(name) for name in plugins]):
            return 1
    command = [sys.executable, str(script)] + (["--restore"] if restore else [])
    return subprocess.run(command).returncode


SCRIPT_ROOT = "/data/jaypacks/scripts"
SCRIPT_NAME_RE = re.compile(r"^[A-Za-z0-9_-]{1,47}$")


def script_remote_path(plugin, name, suffix=".lua"):
    """Where a script (or its log) of a plugin lives on the device; plugin and name are checked, they end up in a
    shell command."""
    if not re.fullmatch(r"[a-z][a-z0-9]*", plugin) or not SCRIPT_NAME_RE.match(name):
        raise ValueError(f"not a plugin or script name: {plugin!r}, {name!r} (letters, digits, - and _)")
    return f"{SCRIPT_ROOT}/{plugin}/{name}{suffix}"


def cmd_push_script(plugin, name, file):
    path = Path(file)
    data = path.read_bytes()
    if any(byte > 126 or (byte < 32 and byte not in (9, 10, 13)) for byte in data):
        print(f"{file}: only plain ASCII text is accepted (no tabs other than 9, no control or non-ASCII characters)")
        return 1
    target = script_remote_path(plugin, name)
    modui.ssh(f"mkdir -p {SCRIPT_ROOT}/{plugin}")
    result = subprocess.run(["scp", "-O", *modui.SSH_OPTS, str(path), f"root@{modui.HOST}:{target}"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr)
        return 1
    print(f"{file} -> {modui.HOST}:{target} ({len(data)} bytes); the plugin reloads it within about a second, "
          f"read the result with: tools/jp.py script-log {plugin} {name}")
    return 0


def cmd_script_log(plugin, name):
    text = modui.ssh(f"cat {script_remote_path(plugin, name, '.log')} 2>&1; true")
    print(text if text.strip() else "(no log yet: the plugin writes it when it loads the script)")
    return 0


def cmd_perf(plugins, build, no_deploy, cycles, restore):
    needed = sorted(set(plugins) | {"siggen"})
    if build and cmd_build_dwarf(needed) != 0:
        return 1
    if not no_deploy:
        modui.reset()  # mod-ui refuses to replace plugins that the loaded graph uses
        if not all([deploy(name) for name in needed]):
            return 1
    command = [sys.executable, str(REPO_DIR / "tools" / "device_tests" / "perf.py"), *plugins,
               "--cycles", str(cycles)] + (["--restore"] if restore else [])
    return subprocess.run(command).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("generate", help="run the generator").add_argument("plugins", nargs="*")
    mac = sub.add_parser("build-mac", help="native build on the Mac")
    mac.add_argument("plugins", nargs="*")
    mac.add_argument("--install", action="store_true", help=f"copy the bundles to {MAC_LV2_DIR}")
    sub.add_parser("snapshot", help="copy modgui screenshots to docs/plugins/images/").add_argument("plugins", nargs="*")
    sub.add_parser("build-dwarf", help="cross-build in docker").add_argument("plugins", nargs="*")
    dep = sub.add_parser("deploy", help=f"install on the device ({modui.HOST})")
    dep.add_argument("plugins", nargs="*")
    dep.add_argument("--unload", action="store_true", help="unload the loaded pedalboard during the install")
    test = sub.add_parser("test-device", help="device integration test of one plugin")
    test.add_argument("plugin")
    test.add_argument("--build", action="store_true", help="cross-build siggen, meter and the plugin first")
    test.add_argument("--no-deploy", action="store_true", help="use what is installed on the device")
    test.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    perf = sub.add_parser("perf", help="measure CPU and memory on the device")
    perf.add_argument("plugins", nargs="*")
    perf.add_argument("--build", action="store_true", help="cross-build siggen and the plugins first")
    perf.add_argument("--no-deploy", action="store_true", help="use what is installed on the device")
    perf.add_argument("--cycles", type=int, default=3, help="cycles with and without the plugin")
    perf.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    push = sub.add_parser("push-script", help="copy a Lua script to the plugin's scripts folder on the device")
    push.add_argument("plugin")
    push.add_argument("name")
    push.add_argument("file")
    slog = sub.add_parser("script-log", help="print the log the plugin wrote next to a script")
    slog.add_argument("plugin")
    slog.add_argument("name")
    args = parser.parse_args()
    if args.command == "push-script":
        return cmd_push_script(args.plugin, args.name, args.file)
    if args.command == "script-log":
        return cmd_script_log(args.plugin, args.name)
    if args.command == "perf":
        return cmd_perf(select_plugins(args.plugins), args.build, args.no_deploy, args.cycles, args.restore)
    if args.command == "test-device":
        return cmd_test_device(select_plugins([args.plugin])[0], args.build, args.no_deploy, args.restore)
    if args.command == "snapshot":
        existing = sorted(p.stem for p in SNAPSHOT_DIR.glob("*.png"))
        if not args.plugins and not existing:
            raise SystemExit("no snapshots yet: name the plugins")
        return cmd_snapshot(select_plugins(args.plugins or existing))
    plugins = select_plugins(args.plugins)
    if args.command == "generate":
        return cmd_generate(plugins)
    if args.command == "build-mac":
        return cmd_build_mac(plugins, args.install)
    if args.command == "build-dwarf":
        return cmd_build_dwarf(plugins)
    return cmd_deploy(plugins, args.unload)


if __name__ == "__main__":
    sys.exit(main())
