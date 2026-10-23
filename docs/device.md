# The MOD Dwarf: facts and control

What a session needs to know about the device. Facts marked "earlier" were verified on the
device before this project started (2026-10-14); the others were measured in this project.
While mod-ui runs, the device is controlled only through mod-ui (its HTTP API and
websocket), never through mod-host's sockets, which would desynchronise the web UI.

## Access

- Address `192.168.51.1` (USB network), overridable with `DWARF_HOST`.
- ssh as `root` with your ssh key installed on the device (password `mod` as fallback). Scripts use
  `-o BatchMode=yes`. sshd has no SFTP subsystem: use `scp -O` or
  `ssh root@192.168.51.1 'cat file' > file` (verified earlier).
- mod-ui (web UI and HTTP API) on port 80; `tools/modui.py status` prints reachability, the
  loaded pedalboard and the installed jaypacks bundles (read-only).
- Back up `/data` before system changes (copy it over ssh); keep the backup outside the
  repository.

## Platform (verified earlier)

| Item | Value |
|---|---|
| CPU, RAM | 4 x Cortex-A35, 993 MB, no swap |
| Kernel | 6.1.15-rt7-moddwarf, PREEMPT_RT |
| Firmware | MOD OS v1.13.5 |
| Audio | `jackd` at 48 kHz, 128 frames, 2 periods, RT priority 80: one `run()` is 2.67 ms |
| Plugin host | `mod-host`, an internal JACK client inside `jackd`; command sockets on localhost 5555/5556 only |
| LV2 path | `/root/.lv2:/usr/lib/lv2`; user bundles go to `/root/.lv2` |
| Services | `jack2.service` (jackd and mod-host), `mod-ui.service`, `mod-system-control.service` |
| C and C++ runtime | glibc 2.27; libstdc++ 6.0.28 (up to GLIBCXX 3.4.28, CXXABI 1.3.12) |
| Tools | `jack_lsp`, `jack_connect`, `jack_capture`, `jack_midi_dump`, `jack_midiseq`, `lv2ls`, `lv2info`, `journalctl`, `python3` (3.4: no f-strings), `libasound.so.2` and `libjack.so.0` (loadable with ctypes); no `ldd`, `curl`, `gdb`, `strace`, `perf`, `aplaymidi`, `amidi` |
| Clock | not synced (earlier: 2024-10-07; on 2026-10-15 the journal showed 2 January), so log times are unreliable |
| Hardware | knobs `/hmi/knob1..3` (201 steps), footswitches `/hmi/footswitch1` (B) and `/hmi/footswitch2` (C), 8 addressing pages |

The audio channels are swapped between the pedalboard's `capture_1/2` / `playback_1/2` and
the hardware ports (`"swapped_audio_channels": true` in `/etc/mod-hardware-descriptor.json`,
verified earlier); measure plugin ports, not `system:*`.

## mod-ui HTTP API (what `tools/modui.py` uses)

| Call | Purpose |
|---|---|
| `GET /reset` | Empty the graph (the loaded pedalboard stays on disk) |
| `POST /pedalboard/load_bundle/` (`bundlepath`, `isDefault`) | Load a pedalboard |
| `GET /effect/get?uri=<uri>` | 404 when the plugin is not installed |
| `GET /effect/add/graph/<instance>?uri=&x=&y=` | Add a plugin instance |
| `GET /effect/connect/graph/<a>,/graph/<b>` | Connect ports, e.g. `siggen/out` to `meter/signal`, or `capture_1` |
| `GET /effect/remove/graph/<instance>` | Remove an instance |
| `POST /effect/parameter/address/graph/<instance>/<symbol>` (JSON) | Assign an actuator (`:bypass` for bypass) |
| `POST /sdk/install` (multipart field `package`, base64 tar.gz) | Install bundles; refused for plugins the loaded graph uses |
| `POST /favorites/add` (field `uri`) | Mark a plugin as a favourite (`true`; `false` if it already is one); kept in mod-ui's `favorites.json`, a reinstall keeps it. `POST /favorites/remove` undoes it |

`/root/data/last.json` names the loaded pedalboard (`modui.current_pedalboard()`).

## mod-ui websocket (`ws://192.168.51.1/websocket`)

