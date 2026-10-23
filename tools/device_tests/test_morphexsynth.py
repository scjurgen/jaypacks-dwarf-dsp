#!/usr/bin/env python3
"""Device integration test of morphexsynth on the Dwarf (Morphexsynth step 8).

Needs meter and morphexsynth installed (tools/jp.py test-device morphexsynth deploys them) and
the Mac test build for the expected values (build-tests, target morphexsynth_expected). Empties
the graph and builds
  morphexsynth/outL -> meter/signal, morphexsynth/outR -> meter/reference,
  mod-ui's MIDI input (mod-midi-merger) -> morphexsynth/midiIn
(nothing goes to the audio outputs during the test), plays MIDI the way a USB controller does
(ALSA sequencer, tools/device_tests/alsa_midi_send.py) and checks with the meter's readings:
  - silence before a note,
  - CPU: mean mod-ui CPU with the plugin idle and with a chord of 12 notes held for 10 s against
    the same graph before the plugin was added (the full figures: tools/jp.py perf), and the
    chord's 10 s without a non-finite or a denormal sample,
  - what a note sounds like against the same pedal on the Mac (morphexsynth_expected, the meter's
    analysis): level of both channels and the zero-crossing frequency, effects off and with the
    patch's effects, a note on a member channel with its pitch bend (Bend range 2, and the
    default 12), and the sustained patches one by one,
  - a soft and a hard note (velocity 40 and 127) against the Mac,
  - the zone ports (a channel outside the zone is silent), the sustain pedal (CC 64), Attack and Release (a
    release offset of 3 s keeps a released note sounding 1.5 s later, the patch's own release does not),
  - scripts: the bundle ships the ten factory scripts and API.md, a user script written over ssh is loaded and
    changes the level by the decibels it sets, an edited file is reloaded within 1.2 s, a broken one is
    rejected (status 3, the log says so, the sound stays), a missing one reports status 2, a hook acts and
    the Patch menu comes back with Script = 0 (user slot 8 only; a script that is there is saved and restored),
  - every control at its minimum and at its maximum, and all ten patches in turn, while a chord is
    held: no non-finite and no denormal samples,
  - xruns unchanged while the graph processes, no problem lines in the jack2 journal,
then connects the synth to the audio outputs, addresses knob 1 = Cutoff, knob 2 = Resonance and
knob 3 = Vol, and leaves the board loaded for playing (with --restore: reloads the previous
pedalboard). Raw messages go to build-dwarf/scratch/test_morphexsynth.log.

Usage:
  test_morphexsynth.py [--restore]
"""

import argparse
import json
import math
import subprocess
import sys
import time

import devicetest
from devicetest import FLOOR_DB, REPO_DIR, URI_BASE, modui

sys.path.insert(0, str(REPO_DIR / "generator"))
import jpgen  # noqa: E402

PREFIX = "test_morphexsynth"
SYNTH = "morphexsynth"
SAMPLE_RATE = 48000.0
EXPECTED_TOOL = REPO_DIR / "build-tests" / "tests" / "host" / "morphexsynth_expected"
SYNTH_JSON = REPO_DIR / "plugins" / SYNTH / f"{SYNTH}.json"
# the midi sender counts from the moment its routing is in place and ssh adds a few hundred ms before
# the script knows the notes are on: levels are compared after the envelopes have settled
SETTLE = 3.0
WINDOW = 1.0
CPU_WINDOW = 5.0
CPU_IDLE_CEILING_POINTS = 20.0
CPU_CHORD_CEILING_POINTS = 70.0
LEVEL_TOLERANCE_DB = 0.3
FULL_PATCH_TOLERANCE_DB = 1.5
FREQUENCY_TOLERANCE = 0.002
CHORD = [(36 + 4 * i, 1, 100) for i in range(12)]
# patches that hold a level (sustain 0.8 or more): the level does not depend on when the note began
SUSTAINED_PATCHES = {0: "4 Ever Mini", 1: "Airy Pad", 3: "Cutting Lead", 7: "Retro Poly", 8: "Section Brass",
                     9: "Warm Bass"}
