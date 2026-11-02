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
LINE_IDLE_MS = 1500                 # a setup line is given up once the console is seen quiet this long
LINE_LATE_MS = 70000                # wait() reads a setup line on past its time at most this long

config = {}
_poller = select.poll()
_poller.register(sys.stdin, select.POLLIN)
# Read the console as bytes: a text read waits for the rest of a character,
# so one stray byte from another program could stop the board.
_console = getattr(sys.stdin, "buffer", sys.stdin)
_line = bytearray()   # the console line so far: from its "CHIRP", once that has come
_marked = False       # "CHIRP" has come on this line
_skip = False         # the rest of a line that was too long: dropped to its end
_quiet = None         # ticks_ms() since which every look at the console found nothing


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


# The store's promise: after one failed operation in a save, or a power cut
# at any point, and any number of later saves, the board still holds a
# complete copy of its settings, and the answer it gave is what the next start
# reads. The one exception: when the flash cannot confirm a store (os.sync()
# fails), the answer is store-failed and the board cannot know which copy the
# next start reads; another push settles it. Two failures inside one save are
# not covered.
ENOENT = 2                          # "no such file": the only error that means a file is not there


def ready(settings):
    """True when the settings can send: a network and an id."""
    return bool(settings.get("ssid") and settings.get("hwid"))


def _read(path):
    """The settings in this file, or None when there is no file or it does not
    parse as settings (a file written by hand can hold anything). An error
    reading a file that is there raises OSError: it may be good."""
    try:
        with open(path) as f:
            text = f.read()
    except OSError as e:
        if e.args[0] == ENOENT:
            return None
        raise
    try:
        stored = json.loads(text)
    except ValueError:
        return None
    return stored if isinstance(stored, dict) else None


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
    or (them, False) when the new copy must be kept as it is. A ready() live
    file wins, and a new copy beside it (a save that did not finish) goes.
    Otherwise a ready() new copy takes the live name, synced before that name
    is written again. A live file that cannot be read may be good: the board
    runs on the new copy for now, and nothing is replaced or removed."""
    try:
        live = _read(CFG_PATH)
    except OSError:
        try:
            new = _read(NEW_PATH)
        except OSError:
            new = None
        return (new if new and ready(new) else {}), False
    if live and ready(live):
        _remove(NEW_PATH)
        return live, True
    try:
        new = _read(NEW_PATH)
    except OSError:
        return live or {}, False    # the new copy may be the good one: keep it
    if not (new and ready(new)):
        return live or {}, True
    try:
        os.rename(NEW_PATH, CFG_PATH)
        _sync()
    except OSError:
        return new, False           # the board runs on the new copy for now
    return new, True


def save(settings):
    """Store the settings, or raise OSError (store-failed) when the next start
    will read the previous ones, or the flash cannot confirm the store. A new
    copy is written and synced, then renamed over the live file. FAT's rename
    removes the old file first, so a failure can leave only the new copy: the
    next start reads it, so that is stored."""
    if not settle()[1]:
        raise OSError("the stored settings cannot be replaced safely now")
    try:
        with open(NEW_PATH, "w") as f:
            json.dump(settings, f)
        _sync()
    except OSError:
        _remove(NEW_PATH)
        _sync()                     # the removal on flash before the answer
        raise
    try:
        os.rename(NEW_PATH, CFG_PATH)
    except OSError:
        try:
            os.stat(CFG_PATH)
        except OSError as e:
            if e.args[0] != ENOENT:
                raise               # no telling which copy is there, so both are kept
            _sync()                 # FAT removed the old file: the new copy is stored
            return
        _remove(NEW_PATH)           # the old file stands
        _sync()                     # and the candidate's removal is on flash
        raise
    _sync()


def fits(name, value):
    """True when a pushed text is within the contract's bounds: an ssid of 1 to
    32 bytes and a pass of up to 64, neither holding a character below U+0020
    or U+007F, and an id of up to 128 of the id characters."""
    if name in ("ssid", "pass"):
        if any(c < " " or c == "\x7f" for c in value):
            return False
    if name == "ssid":
        return 0 < len(value.encode()) <= 32
    if name == "pass":
        return len(value.encode()) <= 64
    return len(value) <= 128 and all(c in ID_CHARS for c in value)


def networks():
    """(name, password) for each stored network, first network first, each
    pair once: one name with two passwords is two networks to try."""
    pairs = [(config["ssid"], config.get("pass") or "")]
    for ap in config.get("aps") or []:
        if (ap["ssid"], ap.get("pass") or "") not in pairs:
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
    time, unless a setup line is arriving: that is read on to its end, for at
    most LINE_LATE_MS more, so the board's other work never comes between its
    pieces. Junk that keeps coming does not hold it: that is no setup line.

    A setup line is given up wherever it is, over the limit or not, once the
    console has been seen quiet for LINE_IDLE_MS (CHIRP-PROV v1.8): looks that
    found nothing, that far apart. Bytes that came while the board was busy
    would still be waiting, so a quiet console seen before and after means no
    byte came between. Bytes found waiting join the line that was arriving:
    nothing shows they paused, so a good push is never thrown away."""
    global _line, _marked, _skip, _quiet
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while True:
        now = time.ticks_ms()
        left = time.ticks_diff(deadline, now)
        arriving = _marked or _skip         # a setup line, or one being thrown away
        if not arriving and left <= 0:
            return
        if arriving and left + LINE_LATE_MS <= 0:
            # Still coming LINE_LATE_MS past the time: the rest of it is
            # dropped through its line ending, by the waits after this one.
            _line, _marked, _skip = bytearray(), False, True
            return
        ms = left
        if arriving:
            seen = 0 if _quiet is None else time.ticks_diff(now, _quiet)
            ms = min(LINE_IDLE_MS - seen, left + LINE_LATE_MS)
        byte = _console.read(1) if _poller.poll(max(ms, 0)) else None
        if byte:
            if isinstance(byte, str):   # a runtime with no byte console
                byte = byte.encode()
            _quiet = None
            _take(byte)
            continue
        if _quiet is None:
            _quiet = now                # nothing waited, nothing came: quiet from here
        # The look ended when the poll's wait did. Time the board spent after
        # it, on anything else, is not quiet it saw. The wait counts whole: in
        # MicroPython 1.28.0, poll returns empty only once its timeout has run
        # out (EINTR loops, anything else raises: extmod/modselect.c), and
        # sys.stdin.buffer.read(n) takes each byte with mp_hal_stdin_rx_chr()
        # and returns n bytes, never nothing (shared/runtime/sys_stdio_mphal.c).
        if arriving and time.ticks_diff(now, _quiet) + max(ms, 0) >= LINE_IDLE_MS:
            _line, _marked, _skip = bytearray(), False, False   # seen stopped: given up