| Message | Direction | Meaning |
|---|---|---|
| `param_set /graph/<instance>/<symbol> <value>` | to mod-ui | Set a control input; symbol `:bypass` switches bypass (verified in step 10: bypass on gives the input bit for bit) |
| `data_ready <n>` | both | mod-ui stops sending output values unless the client echoes it (verified earlier; `modui.WebSocket` echoes) |
| `output_set /graph/<instance> <symbol> <value>` | from mod-ui | A control output listed in `modgui:monitoredOutputs`; six decimals, about every 12.5 ms, only on change (step 9) |
| `stats <cpu %> <xruns>` | from mod-ui | About once per second in recent runs; the xrun count is cumulative |
| `hw_map /graph/<instance> <symbol> <actuator> ...` | from mod-ui | One per hardware assignment; a new client gets all of them in the state replay |
| `add`, `connect`, `param_set`, `loading_start/end`, `size`, `transport`, `truebypass`, `sys_stats`, `ping` | from mod-ui | State replay on connect and live changes |

Round trip `param_set` to the changed output: about 23 ms (step 9). `jack_cpu_load` cannot
be used from scripts (verified earlier); the `stats` messages are the CPU source.

## MIDI (measured 2026-10-16 with the probe plugin)

- **The graph.** `jackd` runs with `-X seq`: an ALSA sequencer client `jack_midi` (client 128,
  port `port`) is bridged to the JACK ports `system:midi_capture_1` and `system:midi_playback_1`
  and subscribed to the kernel's `Midi Through` (client 14, port 0). A USB MIDI controller
  appears there. `ttymidi:MIDI_in` is the DIN input. `mod-midi-merger:in` collects the enabled
  inputs, `mod-midi-merger:out` is what mod-ui calls "All MIDI In" (`/graph/midi_merger_out`),
  `mod-midi-broadcaster` the MIDI output. All of these are internal clients of `jackd`
  (`/etc/jack-internal-session.conf`).
- **A controller changes the numbers.** A LinnStrument plugged into the Dwarf (2026-10-17) is the ALSA client 20 and
  the JACK port `system:midi_capture_14` (and `system:midi_playback_2`); `Midi Through` stays `system:midi_capture_1`.
  mod-ui connects the controller to `mod-midi-merger:in` by itself, so anything played on it reaches every plugin that
  the merger feeds, also while a test runs. `jack_lsp -c <port>` takes a prefix: asking for `system:midi_capture_1`
  also lists `system:midi_capture_14` and its connections, which made the test sender believe it was already
  connected to the merger (it sent nothing: all 46 checks of the first morphexsynth device run saw silence).
  `modui.jack_connected()` reads the whole listing and counts only the lines below the port.
- **A plugin's MIDI input** (an LV2 atom port that supports `midi:MidiEvent`) becomes the JACK
  port `effect_<N>:<symbol>`. mod-ui lists it under `ports.midi.input`. mod-host maps
  `midi:MidiEvent` with the `urid:map` it gives the plugin, so the adapter's id matches the
  events (21 in the first run). The events arrive as 1 to 3 byte atom events with a frame
  offset in the period.
- **Wiring.** The plugin only gets MIDI when the graph connects the merger to it:
  `modui.connect("midi_merger_out", "<instance>/<symbol>")`, as the web UI does for a MIDI
  cable. Events that another JACK client wrote straight into the plugin's port did not arrive
  (Python/ctypes client, then a C client) until that connection existed.
- **Injecting MIDI for tests.** Use the ALSA sequencer, not JACK: an external JACK client that
  writes into `mod-midi-merger:in` lost about half of its events (8 of 16, 36 of 60, 256 of 500),
  presumably because JACK2 runs asynchronously and the external client clears its buffer in
  the same period in which an internal client reads it. Through the sequencer
  (`Midi Through`, the bridge, `system:midi_capture_1`) nothing was lost: 16 of 16, 60 of 60,
  500 of 500 at 1 ms spacing, all 16 channels intact. `tools/device_tests/alsa_midi_send.py` does
  this on the device (it connects `system:midi_capture_1` to the merger for the run, as mod-ui
  does for an enabled MIDI device, and removes the connection again; `modui.midi_send()` pipes
  it through ssh). The bridge stamps events with the next period, so there is no sample
  accuracy and the timing is about a millisecond.
