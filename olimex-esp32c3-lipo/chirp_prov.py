"""USB provisioning and config storage.
CHIRP? reads identity; CHIRP+ validates, merges, and saves settings.
The announce reports Wi-Fi and signing support. main.py supplies the demo slug."""
import json
import machine
import select
import sys
import time

CFG_PATH = "chirp_cfg.json"

# Fields that must be strings when present. Unknown fields are kept but unused.
STRING_FIELDS = ("ssid", "pass", "token", "key", "hwid", "host")

# CHIRP-PROV caps the network list at eight; a longer push is refused, not truncated.
MAX_NETWORKS = 8

# Demo slug for the announce's t= token; main.py sets it.
SLUG = ""

# A setup line is never this long; more input than this without a newline is junk.
MAX_LINE = 4096

_poller = select.poll()
_poller.register(sys.stdin, select.POLLIN)
# Read the console as bytes. A text read waits for the rest of a character,
# so one stray byte -- from a program on the computer probing serial ports,
# say -- could stop the board for ever. A runtime without stdin.buffer falls
# back to the text stream.
_console = getattr(sys.stdin, "buffer", sys.stdin)
_buf = bytearray()  # the console line so far, as bytes


def set_slug(slug):
    """Set the demo slug for the announce."""
    global SLUG
    SLUG = slug


def load():
    """The stored config, or an empty dict when there isn't one."""
    try:
        with open(CFG_PATH) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def ready(config):
    """True when the config is complete enough to report with."""
    return bool(config.get("ssid") and config.get("token")
                and config.get("hwid"))


def networks(config):
    """Return (ssid, password) pairs in order, without repeating the first network."""
    pairs = []
    primary = config.get("ssid")
    if primary:
        pairs.append((primary, config.get("pass") or ""))
    for entry in config.get("aps") or []:
        if not isinstance(entry, dict):
            continue
        ssid = entry.get("ssid")
        if ssid and ssid not in [name for name, _ in pairs]:
            pairs.append((ssid, entry.get("pass") or ""))
    return pairs


def service(config, seconds=0):
    """Service USB setup for the requested duration; zero drains pending input.
    A successful config push saves settings and reboots."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while True:
        remaining = time.ticks_diff(deadline, time.ticks_ms())
        if _poller.poll(remaining if remaining > 0 else 0):
            byte = _console.read(1)
            if byte:
                _feed(byte, config)
                continue  # drain the rest of the line without waiting again
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            return


def _feed(byte, config):
    """One byte off the console into the line buffer. A line counts from its
    first "CHIRP", so stray bytes in front of a command do not spoil it."""
    global _buf
    if isinstance(byte, str):  # the text-stream fallback hands us characters
        byte = byte.encode()
    if byte not in (b"\r", b"\n"):  # the portal sends CRLF; a bare LF is tolerated
        if len(_buf) >= MAX_LINE:
            _buf = bytearray()  # far too long for a setup line: start again
        _buf += byte
        return
    line = bytes(_buf)
    _buf = bytearray()
    start = line.find(b"CHIRP")
    if start < 0:
        return
    try:
        line = line[start:].decode().strip()
    except UnicodeError:
        return  # not text, so not a setup line
    if line == "CHIRP?":
        print("CHIRP! v=1 hw={} radios=wifi sig=1 t={}".format(
            config.get("hwid", ""), SLUG))
    elif line.startswith("CHIRP+"):
        apply_config_push(line[6:].strip(), config)


HWID_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_."


def valid_hwid(value):
    """True when a pushed `hwid` is the platform's grammar: letters, digits and
    : - _ . only, at most 128. Empty means no id."""
    return len(value) <= 128 and all(c in HWID_CHARS for c in value)


def valid_networks(entries):
    """True when a pushed `aps` list is storable: at most MAX_NETWORKS objects,
    each with a non-empty string ssid and an optional string pass."""
    if not isinstance(entries, list) or len(entries) > MAX_NETWORKS:
        return False
    for entry in entries:
        if not isinstance(entry, dict):
            return False
        if not isinstance(entry.get("ssid"), str) or not entry.get("ssid"):
            return False
        if not isinstance(entry.get("pass", ""), str):
            return False
    return True


def apply_config_push(body, config):
    """Validate and merge a push in a separate config copy, then save and reboot.
    Rejected pushes leave the active config unchanged."""
    try:
        push = json.loads(body)
        if not isinstance(push, dict):
            raise ValueError
    except ValueError:
        print("CHIRP= err bad-json")
        return

    # One credential slot: a push carrying both a claim and a token is refused.
    if "claim" in push and "token" in push:
        print("CHIRP= err bad-json")
        return

    if "aps" in push and not valid_networks(push["aps"]):
        print("CHIRP= err bad-json")
        return

    candidate = config.copy()
    for field_name, field_value in push.items():
        if field_name == "claim":
            field_name = "token"
        if field_name in STRING_FIELDS and not isinstance(field_value, str):
            # Wrong type is bad-json by contract; a missing field is checked below.
            print("CHIRP= err bad-json")
            return
        if field_name == "hwid" and not valid_hwid(field_value):
            # It goes into the announce line, a topic and a URL.
            print("CHIRP= err bad-json")
            return
        candidate[field_name] = field_value

    # A top-level ssid without aps replaces the stored network list.
    if "ssid" in push and "aps" not in push:
        candidate.pop("aps", None)

    if not ready(candidate):
        print("CHIRP= err missing-field")
        return

    try:
        with open(CFG_PATH, "w") as f:
            json.dump(candidate, f)
    except OSError:
        print("CHIRP= err store-failed")
        return

    # The live config dict is left alone: the reset below reloads from the file.
    print("CHIRP= ok")
    time.sleep_ms(200)  # let the ok leave the wire before the reset
    machine.reset()