def _take(byte):
    """One console byte into the line. A line counts from its first "CHIRP":
    stray bytes in front are dropped and do not spoil a command. A line longer
    than MAX_LINE from there is thrown away whole, through its line ending, so
    no part of it is read as a command."""
    global _line, _marked, _skip
    if byte in (b"\r", b"\n"):
        if _marked and not _skip:
            _handle(bytes(_line))
        _line, _marked, _skip = bytearray(), False, False
    elif byte and not _skip:
        if len(_line) >= MAX_LINE:
            _line, _marked, _skip = bytearray(), False, True   # far too long
            return
        if not _marked and len(_line) >= 64:
            _line = _line[-4:]      # nothing before "CHIRP" counts, so junk is not kept
        _line += byte
        if not _marked and _line[-5:] == b"CHIRP":
            _line, _marked = bytearray(b"CHIRP"), True      # the line starts here


def _handle(raw):
    try:
        line = raw.decode().strip()
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


def _has_nul(value):
    """True when a name or a text anywhere in the parsed push holds a NUL."""
    if isinstance(value, str):
        return "\x00" in value
    if isinstance(value, dict):
        return any(_has_nul(k) or _has_nul(v) for k, v in value.items())
    if isinstance(value, list):
        return any(_has_nul(x) for x in value)
    return False


def _apply(body):
    """Check a settings push and save it. Returns the CHIRP-PROV answer. A NUL,
    raw or sent as \\u0000, is bad-json wherever it is."""
    if "\x00" in body:
        return "err bad-json"
    try:
        push = json.loads(body)
    except ValueError:
        return "err bad-json"
    if not isinstance(push, dict) or _has_nul(push):
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
    elif "pass" in push and new.get("aps"):
        first = dict(new["aps"][0])  # a pass alone is the first network's, in the list too
        first["pass"] = push["pass"]
        new["aps"] = [first] + new["aps"][1:]
    if not (new.get("ssid") and new.get("hwid")):
        return "err missing-field"
    try:
        save(new)
    except OSError:
        return "err store-failed"
    return "ok"