- **Bypass** (read in mod-host's `effects.c`, not tested on the device): when a plugin is
  bypassed, mod-host sends all-notes-off (CC 123) and all-sound-off (CC 120) on all 16 channels
  to its MIDI input, so an instrument must handle them.
- Tried and abandoned: a Python client with ctypes (libjack) as the sender, and a read-only
  `param_get` on mod-host's command socket (no answer). Control stays with mod-ui.

## Worker, script directory, MIDI out and tempo (measured 2026-10-17, throwaway probe plugin)

Measured for the Lua scripting plan (`docs/roadmap.md`, "Lua scripting" step 1) with a hand-written LV2 plugin that
was not committed: a worker, the Lua 5.4 library, a MIDI input and output and the tempo ports. Numbers are from the
Dwarf at 128 frames, 48 kHz (one period 2.67 ms).

- **LV2 worker.** `mod-host.so` implements `work:schedule` and `work:interface` (also the state interface,
  `patch:Set` and `time:Position`). `schedule_work()` from `run()` wakes the worker thread after 58 us on average
  (maximum 121 us on an idle graph; with 12 notes of Morphexsynth sounding, 150 us on average and 1.29 ms at most).
  A response reaches `work_response()` at the start of the next `run()`, never in the same one: the round trip is
  one period (mean 2.69 ms, 2.97 ms at most in 664 jobs under load, one outlier of 5.45 ms among about 250 on the
  idle graph). The worker thread is `SCHED_OTHER` at nice -19, the audio thread `SCHED_FIFO` priority 75.
- **Limits of the queues.** Both rings are small. Jobs of 2 kB passed (one per 50 ms), jobs of 8 kB and 16 kB were
  refused (`schedule_work` returned an error). 512 jobs of 24 bytes scheduled in one `run()` were all accepted, but
  of 128 or 512 jobs that the worker finished within one period only 79 responses arrived: `respond()` returns an
  error when the response ring is full (about 4 kB), and the response is lost. Bulk data between the threads
  therefore goes through the plugin's own lock-free queues; the worker is the thread and the wake-up, with at most a
  few small jobs and responses per block.
- **Files.** The plugin process (root) can read and write below `/data` (the ext4 partition `mmcblk0p5`, which also
  holds `/root` and `/data/user-files`; `/root/data` is `MOD_DATA_DIR`) and read its bundle. `stat` of a script
  takes 30 to 57 us, reading 1.25 kB 77 to 160 us. mod-ui offers user files only for a fixed list of types
  (`/data/user-files/<Type>`: audio loops, recordings, samples, tracks, cabinet and reverb IRs, MIDI clips and songs,
  Hydrogen drum kits, SF2, SFZ, NAM and Aida DSP models) and has no route to upload other files, so scripts reach
  the device by `scp -O` or `ssh`. The persistence of `/data` over a MOD OS update or a reboot was not tested.
- **String parameters.** The Notes plugin (installed) states a limit of about 450 characters for its text; which layer
  sets it was not tested. A factory patch script is 1.0 to 2.3 kB.
- **Lua 5.4 on the A35** (compiled with GCC 9.4, `-O2`, as C; the shared object with all standard libraries is 307
  kB): a new state with the base, math, table and string libraries 0.3 to 0.5 ms, compiling a 1.25 kB patch script
  0.6 to 0.9 ms, running its `OnStart` 0.2 to 0.6 ms (26 kB of Lua memory at the peak), one call of a small
  `OnNoteOn` 1.5 to 1.8 us. Speed: 15 million VM instructions per second with a count hook, 31 million without, so
  AbacDsp's budget of 2,000,000 instructions per handler is 87 ms. A script that grows a string without bound
  stopped at a 1 MB allocator limit after 4 ms ("not enough memory"), an endless loop at the instruction budget.
- **MIDI out.** An atom output port with `midi:MidiEvent` becomes the JACK port `effect_<N>:midiOut`; the output
  sequence has a capacity of 512 kB. `/effect/connect/graph/<plugin>/midiOut,/graph/<other>/midiIn` delivers to
  another plugin: 67 of 67 messages of a 16-channel burst arrived, plus the plugin's own notes, with frame offsets
  intact. `midi_broadcaster_in` (mod-ui's name for the MIDI output of the Dwarf) as the target delivered notes to
  `mod-midi-broadcaster:out` with the frames the plugin wrote (a note off 127 samples after its note on arrived 127
  samples later). Through the adapter's MIDI output (`lv2/PedalServices.h`) the same holds for the probe plugin: a
  chain of two probes passed 16 note ons on 16 channels, 500 messages and 200 note ons one millisecond apart
  without loss (`tools/device_tests/midi_check.py`).
- **Tempo.** A control input with the designation `time:beatsPerMinute` (and `time:speed` for the transport state)
  is set by mod-ui from the pedalboard's tempo; a websocket message `transport-bpm 90` (or `transport-rolling 1`)
  from a client changed the port within the next block. An atom input that supports `time:Position` receives a
  position object (`time:beatsPerMinute`, `time:speed`, further properties) at every block while the transport
  rolls (1213 atoms in 3 s); while stopped there is one at the start and none after that, a tempo change then
  updates only the port (the atom still said 120 when the port said 90). The default is 120 BPM, stopped.

## Scripts on the device

Plugins with Lua scripts (`docs/scripting.md`) read the user's scripts from `/data/jaypacks/scripts/<plugin>/` (on the
partition `/data`, mounted at `/data` and also under `/root`; writable for the plugin, checked 2026-10-17) and write
`<name>.log` there. The folder is created by the plugin on its first log write, or with `mkdir -p`. A file arrives with
`scp -O` or `ssh root@192.168.51.1 'cat > ...'` (sshd has no SFTP) or `tools/jp.py push-script`; the plugin sees it within
half a second (it checks the modification time twice a second) and a plugin install replaces only its bundle in
`/root/.lv2`. Measured with Morphexsynth: a changed file is reloaded within 1.2 s and the sound is that of the new
file (+12.000 dB for a script set 12 dB louder), a broken file leaves the previous sound with status 3 and a log with
the line of the error, a removed file gives status 2. MIDI Script (2026-10-18): the same loop; `transport-bpm` over the websocket changes the designated tempo port and
`transport-rolling` the `time:speed` port, both within a block. Not tested: that `/data` survives a MOD OS update.

## Performance measurement

`tools/jp.py perf [name...]` deploys siggen and the plugins and runs
`tools/device_tests/perf.py`: per plugin and cycle (3 by default) the mean of mod-ui's
`stats` CPU figure over 6 s without the plugin, then with it at the defaults and at every
preset that differs from them; the cost is the difference within the cycle, in percentage
points, reported as median and range. Memory is what the plugin allocates, counted on the
Mac by its memory probe (`docs/testing.md`, "Memory"), plus the size of the device binary. The source is siggen with white noise at
-12 dBFS on every audio input. Results go to `plugins/<name>/performance.json` and, through
`tools/performance.py`, to the tables in `README.md` and `docs/plugins/<name>.md`.

Every Dwarf has the same hardware and firmware, so the numbers hold for all of them until a
plugin's DSP or the firmware changes; re-measure then. What the numbers mean (first run,
2026-10-15):