EFFECTS_OFF = {"reverb": -60, "chorus": 0, "phaser": 0}
NOTE_ON, NOTE_OFF, CONTROL_CHANGE, PITCH_BEND = 0x90, 0x80, 0xB0, 0xE0


def synth_controls():
    desc = jpgen.validate(jpgen.load(SYNTH_JSON))
    return {port["symbol"]: jpgen.control_range(port) for port in desc["ports-control"]}, \
        {port["symbol"]: port["default"] for port in desc["ports-control"]}


def expected(settle, window, controls, notes, bends=()):
    if not EXPECTED_TOOL.exists():
        subprocess.run(["cmake", "--build", str(REPO_DIR / "build-tests"), "--target", "morphexsynth_expected"],
                       check=True, capture_output=True)
    command = [str(EXPECTED_TOOL), "tone", str(SAMPLE_RATE), str(settle), str(window)]
    command += [f"{k}={v}" for k, v in controls.items()]
    command.append("notes=" + ",".join(f"{note}:{channel}:{velocity}" for note, channel, velocity in notes))
    if bends:
        command.append("bend=" + ",".join(f"{channel}:{value}" for channel, value in bends))
    return json.loads(subprocess.run(command, check=True, capture_output=True, text=True).stdout)


def note_messages(notes, bends=(), start_ms=20):
    """Bends first (an MPE controller sends them before the note on), then the notes."""
    messages = []
    for channel, value in bends:
        messages.append((start_ms, [PITCH_BEND | (channel - 1), value & 0x7F, value >> 7]))
    for i, (note, channel, velocity) in enumerate(notes):
        messages.append((start_ms + 5 * (len(bends) + i), [NOTE_ON | (channel - 1), note, velocity]))
    return messages


def all_notes_off():
    modui.midi_send([(5, [CONTROL_CHANGE, 123, 0])])


def fresh_synth(graph):
    """A new instance of the synth (default controls, empty effects), wired to the meter and to MIDI."""
    modui.remove_plugin(SYNTH)
    graph.reader.poll(0.5)
    wire_synth()
    graph.reader.poll(1.0)


def wire_synth():
    modui.add_plugin(SYNTH, URI_BASE + SYNTH, x=350, y=200)
    modui.connect(f"{SYNTH}/outL", "meter/signal")
    modui.connect(f"{SYNTH}/outR", "meter/reference")
    modui.connect("midi_merger_out", f"{SYNTH}/midiIn")


def readings(graph):
    return {name: graph.out("meter", name) for name in ("peak", "rms", "refRms", "frequency", "nonFinite", "denormals")}


def no_bad_samples(check, graph, what):
    check.near(f"{what}: non-finite samples", graph.out("meter", "nonFinite"), 0.0, 0.0)
    check.near(f"{what}: denormal samples", graph.out("meter", "denormals"), 0.0, 0.0)


def check_silence(graph, check):
    graph.set("meter", {"window": 1.0})
    graph.measure(1.0)
    # what is left of the reverb tail of the chord before is far below any sound
    check.below("peak with no note (dB)", graph.out("meter", "peak"), -120.0)


