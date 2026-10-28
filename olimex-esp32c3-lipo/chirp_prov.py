"""USB provisioning, config storage and the Wi-Fi join.
CHIRP? reads identity; CHIRP+ validates, merges, and saves settings.
The announce reports Wi-Fi and signing support. main.py supplies the demo slug."""
import json
import machine
import network
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

WIFI_TIMEOUT_S = 12  # per-attempt association wait
PROV_PASS_S = 0.5    # USB setup time before each association attempt

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


def connect_wifi(config):
    """Try the stored networks in order. True once we have an address.
    Called before every reading, so the ladder reruns by itself when the link
    drops. USB setup is answered before each attempt, so a board that cannot
    join is still set up."""
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return True

    for ssid, password in networks(config):
        service(config, PROV_PASS_S)
        if _join(wlan, ssid, password):
            return True

    print("chirp: no stored network joined -- will retry")
    return False


def _join(wlan, ssid, password):
    """One attempt at one network. True once we have an address."""
    print("chirp: joining", ssid)
    try:
        wlan.connect(ssid, password)
    except OSError as e:
        print("chirp: connect failed:", e)
        _drop(wlan)  # an attempt left "connecting" (by a soft restart in the
        return False  # middle of one) blocks every next one

    deadline = time.ticks_add(time.ticks_ms(), WIFI_TIMEOUT_S * 1000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        if wlan.isconnected():
            print("chirp: wifi up", wlan.ifconfig()[0])
            return True
        time.sleep_ms(250)

    _drop(wlan)  # the half-open attempt, so the next try starts clean
    print("chirp: no join to", ssid, "within", WIFI_TIMEOUT_S, "s")
    return False


def _drop(wlan):
    """End a Wi-Fi attempt. The radio can refuse even that; the next try
    goes ahead anyway."""
    try:
        wlan.disconnect()
    except OSError:
        pass


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


def _merged(body, config):
    """A copy of the config with the push merged in, or None when the push is
    not one to store: not a JSON object, a claim and a token together, a bad
    network list, a field of the wrong type or a bad hwid. The config itself
    is never changed."""
    try:
        push = json.loads(body)
    except ValueError:
        return None
    # One credential slot: a push carrying both a claim and a token is refused.
    if not isinstance(push, dict) or ("claim" in push and "token" in push):
        return None
    if "aps" in push and not valid_networks(push["aps"]):
        return None

    candidate = config.copy()
    for field_name, field_value in push.items():
        if field_name == "claim":
            field_name = "token"
        # Wrong type is bad-json by contract; a missing field is checked later.
        if field_name in STRING_FIELDS and not isinstance(field_value, str):
            return None
        # The hwid goes into the announce line, a topic and a URL.
        if field_name == "hwid" and not valid_hwid(field_value):
            return None
        candidate[field_name] = field_value

    # A top-level ssid without aps replaces the stored network list.
    if "ssid" in push and "aps" not in push:
        candidate.pop("aps", None)
    # A new device does not sign with the old device's key: the key goes,
    # unless this same push brings one. A token, networks or a key alone
    # keep it.
    if _new_device(push, config) and not push.get("key"):
        candidate.pop("key", None)
    return candidate


def _new_device(push, config):
    """True when a push sets the board up as a new device: it carries a claim
    code, or an id other than the stored one. Letter case aside: the setup
    page finds a board's device that way and pushes the device's spelling."""
    hwid = push.get("hwid")
    return bool(push.get("claim")) or bool(
        hwid and hwid.lower() != config.get("hwid", "").lower())


def apply_config_push(body, config):
    """Validate and merge a push in a separate config copy, then save and reboot.
    Rejected pushes leave the active config unchanged."""
    candidate = _merged(body, config)
    if candidate is None:
        print("CHIRP= err bad-json")
        return

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