- The CPU figure is what mod-ui shows, not the time spent in `run()`. It moved by up to 0.9
  points between cycles, and it does not add up like a time: shipahoy measured 0.1 to 0.4
  points here, but 2.2 points in the device integration test, where it joined a graph with
  siggen and the meter.
- probe, which only copies the audio, costs 8.4 points (16 % with probe alone in an earlier
  run). Its `seconds` output changes every block. The likely cause is the host's work for
  monitored control outputs that change, not the plugin's DSP (not proven).
- Memory cannot be measured on the device. mod-host runs inside jackd as an internal client
  (`-C /etc/jack-internal-session.conf`; jackd is the only process), so the plugins' memory
  is jackd's, but adding eigenghost (1648 kB of buffers, all written when they are
  allocated) grew jackd by `VmData` +4 kB, `VmRSS` +20 kB and `RssAnon` +8 kB, the same as
  jaystwin (0.9 kB), and still the same after 10 s of processing (2026-10-16). The memory is
  not requested from the system: glibc's allocator serves it from space freed earlier in
  jackd's heap (after a large block is freed, glibc raises its mmap threshold and serves later
  large requests from the heap; the device tests add and remove plugins all the time), so
  any reading of jackd depends on what ran before. The tables therefore show the memory
  probe's count.
- A cross-built benchmark that times `run()` on the device (cycles per sample) would measure
  the DSP alone; it is listed in `docs/roadmap.md` ("Next").

## Logs

- `journalctl -u jack2`: jackd and mod-host, including plugin crashes. Device tests take a
  cursor at the start (`--show-cursor`) and read `--after-cursor`, because the clock is wrong.
- `journalctl -u mod-ui`: web server and install errors.
- `lv2info <uri>`: whether lilv parses an installed plugin (`jp.py deploy` checks the
  `Name:` line).

## Pedalboards and graphs

- A pedalboard is a bundle `/root/.pedalboards/<name>.pedalboard/` with the graph TTL,
  `addressings.json`, snapshots and screenshots (verified earlier). Saving through the API
  (`POST /pedalboard/save`) gives a consistent bundle; saving on the device once left a stale
  file.
- The device tests build their graph with `/reset`, `/effect/add` and `/effect/connect`,
  steer with `param_set` and leave the (unsaved) graph loaded so the author can look at it in
  the web UI; `--restore` reloads the pedalboard loaded before.
- Each instance is a JACK client `effect_<N>` with one port per LV2 symbol; plugins run in
  every cycle whether connected or not.
- Hardware assignments made through the API are live until the pedalboard is saved.

## Measured in this project

| Quantity | Value | Source |
|---|---|---|
| mod-ui CPU figure, siggen and meter | 10 to 13 % (varies 10 to 16 % between runs) | steps 9, 10 |
| shipahoy added (Ship Ahoy settings) | 2 to 3 points | step 10 |
| Output update interval / round trip | 12.5 ms / 23 ms | step 9 |
| xrun at a plugin add | once in about 45 adds; none in 40 add/remove cycles | step 10 |