def check_cpu(graph, check):
    """CPU of the graph with the meter alone, with the idle synth, and with a chord of 12 notes for 10 s."""
    start = time.monotonic()
    graph.reader.poll(CPU_WINDOW)
    before, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    wire_synth()
    graph.reader.poll(1.5)
    start = time.monotonic()
    graph.reader.poll(CPU_WINDOW)
    idle, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    graph.set(SYNTH, {"patch": 7})
    graph.set("meter", {"window": 10.0})
    modui.midi_send(note_messages(CHORD))
    graph.reader.poll(1.0)
    start = time.monotonic()
    measure_id = graph.start_measure()
    graph.reader.poll(10.0)
    chord, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    graph.wait_result(measure_id, 10.0)
    if not before or not idle or not chord:
        check.record(False, "no stats messages for the CPU comparison")
        return
    mean_before = sum(before) / len(before)
    mean_idle = sum(idle) / len(idle)
    mean_chord = sum(chord) / len(chord)
    check.info(f"cpu without the synth {mean_before:.1f} %, idle {mean_idle:.1f} %, 12 notes of Retro Poly "
               f"{mean_chord:.1f} %")
    check.below("cpu added by the idle synth (percentage points)", mean_idle - mean_before, CPU_IDLE_CEILING_POINTS)
    check.below("cpu added by the chord (percentage points)", mean_chord - mean_before, CPU_CHORD_CEILING_POINTS)
    no_bad_samples(check, graph, "chord of 12 notes for 10 s")
    check.above("chord still alive after 10 s, rms (dB)", graph.out("meter", "rms"), -60.0)
    all_notes_off()
    graph.set("meter", {"window": WINDOW})
    graph.reader.poll(6.0)  # the release (400 ms) and the reverb tail (T60 1.8 s) of Retro Poly


def power_mean_db(left, right):
    """The mean power of two levels in dB."""
    return 10.0 * math.log10((10.0 ** (left / 10.0) + 10.0 ** (right / 10.0)) / 2.0)


def compare_tone(graph, check, label, controls, notes, bends=(), level_tolerance=LEVEL_TOLERANCE_DB, settle=SETTLE,
                 frequency=True, moving_effects=False):
    """Plays the notes on a fresh instance with the controls and compares the meter's readings with the Mac's.
    The zero-crossing frequency is only compared for a single note without moving effects or noise (frequency). With
    moving effects (the chorus sweeps the two channels in opposite phase from the start of the plugin, which the
    device and the Mac do not share to the sample) the levels of the two channels are compared by their mean power."""
    fresh_synth(graph)
    graph.set("meter", {"window": WINDOW})
    graph.set(SYNTH, controls)
    graph.reader.poll(0.5)
    modui.midi_send(note_messages(notes, bends))
    graph.reader.poll(settle)
    graph.measure(WINDOW)
    device = readings(graph)
    mac = expected(settle, WINDOW, controls, notes, bends)
    if moving_effects:
        check.near(f"{label}: mean power of both channels (dB)", power_mean_db(device["rms"], device["refRms"]),
                   power_mean_db(mac["rms"], mac["rightRms"]), level_tolerance)
    else:
        check.near(f"{label}: left rms (dB)", device["rms"], mac["rms"], level_tolerance)
        check.near(f"{label}: right rms (dB)", device["refRms"], mac["rightRms"], level_tolerance)
    check.near(f"{label}: peak (dB)", device["peak"], mac["peak"], 3 * level_tolerance)
    if frequency and mac["frequency"] > 0.0:
        check.near(f"{label}: zero-crossing frequency (Hz)", device["frequency"], mac["frequency"],
                   max(FREQUENCY_TOLERANCE * mac["frequency"], 0.05))
    no_bad_samples(check, graph, label)
    all_notes_off()
    graph.reader.poll(0.5)
    return device


