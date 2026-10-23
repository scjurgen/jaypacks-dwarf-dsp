#!/usr/bin/env python3
"""Client for the MOD Dwarf: mod-ui HTTP API, mod-ui websocket and ssh.

Library for tools/jp.py and the device tests; as a script it prints a read-only status:
  modui.py status          reachability, loaded pedalboard, installed jaypacks plugins

Only calls that were verified on the device are here. Control output values arrive over the
websocket (OutputReader).

Device address: environment variable DWARF_HOST (default 192.168.51.1); ssh as root with a
key (BatchMode, no password prompts). Standard library only.
"""

import argparse
import base64
import contextlib
import io
import json
import os
import shlex
import socket
import struct
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

HOST = os.environ.get("DWARF_HOST", "192.168.51.1")
BASE = f"http://{HOST}"
SSH_OPTS = ["-o", "BatchMode=yes", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=10"]


class DeviceError(Exception):
    """The device did not answer as expected."""


def http_get(path, timeout=20):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as response:
        return response.read().decode()


def http_post(path, fields, timeout=30):
    data = urllib.parse.urlencode(fields).encode()
    with urllib.request.urlopen(BASE + path, data=data, timeout=timeout) as response:
        return response.read().decode()


def http_post_json(path, obj, timeout=30):
    request = urllib.request.Request(BASE + path, data=json.dumps(obj).encode(),
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode()


def ssh(command, timeout=60):
    result = subprocess.run(["ssh", *SSH_OPTS, f"root@{HOST}", command], capture_output=True, text=True,
                            timeout=timeout)
    if result.returncode != 0:
        raise DeviceError(f"ssh failed ({result.returncode}): {command}\n{result.stderr}")
    return result.stdout


class WebSocket:
    """Minimal RFC 6455 client for the mod-ui websocket: text frames out, frames drained in."""

    def __init__(self, host=HOST, path="/websocket", port=80):
        self.sock = socket.create_connection((host, port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                   f"Sec-WebSocket-Version: 13\r\nOrigin: http://{host}\r\n\r\n")
        self.sock.sendall(request.encode())
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise DeviceError("websocket handshake: connection closed")
            response += chunk
        head, _, self.buf = response.partition(b"\r\n\r\n")
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise DeviceError(f"websocket handshake failed: {head[:200]!r}")

    def send(self, text):
        payload = text.encode()
        n = len(payload)
        if n < 126:
            header = struct.pack("!BB", 0x81, 0x80 | n)
        elif n < 65536:
            header = struct.pack("!BBH", 0x81, 0x80 | 126, n)
        else:
            header = struct.pack("!BBQ", 0x81, 0x80 | 127, n)
        mask = os.urandom(4)
        body = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + body)

    def param_set(self, instance, symbol, value):
        """Set a control port of a plugin in the current graph, e.g. ("shipahoy", "speed", 7)."""
        self.send(f"param_set /graph/{instance}/{symbol} {value}")

    def drain(self, seconds):
        """Collect text messages for a while; echo data_ready so mod-ui keeps sending."""
        return [message for _, message in self.receive(seconds)]

    def receive(self, seconds, poll=0.2):
        """As drain(), each message with the time.monotonic() of its arrival (to the poll
        interval); returns after `seconds` with the complete frames received until then."""
        messages = []
        end = time.monotonic() + seconds
        self.sock.settimeout(min(poll, max(seconds, 0.001)))
        while True:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.buf += chunk
            except socket.timeout:
                pass
            arrival = time.monotonic()
            while len(self.buf) >= 2:
                b0, b1 = self.buf[0], self.buf[1]
                n = b1 & 0x7F
                offset = 2
                if n == 126:
                    if len(self.buf) < 4:
                        break
                    n = struct.unpack("!H", self.buf[2:4])[0]
                    offset = 4
                elif n == 127:
                    if len(self.buf) < 10:
                        break
                    n = struct.unpack("!Q", self.buf[2:10])[0]
                    offset = 10
                if len(self.buf) < offset + n:
                    break
                payload = self.buf[offset:offset + n]
                self.buf = self.buf[offset + n:]
                if b0 & 0x0F == 1:
                    message = payload.decode(errors="replace")
                    messages.append((arrival, message))
                    if message.startswith("data_ready"):
                        self.send(message)
            if time.monotonic() >= end:
                break
        return messages

    def close(self):
        self.sock.close()


def parse_output(message):
    """(instance, symbol, value) of an "output_set /graph/<instance> <symbol> <value>" message,
    None for any other message."""
    parts = message.split()
    if len(parts) != 4 or parts[0] != "output_set" or not parts[1].startswith("/graph/"):
        return None
    try:
        return parts[1][len("/graph/"):], parts[2], float(parts[3])
    except ValueError:
        return None


def parse_hw_map(message):
    """(instance, symbol, actuator) of an "hw_map /graph/<instance> <symbol> <actuator> ..."
    message (one per hardware assignment, e.g. symbol ":bypass" on "/hmi/footswitch1"), None
    for any other message."""
    parts = message.split()
    if len(parts) < 4 or parts[0] != "hw_map" or not parts[1].startswith("/graph/"):
        return None
    return parts[1][len("/graph/"):], parts[2], parts[3]


class OutputReader:
    """Latest control output values of the plugins in the graph, as mod-ui reports them over
    the websocket (only ports listed in modgui:monitoredOutputs). Keeps every raw message
    in `log` as (arrival time, text)."""

    def __init__(self, ws):
        self.ws = ws
        self.values = {}
        self.log = []

    def poll(self, seconds):
        for arrival, message in self.ws.receive(seconds):
            self.log.append((arrival, message))
            output = parse_output(message)
            if output is not None:
                instance, symbol, value = output
                self.values[(instance, symbol)] = (value, arrival)

    def value(self, instance, symbol):
        entry = self.values.get((instance, symbol))
        return None if entry is None else entry[0]

    def wait_for(self, instance, symbol, accept, timeout):
        """Poll until accept(value) is true for the output; returns the value, None on timeout."""
        end = time.monotonic() + timeout
        while True:
            value = self.value(instance, symbol)
            if value is not None and accept(value):
                return value
            remaining = end - time.monotonic()
            if remaining <= 0:
                return None
            self.poll(min(0.05, remaining))


def cpu_load(ws, seconds=4.0):
    """Average and maximum of the mod-ui "stats <cpu> <xruns>" messages, the last xrun count and
    a count of message kinds. (jack_cpu_load cannot be used from scripts: it prints nothing
    when piped and ignores SIGTERM.)"""
    values, xruns, kinds = [], None, {}
    for message in ws.drain(seconds):
        kind = message.split(" ", 1)[0]
        kinds[kind] = kinds.get(kind, 0) + 1
        if kind == "stats":
            parts = message.split()
            values.append(float(parts[1]))
            if len(parts) > 2:
                xruns = parts[2]
    average = sum(values) / len(values) if values else None
    return average, (max(values) if values else None), xruns, kinds


MIDI_SENDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "device_tests", "alsa_midi_send.py")


def jack_midi_inputs(symbol):
    """JACK MIDI input ports of the plugins in the graph whose port symbol is `symbol`, as
    "effect_<n>:<symbol>" (mod-host creates one JACK port per atom MIDI input)."""
    ports = ssh("jack_lsp").split()
    return [port for port in ports if port.startswith("effect_") and port.endswith(":" + symbol)]


def listing_has_connection(listing, source, target):
    """True if the output of jack_lsp -c has target indented below source. A port stands at the start of a line
    and its connections are indented below it; asking jack_lsp for one port would also list the ports that start
    with the same name (system:midi_capture_14 for system:midi_capture_1), so the whole listing is read."""
    port = None
    for line in listing.splitlines():
        if line[:1] in (" ", "\t"):
            if port == source and line.strip() == target:
                return True
        else:
            port = line.strip()
    return False


def jack_connected(source, target):
    """True if the JACK port source is connected to target."""
    return listing_has_connection(ssh("jack_lsp -c"), source, target)


def midi_send(messages, timeout=60):
    """Plays messages into the Dwarf's MIDI input the way a USB controller does, by running
    device_tests/alsa_midi_send.py with the device's python3 (ALSA sequencer, jackd's bridge,
    mod-midi-merger; see that file). The graph must connect "midi_merger_out" to the MIDI input of
    the plugin under test (connect()). messages: [(milliseconds, [status, data1, data2]), ...]
    sorted by time, counted from the moment the routing is in place; the timing is good to about
    a millisecond, not sample accurate. Returns the sender's one-line report."""
    schedule = json.dumps([[ms, [int(byte) for byte in message]] for ms, message in messages],
                          separators=(",", ":"))
    with open(MIDI_SENDER, encoding="ascii") as handle:
        script = handle.read()
    result = subprocess.run(["ssh", *SSH_OPTS, f"root@{HOST}", f"python3 - {shlex.quote(schedule)}"], input=script,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        raise DeviceError(f"midi_send failed ({result.returncode}): {result.stdout.strip()} {result.stderr.strip()}")
    return result.stdout.strip()


def current_pedalboard():
    """Bundle path of the loaded pedalboard, "" if none or unknown."""
    try:
        return json.loads(ssh("cat /root/data/last.json")).get("pedalboard", "")
    except (DeviceError, ValueError):
        return ""


def reset():
    """Empty the current graph (unloads the pedalboard without deleting it)."""
    return http_get("/reset")


def load_pedalboard(bundle_path):
    return http_post("/pedalboard/load_bundle/", {"bundlepath": bundle_path, "isDefault": "0"})


@contextlib.contextmanager
def preserved_pedalboard(restore=True):
    """Run a block with the graph to itself; afterwards, with restore, reset and reload the
    pedalboard that was loaded before, also when the block fails. Without restore the block's
    graph stays loaded (unsaved) for the author to look at in the web UI. Yields that board's
    path."""
    board = current_pedalboard()
    try:
        yield board
    finally:
        if restore:
            reset()
            if board:
                load_pedalboard(board)


def plugin_installed(uri):
    """True if mod-ui knows the plugin (it answers 404 for unknown URIs)."""
    try:
        http_get("/effect/get?" + urllib.parse.urlencode({"uri": uri}), timeout=30)
        return True
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return False
        raise


def add_plugin(instance, uri, x=100, y=200):
    answer = http_get(f"/effect/add/graph/{instance}?" + urllib.parse.urlencode({"uri": uri, "x": x, "y": y}))
    if answer.strip() in ("false", ""):
        raise DeviceError(f"adding {uri} as {instance} failed: {answer[:200]}")
    return answer


def remove_plugin(instance):
    return http_get(f"/effect/remove/graph/{instance}")


def connect(source, target):
    """Connect two graph ports, e.g. ("capture_1", "shipahoy/in")."""
    return http_get(f"/effect/connect/graph/{source},/graph/{target}")


def address(port, actuator, label, minimum, maximum, value, steps):
    """Assign a hardware actuator (e.g. "/hmi/knob1", "/hmi/footswitch1") to a port path
    (e.g. "/graph/shipahoy/speed" or "/graph/shipahoy/:bypass")."""
    body = {"uri": actuator, "label": label, "minimum": minimum, "maximum": maximum, "value": value,
            "steps": steps, "tempo": False, "dividers": None, "page": 0, "subpage": 0,
            "coloured": False, "momentary": 0}
    return http_post_json(f"/effect/parameter/address{port}", body)


def tar_gz(bundle_dir):
    """Bundle as tar.gz; Python's tarfile writes no macOS AppleDouble (._*) entries."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
        tar.add(str(bundle_dir), arcname=os.path.basename(str(bundle_dir)))
    return buffer.getvalue()


def sdk_install(package):
    """POST /sdk/install with the base64-encoded tar.gz in the multipart field "package".
    mod-ui refuses to replace a plugin that the loaded pedalboard uses."""
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"package\"; filename=\"package\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode()
    body += base64.b64encode(package) + f"\r\n--{boundary}--\r\n".encode()
    request = urllib.request.Request(BASE + "/sdk/install", data=body,
                                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.loads(response.read().decode())


def add_favorite(uri):
    """Marks a plugin as a favourite in mod-ui (POST /favorites/add with the field "uri"; mod-ui
    keeps the list in memory and in its favorites.json). True when added, False when it already
    was one. A reinstall keeps the mark."""
    return http_post("/favorites/add", {"uri": uri}).strip().lower() == "true"


def lv2info_name(uri):
    """The "Name:" line of lv2info on the device, "" if lilv does not find the plugin."""
    return ssh(f"lv2info {uri} 2>&1 | grep -m1 'Name:' ; true").strip()


def cmd_status():
    print(f"device {HOST}")
    try:
        http_get("/", timeout=10)
        print("  mod-ui: reachable")
    except (OSError, urllib.error.URLError) as error:
        print(f"  mod-ui: not reachable ({error})")
        return 1
    try:
        board = current_pedalboard()
        print(f"  pedalboard: {board or '(none)'}")
        bundles = ssh("ls -d /root/.lv2/jaypacks-*.lv2 2>/dev/null; true").split()
        print(f"  jaypacks bundles: {' '.join(os.path.basename(b) for b in bundles) or '(none)'}")
    except DeviceError as error:
        print(f"  ssh: {error}")
        return 1
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="read-only: reachability, pedalboard, installed jaypacks bundles")
    parser.parse_args(argv)
    return cmd_status()


if __name__ == "__main__":
    sys.exit(main())
