"""USB setup and Wi-Fi for the Warblet Open starter.
The setup page at warbletiot.com/flash sends the Wi-Fi networks and the board's
id over USB (CHIRP-PROV). This file stores them, joins Wi-Fi, and keeps
answering the setup page. It stores no token and no key: this is the Open level."""
import json
import machine
import network
import select
import sys
import time

SLUG = "demo-open-starter"          # which demo this is, for the setup page
CFG_PATH = "open_cfg.json"          # where the settings live on the board
TEXT_FIELDS = ("ssid", "pass", "hwid")
ID_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_."
MAX_LINE = 4096                     # a setup line is never longer; more is junk

config = {}
_poller = select.poll()
_poller.register(sys.stdin, select.POLLIN)
# Read the console as bytes: a text read waits for the rest of a character,
# so one stray byte from another program could stop the board.
_console = getattr(sys.stdin, "buffer", sys.stdin)
_line = bytearray()


def start():
    """Load the settings and return the board's id. Until the setup page has
    sent a network and an id, only answer the setup page."""
    global config
    try:
        with open(CFG_PATH) as f:
            config = json.load(f)
    except (OSError, ValueError):
        config = {}
    if not isinstance(config, dict):  # a file written by hand can hold anything
        config = {}
    if not (config.get("ssid") and config.get("hwid")):
        print("open: not set up yet -- connect this board at warbletiot.com/flash")
        while True:
            wait(1)                 # a good push saves and restarts the board
    print("open: sending as", config["hwid"])
    return config["hwid"]


def networks():
    """(name, password) for each stored network, first network first."""
    pairs = [(config["ssid"], config.get("pass") or "")]
    for ap in config.get("aps") or []:
        if ap["ssid"] not in [name for name, _ in pairs]:
            pairs.append((ap["ssid"], ap.get("pass") or ""))
    return pairs


def wifi():
    """Join a stored network unless already joined. About 12 s per network."""
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return
    for name, password in networks():
        print("open: joining", name)
        wlan.connect(name, password)
        for _ in range(48):
            if wlan.isconnected():
                return
            wait(0.25)
        wlan.disconnect()           # start the next try clean
    raise OSError("no Wi-Fi network joined")


def wait(seconds):
    """Wait, answering the setup page on the USB console meanwhile. Returns on
    time even while bytes keep arriving."""
    global _line
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while True:
        left = time.ticks_diff(deadline, time.ticks_ms())
        if left <= 0:
            return
        if not _poller.poll(left):
            continue
        byte = _console.read(1)
        if isinstance(byte, str):   # a runtime with no byte console
            byte = byte.encode()
        if byte in (b"\r", b"\n"):
            _handle(bytes(_line))
            _line = bytearray()
        elif byte:
            if len(_line) >= MAX_LINE:
                _line = bytearray()
            _line += byte


def _handle(raw):
    start = raw.find(b"CHIRP")      # stray bytes in front do not spoil a command
    if start < 0:
        return
    try:
        line = raw[start:].decode().strip()
    except UnicodeError:
        return
    if line == "CHIRP?":
        print("CHIRP! v=1 hw={} radios=wifi t={}".format(config.get("hwid", ""), SLUG))
    elif line.startswith("CHIRP+"):
        answer = _apply(line[6:].strip())
        print("CHIRP= " + answer)
        if answer == "ok":
            time.sleep_ms(200)      # let the answer leave before the restart
            machine.reset()


def _apply(body):
    """Check a settings push and save it. Returns the CHIRP-PROV answer."""
    try:
        push = json.loads(body)
    except ValueError:
        return "err bad-json"
    if not isinstance(push, dict):
        return "err bad-json"
    if "claim" in push or "token" in push:
        return "err unsupported"    # Open: this firmware never sends a credential
    new = dict(config)
    for name in TEXT_FIELDS:
        if name in push:
            if not isinstance(push[name], str):
                return "err bad-json"
            new[name] = push[name]
    if "aps" in push:
        aps = push["aps"]
        if not isinstance(aps, list) or len(aps) > 8:
            return "err bad-json"
        for ap in aps:
            if not (isinstance(ap, dict) and isinstance(ap.get("ssid"), str) and ap["ssid"]
                    and isinstance(ap.get("pass", ""), str)):
                return "err bad-json"
        new["aps"] = aps
    elif "ssid" in push:
        new.pop("aps", None)        # one network named means exactly that one
    hwid = new.get("hwid", "")
    if len(hwid) > 128 or any(c not in ID_CHARS for c in hwid):
        return "err bad-json"
    if not (new.get("ssid") and hwid):
        return "err missing-field"
    try:
        with open(CFG_PATH, "w") as f:
            json.dump(new, f)
    except OSError:
        return "err store-failed"
    return "ok"