def check_tones(graph, check):
    note = [(57, 1, 100)]
    compare_tone(graph, check, "Retro Poly A3, effects off", dict(EFFECTS_OFF, patch=7), note)
    compare_tone(graph, check, "Retro Poly A3 with its effects", {"patch": 7}, note,
                 level_tolerance=FULL_PATCH_TOLERANCE_DB, frequency=False, moving_effects=True)
    bent = compare_tone(graph, check, "Retro Poly A3 on channel 2, bend range 2",
                        dict(EFFECTS_OFF, patch=7, bendRange=2), [(57, 2, 100)], [(2, 16383)])
    octave = compare_tone(graph, check, "Retro Poly A3 on channel 2, the default bend range (12)",
                          dict(EFFECTS_OFF, patch=7), [(57, 2, 100)], [(2, 16383)])
    if bent["frequency"] and octave["frequency"]:
        check.info(f"bend frequencies {bent['frequency']:.2f} Hz (range 2) and {octave['frequency']:.2f} Hz (range 12)")
    # a tritone: no simple ratio, so the level does not depend on the relative phase of the two notes
    compare_tone(graph, check, "Cutting Lead, two notes a tritone apart, effects off", dict(EFFECTS_OFF, patch=3),
                 [(48, 1, 100), (54, 1, 80)], level_tolerance=0.5, frequency=False)
    # the velocity: the level of a soft and of a hard note against the Mac (12 dB between 30 and 127, Warm Bass
    # also gets a brighter sound, which the level of the sustained note shows)
    for velocity in (40, 127):
        compare_tone(graph, check, f"Warm Bass A2 at velocity {velocity}, effects off", dict(EFFECTS_OFF, patch=9),
                     [(45, 1, velocity)], level_tolerance=0.5, settle=4.0)
    for patch, name in SUSTAINED_PATCHES.items():
        if patch == 7:
            continue
        # the zero crossings are a pitch only where one oscillator dominates: not with the noise of 4 Ever Mini,
        # nor with the detuned square and saws of Section Brass (their beat moves the crossings with the time)
        compare_tone(graph, check, f"{name} C3, effects off", dict(EFFECTS_OFF, patch=patch), [(48, 1, 100)],
                     level_tolerance=0.5, settle=4.0, frequency=patch in (1, 3, 9))


def check_zone_and_pedal(graph, check):
    fresh_synth(graph)
    graph.set("meter", {"window": WINDOW})
    graph.set(SYNTH, dict(EFFECTS_OFF, patch=9, mpeUpper=5))
    graph.reader.poll(0.5)
    modui.midi_send(note_messages([(45, 8, 100)]))
    graph.reader.poll(1.0)
    graph.measure(WINDOW)
    check.near("note on channel 8 with the zone ending at 5: peak (dB)", graph.out("meter", "peak"), FLOOR_DB, 0.0)
    modui.midi_send(note_messages([(45, 3, 100)]))
    graph.reader.poll(1.0)
    graph.measure(WINDOW)
    check.above("note on channel 3: peak (dB)", graph.out("meter", "peak"), -40.0)
    all_notes_off()
    graph.reader.poll(1.5)
    graph.set(SYNTH, {"mpeUpper": 16})

    modui.midi_send([(5, [CONTROL_CHANGE, 64, 127]), (20, [NOTE_ON, 45, 100]), (400, [NOTE_OFF, 45, 64])])
    graph.reader.poll(1.5)
    graph.measure(WINDOW)
    check.above("note released under the sustain pedal: peak (dB)", graph.out("meter", "peak"), -40.0)
    modui.midi_send([(5, [CONTROL_CHANGE, 64, 0])])
    graph.reader.poll(1.5)
    graph.measure(WINDOW)
    check.below("after the pedal lifts: peak (dB)", graph.out("meter", "peak"), -90.0)


def check_envelope_offsets(graph, check):
    fresh_synth(graph)
    graph.set("meter", {"window": WINDOW})
    for release, sounding in ((0, False), (3000, True)):
        graph.set(SYNTH, dict(EFFECTS_OFF, patch=9, release=release))
        graph.reader.poll(0.5)
        modui.midi_send([(20, [NOTE_ON, 45, 100]), (400, [NOTE_OFF, 45, 64])])
        graph.reader.poll(1.9)
        graph.measure(WINDOW)
        peak = graph.out("meter", "peak")
        if sounding:
            check.above("Warm Bass released 1.5 s ago with release +3000 ms: peak (dB)", peak, -70.0)
        else:
            check.below("Warm Bass released 1.5 s ago with its own release: peak (dB)", peak, -90.0)
        all_notes_off()
        graph.reader.poll(3.5)


