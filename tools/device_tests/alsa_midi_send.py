#!/usr/bin/env python3
"""Runs ON THE DWARF (Python 3.4, no third-party modules): plays a MIDI schedule into the
ALSA sequencer, the way a USB MIDI controller does. tools/modui.py (midi_send) feeds this
file to the device's python3 over ssh; it is not run on the Mac.

jackd (started with -X seq) bridges the ALSA sequencer to JACK: whatever arrives at the
"Midi Through" port (client 14) comes out of the JACK port system:midi_capture_1, inside the
JACK server, so the path is the one of a real controller (an external JACK client that writes
into mod-host's clients directly loses about half of its events, because JACK2 runs in
asynchronous mode). For the length of the run this script connects system:midi_capture_1 to
mod-midi-merger:in, the connection mod-ui makes for an enabled MIDI device, and removes it again (a
controller that is plugged in appears as another JACK port, system:midi_capture_14 for a LinnStrument).

Usage on the device:
  python3 - SCHEDULE_JSON
SCHEDULE_JSON: list of [milliseconds, [status, data1, data2]] sorted by time, counted from the
moment the routing is in place (bytes beyond the length of the message, which the status byte
gives, are ignored, so a channel pressure message may carry a third 0). The events
are sent when their time comes, with a timing error of about a millisecond (no queue is used),
and the JACK bridge stamps them with the next period: this is not sample accurate. Prints
"sent N".

libasound is used through ctypes: the device has no python-alsa and no aplaymidi.
"""

import ctypes
import json
import subprocess
import sys
import time

SEQ_OPEN_OUTPUT = 1
PORT_CAP_READ = 1 << 0
PORT_CAP_SUBS_READ = 1 << 5
PORT_TYPE_MIDI_GENERIC = 1 << 1
PORT_TYPE_APPLICATION = 1 << 20
ADDRESS_SUBSCRIBERS = 254
ADDRESS_UNKNOWN = 253
QUEUE_DIRECT = 253
EVENT_NONE = 255  # snd_midi_event_encode() leaves this type while a message is incomplete
MIDI_THROUGH = (14, 0)
CAPTURE = "system:midi_capture_1"
MERGER = "mod-midi-merger:in"


class Address(ctypes.Structure):
    _fields_ = [("client", ctypes.c_ubyte), ("port", ctypes.c_ubyte)]


class Event(ctypes.Structure):
    """snd_seq_event_t of alsa/seq_event.h (28 bytes); the time and the data are not used here."""
    _fields_ = [("type", ctypes.c_ubyte), ("flags", ctypes.c_ubyte), ("tag", ctypes.c_ubyte),
                ("queue", ctypes.c_ubyte), ("time", ctypes.c_ubyte * 8), ("source", Address),
                ("dest", Address), ("data", ctypes.c_ubyte * 12)]


def message_length(status):
    """Bytes of the MIDI message that starts with this status byte (system exclusive is not sent)."""
    if status >= 0xF8:
        return 1
    if status in (0xF1, 0xF3):
        return 2
    if status == 0xF2:
        return 3
    return 2 if status in range(0xC0, 0xE0) else 3


def jack_connected():
    """True if CAPTURE is connected to MERGER. jack_lsp -c lists a port at the start of a line and its
    connections indented below it; its argument is a prefix, so it also lists system:midi_capture_14 (a USB
    controller) when asked for system:midi_capture_1: only the lines below CAPTURE count."""
    listing = subprocess.check_output(["jack_lsp", "-c"]).decode("ascii", "replace").splitlines()
    port = None
    for line in listing:
        if line[:1] in (" ", "\t"):
            if port == CAPTURE and line.strip() == MERGER:
                return True
        else:
            port = line.strip()
    return False


def main(argv):
    schedule = json.loads(argv[1])
    asound = ctypes.CDLL("libasound.so.2")
    seq = ctypes.c_void_p()
    if asound.snd_seq_open(ctypes.byref(seq), b"default", SEQ_OPEN_OUTPUT, 0) < 0:
        print("cannot open the ALSA sequencer")
        return 2
    asound.snd_seq_set_client_name(seq, b"jp-midi-send")
    port = asound.snd_seq_create_simple_port(seq, b"out", PORT_CAP_READ | PORT_CAP_SUBS_READ,
                                             PORT_TYPE_MIDI_GENERIC | PORT_TYPE_APPLICATION)
    if port < 0 or asound.snd_seq_connect_to(seq, port, MIDI_THROUGH[0], MIDI_THROUGH[1]) < 0:
        print("cannot connect to Midi Through")
        return 2
    coder = ctypes.c_void_p()
    asound.snd_midi_event_new(256, ctypes.byref(coder))
    asound.snd_midi_event_init(coder)

    added = not jack_connected()
    if added:
        subprocess.check_call(["jack_connect", CAPTURE, MERGER])
    sent = 0
    failed = 0
    try:
        time.sleep(0.3)  # the routing is in place before the schedule starts
        start = time.time()
        for milliseconds, message in schedule:
            wait = start + milliseconds / 1000.0 - time.time()
            if wait > 0:
                time.sleep(wait)
            length = message_length(message[0])
            data = (ctypes.c_ubyte * length)(*(list(message) + [0, 0])[:length])
            event = Event()
            consumed = asound.snd_midi_event_encode(coder, data, length, ctypes.byref(event))
            if consumed <= 0 or event.type == EVENT_NONE:
                failed += 1
                continue
            event.source.port = port
            event.dest.client = ADDRESS_SUBSCRIBERS
            event.dest.port = ADDRESS_UNKNOWN
            event.queue = QUEUE_DIRECT
            if asound.snd_seq_event_output_direct(seq, ctypes.byref(event)) < 0:
                failed += 1
            else:
                sent += 1
        time.sleep(0.3)
    finally:
        if added:
            subprocess.call(["jack_disconnect", CAPTURE, MERGER])
        asound.snd_seq_close(seq)
    print("sent %d failed %d" % (sent, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
