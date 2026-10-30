"""USB setup and Wi-Fi for the Warblet Open starter.
The setup page at warbletiot.com/flash sends the Wi-Fi networks and the board's
id over USB (CHIRP-PROV). This file stores them, joins Wi-Fi, and keeps
answering the setup page. It stores no token and no key: this is the Open level."""
import json
import machine
import network
import os
import select
import sys
import time

SLUG = "demo-open-starter"          # which demo this is, for the setup page
FW = "v1.6.0"                       # the release these files last changed in
CFG_PATH = "open_cfg.json"          # where the settings live on the board
NEW_PATH = "open_cfg.new"           # a new copy, before it takes the old one's place
TEXT_FIELDS = ("ssid", "pass", "hwid")
HOST = "http.warbletiot.com"        # main.py's: a push may name it, never another
# What this firmware can do, for the setup page: Open only, no key, plain HTTP.
CAPS = ('{"v":1,"auth":[0],"keyProvisioning":[],"otaKeyRotation":false,'
        '"otaFirmwareUpdate":false,"transports":["http"]}')
ID_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_."
MAX_NETWORKS = 8                    # the setup page sends at most this many
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
    config = settle()[0]
    if not (config.get("ssid") and config.get("hwid")):
        print("open: not set up yet -- connect this board at warbletiot.com/flash")
        while True:
            wait(1)                 # a good push saves and restarts the board
    print("open: sending as", config["hwid"])
    return config["hwid"]


def _read(path):
    """The settings in this file, or None when it is missing or not whole."""
    try:
        with open(path) as f:
            stored = json.load(f)
    except (OSError, ValueError):
        return None
    return stored if isinstance(stored, dict) else None   # a file written by hand can hold anything


def _remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


def _sync():
    """Write the file system's cache to flash, where the runtime can."""
    sync = getattr(os, "sync", None)
    if sync:
        sync()


def settle():
    """Leave the stored settings under the live name, and return (them, True),
    or (them, False) when they could only be read from the new copy. A live
    file that reads whole wins, and a new copy beside it (a save that did not
    finish) goes. Otherwise a whole new copy takes the live name now."""
    stored = _read(CFG_PATH)
    if stored is not None:
        _remove(NEW_PATH)
        return stored, True
    stored = _read(NEW_PATH)
    if stored is None:
        return {}, True
    try:
        os.rename(NEW_PATH, CFG_PATH)
    except OSError:
        return stored, False        # the board runs on the new copy for now
    return stored, True


def save(settings):
    """Store the settings, or raise OSError (store-failed) when the next start
    will read the previous ones. A new copy is written, then renamed over the
    live file. FAT's rename removes the old file first, so a failure can leave
    only the new copy: the next start reads it, so that is stored."""
    if not settle()[1]:
        raise OSError("the stored settings are only in " + NEW_PATH)  # never write over it
    try:
        with open(NEW_PATH, "w") as f:
            json.dump(settings, f)
        _sync()
    except OSError:
        _remove(NEW_PATH)
        raise
    try:
        os.rename(NEW_PATH, CFG_PATH)
    except OSError:
        try:
            os.stat(CFG_PATH)
        except OSError:
            return                  # the old file is gone: the new copy is stored
        _remove(NEW_PATH)           # the old file stands
        raise
    try:
        _sync()
    except OSError:
        pass                        # renamed: these are the stored settings either way


def fits(name, value):
    """True when a pushed text is within the contract's bounds: an ssid of 1 to
    32 bytes, a pass of up to 64, an id of up to 128 of the id characters."""
    if name == "ssid":
        return 0 < len(value.encode()) <= 32
    if name == "pass":
        return len(value.encode()) <= 64
    return len(value) <= 128 and all(c in ID_CHARS for c in value)


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
        try:
            wlan.connect(name, password)
        except OSError as e:        # a join cut short by a restart (Ctrl-D)
            print("open: connect failed:", e)
            drop(wlan)              # leaves the radio "connecting": end it
            continue
        for _ in range(48):
            if wlan.isconnected():
                return
            wait(0.25)
        drop(wlan)                  # start the next try clean
    raise OSError("no Wi-Fi network joined")


def drop(wlan):
    """End a Wi-Fi attempt. The radio can refuse even that; the next try
    goes ahead anyway."""
    try:
        wlan.disconnect()
    except OSError:
        pass


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
        print("CHIRP! v=1 hw={} fw={} key_set=0 radios=wifi t={} caps={}".format(
            config.get("hwid", ""), FW, SLUG, CAPS))
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
    # Open: no credential, no key, and no host but its own. Any field this
    # firmware does not use is refused by name (the first in A-Z order).
    refused = [k for k in push if k not in TEXT_FIELDS + ("aps", "host")
               or (k == "host" and push[k] != HOST)]
    if refused:
        if any(not 0 < len(k) <= 32 or any(c not in ID_CHARS or c == ":" for c in k)
               for k in refused):
            return "err bad-json"       # a name that cannot go on the answer line
        return "err unsupported " + min(refused)
    new = dict(config)
    for name in TEXT_FIELDS:
        if name in push:
            if not (isinstance(push[name], str) and fits(name, push[name])):
                return "err bad-json"
            new[name] = push[name]
    if "aps" in push:
        # The whole list, each entry exactly an ssid and a pass; a top-level
        # ssid or pass sent with it must be its first entry's.
        aps = push["aps"]
        if not (isinstance(aps, list) and 0 < len(aps) <= MAX_NETWORKS) or any(
                not (isinstance(ap, dict) and sorted(ap) == ["pass", "ssid"]
                     and isinstance(ap["ssid"], str) and isinstance(ap["pass"], str)
                     and fits("ssid", ap["ssid"]) and fits("pass", ap["pass"]))
                for ap in aps) or any(k in push and push[k] != aps[0][k] for k in ("ssid", "pass")):
            return "err bad-json"
        new["aps"] = aps
        new["ssid"], new["pass"] = aps[0]["ssid"], aps[0]["pass"]   # tried first
    elif "ssid" in push:
        new.pop("aps", None)        # one network named means exactly that one
    if not (new.get("ssid") and new.get("hwid")):
        return "err missing-field"
    try:
        save(new)
    except OSError:
        return "err store-failed"
    return "ok"