def check_extremes(graph, check, ranges, defaults):
    fresh_synth(graph)
    graph.set(SYNTH, defaults)
    graph.reader.poll(0.5)
    modui.midi_send(note_messages(CHORD[:8]))
    graph.reader.poll(1.0)
    for symbol, (low, high) in ranges.items():
        for value in (low, high):
            graph.set(SYNTH, {symbol: value})
            graph.reader.poll(0.4)
            graph.measure(WINDOW)
            no_bad_samples(check, graph, f"{symbol} = {value}")
        graph.set(SYNTH, {symbol: defaults[symbol]})
    all_notes_off()
    graph.reader.poll(1.0)


def check_patch_sweep(graph, check):
    fresh_synth(graph)
    graph.set("meter", {"window": 8.0})
    modui.midi_send(note_messages(CHORD[:8]))
    graph.reader.poll(1.0)
    measure_id = graph.start_measure()
    for patch in list(range(10)) + [7]:
        graph.set(SYNTH, {"patch": patch})
        graph.reader.poll(0.6)
    graph.wait_result(measure_id, 8.0)
    no_bad_samples(check, graph, "all ten patches in turn with 8 notes held")
    check.above("rms at the end of the sweep (dB)", graph.out("meter", "rms"), -80.0)
    all_notes_off()
    graph.set("meter", {"window": WINDOW})
    graph.reader.poll(1.0)


USER_FOLDER = "/data/jaypacks/scripts/morphexsynth"
BUNDLE_FOLDER = f"/root/.lv2/jaypacks-{SYNTH}.lv2"
TEST_SLOT = 8
TEST_SCRIPT = f"{USER_FOLDER}/user-{TEST_SLOT}.lua"
TEST_LOG = f"{USER_FOLDER}/user-{TEST_SLOT}.log"
RELOAD_WAIT = 1.2


def script_text(volume_db, hook=""):
    return ("function OnStart()\n"
            f"    SetVolume({volume_db})\n"
            "    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5 })\n"
            "    SetAmpEnvelope({ attackMs = 2, decayMs = 100, sustainLevel = 0.8, releaseMs = 50 })\n"
            "    SetFilter({ cutoff = 100 })\n"
            "end\n" + hook)


def write_remote(path, text):
    """Writes a text file on the Dwarf over ssh (the folder is created, as a user would do once)."""
    folder = path.rsplit("/", 1)[0]
    subprocess.run(["ssh", *modui.SSH_OPTS, f"root@{modui.HOST}", f"mkdir -p {folder} && cat > {path}"], input=text,
                   text=True, check=True, timeout=30)


def read_remote(path):
    try:
        return modui.ssh(f"cat {path} 2>/dev/null; true")
    except modui.DeviceError:
        return ""


def played_level(graph):
    """rms (dB) of one note of the loaded sound, with the effects off (a fresh note: the old one was released)."""
    modui.midi_send([(20, [NOTE_ON, 57, 100])])
    graph.reader.poll(2.0)
    graph.measure(WINDOW)
    level = graph.out("meter", "rms")
    all_notes_off()
    graph.reader.poll(0.8)
    return level


