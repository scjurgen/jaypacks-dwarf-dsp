# Documentation index

| File | Content |
|---|---|
| `architecture.md` | JSON -> generator -> adapter -> pedal, naming and URIs, versions, builds, directory map |
| `building.md` | Prerequisites, the daily loop, Mac bundles, the Dwarf build in docker, deploy |
| `testing.md` | Test layers L0 to L3, generic host tests, thresholds, adding tests |
| `device.md` | MOD Dwarf facts, ssh, the mod-ui HTTP API and websocket, logs, pedalboards |
| `test-plugins.md` | probe, siggen and meter: readback through mod-ui, the measure protocol, writing a device test |
| `caveats-and-limits.md` | Compiler, floating point, real time, mod-ui and device pitfalls |
| `plugin-json.md` | Reference of the plugin JSON format, what the generator writes, adding a plugin |
| `plugins/shipahoy.md` | shipahoy: background, how it works, techniques, controls, the "Ship Ahoy" sound, performance |
| `plugins/eigenghost.md` | eigenghost: how the reverb works, techniques, controls, using it, performance |
| `plugins/midiscript.md` | midiscript: a MIDI effect in Lua: how the timing works, controls, the shipped scripts, tests |
| `plugins/siggen.md`, `plugins/meter.md`, `plugins/probe.md` | The test plugins' ports, behaviour and techniques |
| `plugins/images/` | Pedal face snapshots (`tools/jp.py snapshot`) |
| `reaper.md` | Listening and testing in REAPER on the Mac, null tests between builds |
| `genai-development.md` | How the project is developed with an LLM: the repository as memory, the loop, gates, traps |
| `working-with-claude-code.md` | How an LLM session continues the work: rules, techniques, traps |
| `scripting.md` | The scripting core: sandbox, commands, host, hot reload, logs, API description, the user's loop (guide for an AI assistant, examples, the brief) |
| `scripting-plugins.md` | Checklist for adding scripting to a plugin (examples and guide included), with Morphexsynth as the example |
| `outlook-lua.md` | Lua scripting on the Dwarf: AbacDsp's engine, LV2 options, what was measured on the device, the decided design |
| `roadmap.md` | Step list, status, what is next, findings, risks |
| `references.md` | AbacDsp (where to look, pinned commit, provenance of ported files) and external references |
| `DOCSTYLE.md` | How header comments are written |

Algorithm charts and analysis are in `../documentation/` (same layout as AbacDsp, see its
README).
