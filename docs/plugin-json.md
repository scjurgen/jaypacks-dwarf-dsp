# Plugin description (JSON)

Every plugin is described by `plugins/<name>/<name>.json`. `generator/jpgen.py` validates
it and writes the port table `ports.h` and the TTL files of the bundle. This page is the
reference of every field; `plugins/shipahoy/shipahoy.json` is a complete example, `plugins/meter/meter.json` one with control outputs and
`plugins/siggen/siggen.json` one without audio input. All text is printable ASCII.

## Top level

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | Lower case letters and digits, starting with a letter; must equal the file name. Gives the URI `https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/<name>`, the bundle `jaypacks-<name>.lv2` and the binary `jaypacks-<name>.so`. |
| `displayname` | yes | Name shown by hosts (`doap:name`, `mod:label`); 1 to 16 characters. |
| `plugintype` | yes | LV2 plugin class without prefix, e.g. `FilterPlugin`, `DelayPlugin`, `ReverbPlugin` (the list is in `jpgen.py`). |
| `major`, `minor`, `micro` | yes | Version. `minor` and `micro` become `lv2:minorVersion` and `lv2:microVersion`. LV2 has no major version: an incompatible change needs a new URI, so `major` only appears in `ports.h` (`c_version`). LV2 treats an odd `minor` as a development version; mod-ui shows `minor.micro` as the version and calls 0.1.0 "testing". |
| `shortdescription` | yes | One line. |
| `description` | yes | List of lines; joined with newlines into `rdfs:comment`. |
| `audio-ports-in`, `audio-ports-out` | yes | Lists of `{"name": ..., "symbol": ...}`. `audio-ports-in` may be empty (a signal source such as siggen); `audio-ports-out` may not, unless the plugin has `"midi-out"` (a MIDI processor has no audio ports at all). |
| `ports-control` | yes | List of control inputs, see below; the order is the port order. |
| `ports-control-out` | no | List of control outputs (values the plugin reports), see below. |
| `midi-in` | no | `true` adds a MIDI input, see below. Default `false`. |
| `midi-out` | no | `true` adds a MIDI output, see below. Default `false`. |
| `worker` | no | `true` makes the host's worker thread available to the pedal, see below. Default `false`. |
| `presets` | no | List of presets, see below. |
| `CPP` | yes | `{"CLASS_NAME": "jp::ShipAhoyPedal", "INCLUDE": "plugins/shipahoy/ShipAhoyPedal.h"}`: the pedal class and its header, relative to the repository root. |
| `color` | yes | Plugin colour (the accent: acrylic rim, value arc, footswitch ring, brand lines; the casing is milky for every plugin): a name from `COLORS` in `jpgen.py` (e.g. `Filter`, `Delay`, `Reverb`) or `#rrggbb`. The named colours keep the hues of the old table at HLS saturation 1, lightness 0.4 (`Wah` is grey). |
| `status` | no | Kept from the old format (`alpha`, `beta`); not used. |

Unknown keys are errors, so a typo does not go unnoticed.

## Control ports

| Key | Required | Meaning |
|---|---|---|
| `symbol` | yes | Letters and digits, starting lower case; unique among all ports. The pedal must have a setter `set<Symbol>(float)` (first letter upper case), which receives the port value unchanged. |
| `display` | yes | Name shown by hosts. |
| `default` | yes | Inside the range; an integer for discrete ports. |
| `minimum`, `maximum` | yes, except for enumerations | `minimum < maximum`. |
| `unit` | no | `pc`, `hz`, `oct`, `ms`, `s`, `db`, `bpm`, `semitone`, `cent`, `m` (LV2 units). |
| `property` | no | `logarithmic` (needs `minimum > 0`), `enumeration`, `integer`, `toggled`. |
| `designation` | no | `beatsPerMinute` or `speed`: the host fills the port with the board's tempo (BPM) or the transport state (0 stopped, 1 rolling) through `lv2:designation time:beatsPerMinute` / `time:speed`. Each at most once per plugin. Measured on the Dwarf in docs/device.md. |
| `listitems` | for enumerations | Menu entries; the port takes the values 0 to n-1, `minimum`/`maximum` may be left out. |
| `hasUI` | yes | 0 or 1: whether the control is a knob on the pedal face (modgui), in JSON order; at most 6. The face width follows their number (none or one: 190 px, two to four: 230 px, five or six: 280 px; layouts in `jpgen.knob_positions()`). All controls stay reachable in mod-ui's settings and on the Dwarf. |

## Control outputs

Values the plugin reports to the host, e.g. measurements. The pedal must have a const
getter `get<Symbol>()` returning float (first letter upper case); the adapter writes its value
to the port after every `run()` and after `activate()`.

| Key | Required | Meaning |
|---|---|---|
| `symbol` | yes | As for control inputs; unique among all ports. |
| `display` | yes | Name shown by hosts. |
| `minimum`, `maximum` | yes | Range of the reported values, `minimum < maximum`; the generic host tests check that the outputs stay inside it. |
| `unit` | no | As for control inputs. |
| `property` | no | Only `integer`. |

Every control output is listed in `modgui:monitoredOutputs`: mod-ui only forwards the values of
the ports listed there to its websocket clients (`docs/test-plugins.md`).

## MIDI input