def check_user_scripts(graph, check):
    """The scripts of the plugin on the Dwarf (docs/scripting.md): the bundle, the slot, the files, the hot reload."""
    listing = modui.ssh(f"ls {BUNDLE_FOLDER}/scripts 2>&1; true").split()
    names = ["4-ever-mini", "airy-pad", "crystal-bells", "cutting-lead", "grand-stage", "harp-pluck", "kick-punch",
             "retro-poly", "section-brass", "warm-bass"]
    check.record(all(f"{name}.lua" in listing for name in names), "the bundle ships the ten factory scripts")
    check.record("API.md" in listing and "constants.lua" in listing, "the bundle ships API.md and constants.lua")
    api = modui.ssh(f"head -1 {BUNDLE_FOLDER}/scripts/API.md 2>&1; true").strip()
    check.record(api == "# Morphexsynth scripting API", f"API.md starts with its title ({api!r})")

    saved = read_remote(TEST_SCRIPT)
    saved_log = read_remote(TEST_LOG)
    try:
        fresh_synth(graph)
        graph.set("meter", {"window": WINDOW})
        graph.set(SYNTH, dict(EFFECTS_OFF, vol=0))
        graph.reader.poll(0.5)
        check.near("script status with the Patch menu", graph.out(SYNTH, "scriptStatus"), 1.0, 0.0)
        factory = played_level(graph)

        write_remote(TEST_SCRIPT, script_text(-6))
        graph.set(SYNTH, {"script": TEST_SLOT})
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status of a user script", graph.out(SYNTH, "scriptStatus"), 1.0, 0.0)
        quiet = played_level(graph)
        check.info(f"factory Retro Poly {factory:.1f} dB, user script at -6 dB {quiet:.1f} dB")

        write_remote(TEST_SCRIPT, script_text(6))
        graph.reader.poll(RELOAD_WAIT)
        loud = played_level(graph)
        check.near("an edited file is reloaded within 1.2 s: level change (dB, expected +12)", loud - quiet, 12.0, 0.3)

        write_remote(TEST_SCRIPT, "function OnStart( oops\n")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status of a broken file (3: compile error)", graph.out(SYNTH, "scriptStatus"), 3.0, 0.0)
        kept = played_level(graph)
        check.near("the sound stays when a file is broken (dB)", kept, loud, 0.3)
        log = read_remote(TEST_LOG)
        check.record("compile error" in log and f"user-{TEST_SLOT}:" in log,
                     "the log file next to the script names the error and the line (user-8:<line>:)")

        write_remote(TEST_SCRIPT, script_text(-6, "function OnNoteOn(ch, note, vel) SetVolume(-40) end\n"))
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status after the file is fixed", graph.out(SYNTH, "scriptStatus"), 1.0, 0.0)
        modui.midi_send([(20, [NOTE_ON, 57, 100])])
        graph.reader.poll(2.0)
        graph.measure(WINDOW)
        hooked = graph.out("meter", "rms")
        all_notes_off()
        graph.reader.poll(0.8)
        check.below("a hook acts: OnNoteOn turns the patch down by 34 dB (rms, dB)", hooked, quiet - 25.0)

        modui.ssh(f"rm -f {TEST_SCRIPT}")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status when the file is removed (2: file missing)", graph.out(SYNTH, "scriptStatus"), 2.0, 0.0)

        graph.set(SYNTH, {"script": 0})
        graph.reader.poll(0.5)
        check.near("script status back with the Patch menu", graph.out(SYNTH, "scriptStatus"), 1.0, 0.0)
        check.near("the Patch menu sounds as before (dB)", played_level(graph), factory, 0.3)
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
            if saved_log:
                write_remote(TEST_LOG, saved_log)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT} {TEST_LOG}; true")


def morph_script(first, extra="", oscillators=1):
    """A sustained patch whose oscillators play `first`, then `extra` (SetWaveMorph calls)."""
    levels = "".join(f"    SetOscillator({i}, {{ waveform = OscWaveform.{first}, level = {0.5 / oscillators:.3f}, "
                     f"detune = {0.1 * i:.2f} }})\n" for i in range(oscillators))
    return ("function OnStart()\n    SetVolume(-6)\n" + levels +
            "    SetAmpEnvelope({ attackMs = 2, decayMs = 100, sustainLevel = 0.8, releaseMs = 50 })\n"
            "    SetFilter({ cutoff = 100 })\n" + extra + "end\n")


def chord_cpu(graph, text, seconds=5.0):
    """Mean mod-ui CPU with a chord of 8 notes held on the script (user slot 8)."""
    write_remote(TEST_SCRIPT, text)
    graph.reader.poll(RELOAD_WAIT)
    modui.midi_send(note_messages(CHORD[:8]))
    graph.reader.poll(1.0)
    start = time.monotonic()
    graph.reader.poll(seconds)
    cpu, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    all_notes_off()
    graph.reader.poll(1.5)
    return sum(cpu) / len(cpu) if cpu else None


def check_morph(graph, check):
    """The wavetable morph on the device (docs/scripting.md): position 0 and 1 against plain waveforms, and what the
    second table read costs with 8 notes of three oscillators."""
    saved = read_remote(TEST_SCRIPT)
    saved_log = read_remote(TEST_LOG)
    try:
        fresh_synth(graph)
        graph.set("meter", {"window": WINDOW})
        graph.set(SYNTH, dict(EFFECTS_OFF, vol=0, script=TEST_SLOT))
        third = "    SetWaveMorph(0, { waveform2 = OscWaveform.White, waveform3 = OscWaveform.Triangle, morph = %s })\n"
        levels = {}
        for label, text in (("saw", morph_script("Saw")),
                            ("triangle", morph_script("Triangle")),
                            ("saw, morph 0", morph_script("Saw", third % "0")),
                            ("saw, morph 1 to triangle", morph_script("Saw", third % "1")),
                            ("saw, morph 0.25 toward white", morph_script("Saw", third % "0.25"))):
            write_remote(TEST_SCRIPT, text)
            graph.reader.poll(RELOAD_WAIT)
            check.near(f"morph script '{label}': status", graph.out(SYNTH, "scriptStatus"), 1.0, 0.0)
            levels[label] = played_level(graph)
        check.info("levels (dB): " + ", ".join(f"{label} {value:.2f}" for label, value in levels.items()))
        check.near("morph 0 sounds as the plain saw (dB)", levels["saw, morph 0"], levels["saw"], 0.05)
        check.near("morph 1 with a triangle as the third waveform sounds as the plain triangle (dB)",
                   levels["saw, morph 1 to triangle"], levels["triangle"], 0.05)
        check.record(abs(levels["saw, morph 0.25 toward white"] - levels["saw"]) > 0.2,
                     "a position between the slots sounds different from the first waveform")
        morphing = "".join(f"    SetWaveMorph({i}, {{ waveform2 = OscWaveform.Square, waveform3 = OscWaveform.Triangle, "
                           f"morph = 0.5 }})\n" for i in range(3))
        # three oscillators; a morph at 0.5 reads the second slot alone, at 0.25 two tables per sample
        crossfading = morphing.replace("0.5 }", "0.25 }")
        plain_cpu = chord_cpu(graph, morph_script("Saw", "", 3))
        slot_cpu = chord_cpu(graph, morph_script("Saw", morphing, 3))
        mixed_cpu = chord_cpu(graph, morph_script("Saw", crossfading, 3))
        if None in (plain_cpu, slot_cpu, mixed_cpu):
            check.record(False, "no stats messages for the morph CPU comparison")
        else:
            check.info(f"cpu with 8 notes of 3 oscillators: plain {plain_cpu:.1f} %, morph on a slot "
                       f"{slot_cpu:.1f} %, crossfading two tables {mixed_cpu:.1f} % (points of mod-ui's DSP load)")
            check.below("cpu added by crossfading two tables on all 24 oscillators (percentage points)",
                        mixed_cpu - plain_cpu, 15.0)
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
            if saved_log:
                write_remote(TEST_LOG, saved_log)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT} {TEST_LOG}; true")