`"midi-in": true` adds one input port after the control outputs: an LV2 atom sequence port that
supports `midi:MidiEvent`, symbol `midiIn`, name "MIDI In" (the symbol is reserved), and makes
`urid:map` a required feature of the plugin. Use `"plugintype": "InstrumentPlugin"` for a
synth. The pedal then needs `void midi(const uint8_t* msg, uint32_t size) noexcept`, called from
`run()` between the audio of the frames before the event and the audio after it
(`docs/architecture.md`, the adapter contract). `plugins/probe/probe.json` is the example; it
counts the events (`docs/test-plugins.md`).

## MIDI output

`"midi-out": true` adds one output port after the MIDI input (or after the control outputs if there is none): an LV2
atom sequence port that supports `midi:MidiEvent`, symbol `midiOut`, name "MIDI Out" (reserved), and makes `urid:map` a
required feature. The pedal needs `void setMidiOutput(MidiOutput&) noexcept` and writes through it
(`lv2/PedalServices.h`, `docs/architecture.md`, the adapter contract). A plugin that only processes MIDI has empty
`audio-ports-in` and `audio-ports-out` and `void process(size_t n) noexcept`. `plugins/probe/probe.json` passes its
MIDI input on to the output. The pedal face (`--modgui`) gets a MIDI socket on the output side from the icon
template; mod-ui draws a socket only for ports the template names, so the port alone is not wireable in the web UI.

## Worker

`"worker": true` makes `work:schedule` a required feature and `work:interface` extension data of the plugin, for
work that must not run in `run()` (reading files, compiling, anything that allocates or blocks). The pedal needs
`void setWorkScheduler(WorkScheduler&) noexcept`, `void work(const void* data, uint32_t size, WorkResponder&) noexcept`
(host worker thread) and `void workResponse(const void* data, uint32_t size) noexcept` (audio thread, at the start of a
later `run()`); see `docs/architecture.md`. `plugins/probe/probe.json` sends every note on through the worker and
back.

## Presets

```json
"presets": [
  {"name": "Ship Ahoy", "values": {"mix": 100, "tempo": 105, "script": 0, "x": 5, "y": 85}}
]
```

Names are unique; every value names a control and lies in its range (integers for discrete
ports). Controls left out keep their current value when a host loads the preset. All
presets go into one `presets.ttl`, listed in `manifest.ttl`.

## What the generator writes

| File | Where | Content |
|---|---|---|
| `ports.h` | `<build>/gen/<name>/` | `jp::generated::PluginPorts`: URI, version, audio port counts, one `ControlInput` per control input (index, symbol, range, default, setter) and one `ControlOutput` per control output (index, symbol, getter), `c_midiInput` and `c_midiIndex`, `c_midiOutput` and `c_midiOutIndex`, `c_worker` for `lv2/Lv2Adapter.h` |
| `manifest.ttl` | `<build>/lv2/jaypacks-<name>.lv2/` | plugin URI, binary, presets |
| `<name>.ttl` | same | plugin description and ports |
| `presets.ttl` | same | the presets, if any |
| `modgui.ttl`, `modgui/jaypacks-<name>.html`, `.css` | same | with `JP_GRAPHICS` (`jpgen.py --modgui`): modgui description (with the monitored outputs), icon template, stylesheet |
| `modgui/face.png`, `screenshot.png`, `thumbnail.png`, `knobs/boxy/white.png`, `pedals/onoff_white.png` | same | with `JP_GRAPHICS`, by `generator/graphics.py` |

Port indices: audio inputs first, then audio outputs, then the control inputs, then the control
outputs, each in JSON order, then the MIDI input, then the MIDI output.
Files are rewritten only when their content changes; CMake re-runs the generator when the
JSON, `jpgen.py` or a template changes, and `graphics.py` also when a shared image changes.

Check a file by hand with `generator/jpgen.py plugins/<name>/<name>.json --check`.

## Adding a plugin

1. Write the DSP in the matching category of `dsp/` (header-only, C++17, namespace `jp`,
   real-time safe) with unit tests in the same category of `tests/unit/`; a port from AbacDsp gets a provenance row in
   `docs/references.md`.
2. Write the pedal `plugins/<name>/<Name>Pedal.h`: `setSampleRate(float)`, `reset()`, one
   `set<Symbol>(float)` per control input (port units, smoothing inside), one
   `get<Symbol>() const` per control output, and the `process()` that matches the audio port
   count (`docs/architecture.md`), `midi()` for a plugin with a MIDI input, `setMidiOutput()` for a MIDI output and
   the worker callbacks for a worker. Unit tests in
   `tests/unit/<Name>Pedal_test.cpp`.
3. Write `plugins/<name>/<name>.json` as described above and check it with
   `generator/jpgen.py plugins/<name>/<name>.json --check`.
4. Add the name to `JP_PLUGINS` in `plugins/CMakeLists.txt`.
5. `dev-scripts/dev-test.sh`: the generic host tests now run on the new bundle (every control
   must change the audio or a control output; block size and in-place independence; finite
   output for any control value), and `host.modgui` checks its pedal face. Plugin behaviour
   through the binary goes into `tests/host/<Name>_test.cpp` (`docs/testing.md`).
6. `tools/jp.py build-mac <name> --install` to listen in a desktop host, then
   `tools/jp.py build-dwarf <name>` and a device test `tools/device_tests/test_<name>.py`
   run by `tools/jp.py test-device <name>` (`docs/test-plugins.md`).
7. Document: `docs/plugins/<name>.md`, charts in `documentation/<Name>/` where useful,
   roadmap, README.