def check_addressing(check):
    modui.address(f"/graph/{SYNTH}/cutoff", "/hmi/knob1", "Cutoff", -48.0, 48.0, 0.0, 97)
    modui.address(f"/graph/{SYNTH}/resonance", "/hmi/knob2", "Resonance", -100.0, 100.0, 0.0, 101)
    modui.address(f"/graph/{SYNTH}/vol", "/hmi/knob3", "Vol", -40.0, 12.0, 0.0, 105)
    fresh = modui.WebSocket()  # a new client gets the whole state, hw_map messages included
    try:
        maps = {modui.parse_hw_map(message) for message in fresh.drain(2.0)}
    finally:
        fresh.close()
    check.record((SYNTH, "cutoff", "/hmi/knob1") in maps, "hw_map: knob 1 -> cutoff")
    check.record((SYNTH, "resonance", "/hmi/knob2") in maps, "hw_map: knob 2 -> resonance")
    check.record((SYNTH, "vol", "/hmi/knob3") in maps, "hw_map: knob 3 -> vol")


def run(restore):
    missing = devicetest.missing_plugins(["meter", SYNTH])
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py test-device {SYNTH} deploys them)")
        return 1
    ranges, defaults = synth_controls()
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"{PREFIX}: pedalboard before the test: {board or '(none)'}")
        cursor = devicetest.journal_cursor()
        modui.reset()
        modui.add_plugin("meter", URI_BASE + "meter", x=650, y=200)
        graph = devicetest.Graph()
        start = time.monotonic()
        processing = start
        step_starts = []
        try:
            graph.reader.poll(1.0)
            print(f"{PREFIX}: cpu ...", flush=True)
            check_cpu(graph, check)
            processing = time.monotonic()
            steps = [
                ("silence", lambda: check_silence(graph, check)),
                ("tones against the Mac", lambda: check_tones(graph, check)),
                ("zone and sustain pedal", lambda: check_zone_and_pedal(graph, check)),
                ("attack and release", lambda: check_envelope_offsets(graph, check)),
                ("extremes", lambda: check_extremes(graph, check, ranges, defaults)),
                ("patch sweep", lambda: check_patch_sweep(graph, check)),
                ("user scripts", lambda: check_user_scripts(graph, check)),
                ("wavetable morph", lambda: check_morph(graph, check)),
            ]
            for name, step in steps:
                print(f"{PREFIX}: {name} ...", flush=True)
                step_starts.append((time.monotonic(), name))
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
            previous = None
            for arrival, message in graph.reader.log:
                parts = message.split()
                if parts and parts[0] == "stats" and len(parts) >= 3:
                    if previous is not None and int(float(parts[2])) != previous:
                        step = [name for began, name in step_starts if began <= arrival]
                        check.info(f"xruns {previous} -> {int(float(parts[2]))} reported at {arrival - start:.1f} s, "
                                   f"during step '{step[-1] if step else 'cpu'}'")
                    previous = int(float(parts[2]))
            check.record(processing_xruns[-1] == processing_xruns[0],
                         f"xruns while processing {processing_xruns[0]} -> {processing_xruns[-1]} (expected unchanged)")
        else:
            check.record(False, "no stats messages: xruns unknown")
        lines = devicetest.journal_lines(cursor)
        problems = devicetest.journal_problems(lines)
        check.record(not problems, f"jack2 journal: {len(problems)} problem lines among {len(lines)} during the run")
        for line in problems[:10]:
            check.info(f"journal: {line}")
        # the board for playing: a fresh synth on the audio outputs and the hardware knobs
        try:
            fresh = devicetest.Graph()
            try:
                fresh_synth(fresh)
            finally:
                fresh.close()
            modui.connect(f"{SYNTH}/outL", "playback_1")
            modui.connect(f"{SYNTH}/outR", "playback_2")
            check_addressing(check)
        except modui.DeviceError as error:
            check.record(False, f"board for playing: {error}")
    check.info(f"raw messages: {log}")
    check.info("previous pedalboard restored" if restore
               else "board left loaded for playing (synth on the outputs, MIDI from the merger, knob 1 = cutoff, "
                    "knob 2 = resonance, knob 3 = vol)")
    return check.report(PREFIX)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.restore)


if __name__ == "__main__":
    sys.exit(main())
