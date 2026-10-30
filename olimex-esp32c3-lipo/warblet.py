# Warblet for MicroPython: USB setup, Wi-Fi, and messages to your dashboard,
# in two files. This one sets the board up and keeps it online;
# warblet_https.py sends reports and takes commands. main.py imports the
# second, which brings this one's calls with it:
#
#     import warblet_https as warblet
#     warblet.demo("demo-olimex-c3", "v1.6.0")   # what the setup page sees
#     warblet.on_command(on_command)             # called with each command's bytes
#     warblet.begin()                            # waits for USB setup the first time
#     while True:
#         if warblet.connected():                # joins Wi-Fi when it must
#             warblet.send(report(), tag)        # returns the HTTP status
#         warblet.wait(INTERVAL_S)               # answers USB setup, collects commands
#
# The setup page at warbletiot.com/flash sends the Wi-Fi networks, the board's
# id, its token and, for signed messages, its key over the USB cable
# (CHIRP-PROV v1.8). They are kept in chirp_cfg.json. Messages are signed
# once a key is stored. The calls copy the Warblet Arduino library's names.
#
# Comments here, not docstrings: a board holds every docstring in memory while
# it compiles a file, and comments cost it nothing. Two files of this size,
# written this way, compile on a board with little memory to spare.
import json
import machine
import network
import select
import sys
import time

try:
    from uhashlib import sha256
    from ubinascii import a2b_base64, hexlify
except ImportError:  # host CPython, so this file can be tested off the board
    from hashlib import sha256
    from binascii import a2b_base64, hexlify

# ---- settings, and USB setup (CHIRP-PROV v1.8) -----------------------------

CFG_PATH = "chirp_cfg.json"

# The push fields this firmware acts on. Any other is refused by name.
ACCEPTS = ("ssid", "pass", "aps", "hwid", "host", "claim", "token", "key")
STRING_FIELDS = ("ssid", "pass", "hwid", "host", "claim", "token", "key")

# What the setup page learns this firmware can do. The parts below add their
# routes to TRANSPORTS (the HTTPS part adds "https").
TRANSPORTS = []

MAX_NETWORKS = 8     # CHIRP-PROV caps the list; a longer one is refused
MAX_LINE = 4096      # a setup line is never this long; more is junk
WIFI_TIMEOUT_S = 12  # per-attempt association wait
PROV_PASS_S = 0.5    # USB setup time before each association attempt
SETTINGS_S = 10      # after an announce, keep reading this long for the push
LINE_IDLE_MS = 500   # a setup line still arriving may finish past the deadline,
                     # unless its bytes stop for this long
COMMAND_EVERY_S = 5  # a command check, then this long before the next

config = {}          # the stored settings; begin() fills it
_slug = ""           # the demo this firmware is (demo())
_fw = "warblet-micropython"  # its version (demo())
_handler = None      # on_command()
_command_check = None  # the part that asks for commands (use_commands())
_next_check = 0

_poller = select.poll()
_poller.register(sys.stdin, select.POLLIN)
# Read the console as bytes. A text read waits for the rest of a character,
# so one stray byte -- from a program on the computer probing serial ports,
# say -- could stop the board for ever. A runtime without stdin.buffer falls
# back to the text stream.
_console = getattr(sys.stdin, "buffer", sys.stdin)
_buf = bytearray()   # the console line so far, as bytes
_probed = False      # a CHIRP? read and not yet answered


def demo(slug, fw):
    # Name the demo and its version for the setup page (t= and fw=).
    global _slug, _fw
    _slug, _fw = slug, fw


def on_command(handler):
    # Call handler(command_bytes) for each command from the dashboard.
    global _handler
    _handler = handler


def check_commands_every(seconds):
    # Ask for commands this often (5 s unless set).
    global COMMAND_EVERY_S
    COMMAND_EVERY_S = seconds


def use_commands(check):
    # For the transport part: check(handler) asks for commands.
    global _command_check
    _command_check = check


def load():
    # The stored settings, or an empty dict when there are none.
    try:
        with open(CFG_PATH) as f:
            stored = json.load(f)
        return stored if isinstance(stored, dict) else {}
    except (OSError, ValueError):
        return {}


def ready(settings):
    # True when the settings are complete enough to report with.
    return bool(settings.get("ssid") and settings.get("token")
                and settings.get("hwid"))


def begin():
    # Load the settings. Until the setup page has sent them, only answer it:
    # a good push saves them and restarts the board.
    config.clear()
    config.update(load())
    if not ready(config):
        print("chirp: not provisioned -- waiting on USB, warbletiot.com/flash")
        while True:
            service(1)
    print("chirp: reporting as", config["hwid"],
          "signed" if signing_key() else "token only")


def signing_key():
    # The stored signing key (base64), or None when there is none that will sign.
    key = config.get("key")
    return key if usable_key(key) else None


B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"


def usable_key(key):
    # True for a key that will sign: 44 base64 characters, one = at the end,
    # that decode to 32 bytes.
    try:
        return (isinstance(key, str) and len(key) == 44 and key[43] == "="
                and all(c in B64 for c in key[:43]) and len(a2b_base64(key)) == 32)
    except ValueError:
        return False


def caps():
    # The capability object the setup page reads, keys in the contract's
    # order: Token, then Signed once a key is stored; the key comes over USB;
    # no over-the-air updates; and the routes the parts below registered.
    return ('{"v":1,"auth":[1,2],"keyProvisioning":["usb"],"otaKeyRotation":false,'
            '"otaFirmwareUpdate":false,"transports":[%s]}'
            % ",".join('"%s"' % t for t in TRANSPORTS))


def announce():
    # The answer to CHIRP?: the board's id, version, key state and capabilities.
    return "CHIRP! v=1 hw={} fw={} key_set={} radios=wifi t={} caps={}".format(
        config.get("hwid", ""), _fw, 1 if signing_key() else 0, _slug, caps())


def service(seconds=0):
    # Answer USB setup for this long; zero reads what is waiting.
    # A good push saves the settings and restarts the board.
    # Probes are answered once the waiting input has been read, once for all of
    # them: a board that was busy finds a probe for every quarter second it was
    # away, and answering each would keep it printing while the push arrives.
    # The setup page sends its push as soon as it reads the answer, so the pass
    # then goes on for SETTINGS_S: the board's next network request reads
    # nothing, and a UART console holds only a few hundred bytes. A setup line
    # still arriving when the pass ends is read to its end.
    global _probed, _buf
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    late = 0  # bytes read past the deadline to finish a line
    while True:
        remaining = time.ticks_diff(deadline, time.ticks_ms())
        if _probed:
            wait = 0  # owing an answer: only look for more input
        elif remaining > 0:
            wait = remaining
        elif _line_started(late):
            wait = LINE_IDLE_MS  # a setup line is arriving: let it end
        else:
            wait = 0
        if _poller.poll(wait):
            byte = _console.read(1)
            if byte:
                if remaining <= 0:
                    late += 1
                _feed(byte)
                continue  # drain the rest of the line without waiting again
        if _probed:
            _probed = False
            print(announce())
            window = time.ticks_add(time.ticks_ms(), SETTINGS_S * 1000)
            if time.ticks_diff(window, deadline) > 0:
                deadline = window  # the push follows: keep reading for it
        if time.ticks_diff(deadline, time.ticks_ms()) > 0:
            continue
        if remaining > 0 and _line_started(late):
            continue  # the pass ended in the middle of a line: read the rest
        if remaining <= 0 and wait:
            _buf = bytearray()  # the line stopped coming: it will not end now
        return


def _line_started(late):
    # True when a setup line has begun and is still short enough to finish.
    return late < MAX_LINE and bytes(_buf).find(b"CHIRP") >= 0


def _feed(byte):
    # One byte off the console into the line buffer. A line counts from its
    # first "CHIRP", so stray bytes in front of a command do not spoil it.
    global _buf, _probed
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
        _probed = True  # answered by service() once the input is read
    elif line.startswith("CHIRP+"):
        apply_config_push(line[6:].strip())


HWID_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_."
NAME_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"


def valid_hwid(value):
    # True when a pushed `hwid` is the platform's grammar: letters, digits and
    # : - _ . only, at most 128. Empty means no id.
    return len(value) <= 128 and all(c in HWID_CHARS for c in value)


def valid_networks(entries):
    # True when a pushed `aps` list is storable: at most MAX_NETWORKS objects,
    # each with a non-empty string ssid and an optional string pass.
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


def _merged(body, settings):
    # (a copy of the settings with the push merged in, None), or (None, the
    # reason it is refused), in the contract's order: not one JSON object; a
    # field this firmware does not act on (the first by name, so every board
    # names the same one); a bad value; then, in apply_config_push(), an
    # incomplete result. The settings themselves are never changed.
    try:
        push = json.loads(body)
    except ValueError:
        return None, "bad-json"
    if not isinstance(push, dict):
        return None, "bad-json"
    unknown = [name for name in push if name not in ACCEPTS]
    if unknown:
        # A name that is not a short plain word cannot go on the answer line.
        if not all(0 < len(n) <= 32 and all(c in NAME_CHARS for c in n) for n in unknown):
            return None, "bad-json"
        return None, "unsupported " + min(unknown)
    # One credential slot: a push carrying both a claim and a token is refused.
    if ("claim" in push and "token" in push) or \
            ("aps" in push and not valid_networks(push["aps"])):
        return None, "bad-json"

    candidate = settings.copy()
    for name, value in push.items():
        if name in STRING_FIELDS and not isinstance(value, str):
            return None, "bad-json"
        # An empty credential or key is refused; a key must be one that signs.
        if name in ("claim", "token", "key") and not value:
            return None, "bad-json"
        if (name == "key" and not usable_key(value)) or \
                (name == "hwid" and not valid_hwid(value)):
            return None, "bad-json"
        candidate["token" if name == "claim" else name] = value

    # A top-level ssid without aps replaces the stored network list.
    if "ssid" in push and "aps" not in push:
        candidate.pop("aps", None)
    # A new device does not sign with the old device's key: the key goes,
    # unless this same push brings one. A token, networks or a key alone
    # keep it.
    if _new_device(push, settings) and "key" not in push:
        candidate.pop("key", None)
    return candidate, None


def _new_device(push, settings):
    # True when a push sets the board up as a new device: it carries a claim
    # code, or an id other than the stored one. Ids are compared exactly, as
    # Warblet tells devices apart: "sensor" and "SENSOR" are two.
    hwid = push.get("hwid")
    return bool(push.get("claim")) or bool(hwid and hwid != settings.get("hwid", ""))


def apply_config_push(body):
    # Check a push and merge it into a copy of the settings, then save it and
    # restart. A refused push changes nothing.
    candidate, refused = _merged(body, config)
    if refused:
        print("CHIRP= err " + refused)
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
    # The live settings are left alone: the restart below reloads the file.
    print("CHIRP= ok")
    time.sleep_ms(200)  # let the ok leave the wire before the reset
    machine.reset()


def wait(seconds):
    # Wait this long, answering USB setup and, every COMMAND_EVERY_S, asking
    # for a command once on_command() has a function. A check that is running
    # when the time is up finishes first.
    global _next_check
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while True:
        left = time.ticks_diff(deadline, time.ticks_ms())
        if _handler and _command_check:
            left = min(left, max(0, time.ticks_diff(_next_check, time.ticks_ms())))
        service(max(0, left) / 1000)
        if _handler and _command_check and time.ticks_diff(time.ticks_ms(), _next_check) >= 0:
            _command_check(_handler)
            _next_check = time.ticks_add(time.ticks_ms(), COMMAND_EVERY_S * 1000)
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            return


def check_now():
    # Make the next wait() ask for a command at once (after each report:
    # an answer to it may be waiting).
    global _next_check
    _next_check = time.ticks_ms()


# ---- Wi-Fi -----------------------------------------------------------------

def networks(settings):
    # Return (ssid, password) pairs in order, without repeating the first network.
    pairs = []
    primary = settings.get("ssid")
    if primary:
        pairs.append((primary, settings.get("pass") or ""))
    for entry in settings.get("aps") or []:
        if not isinstance(entry, dict):
            continue
        ssid = entry.get("ssid")
        if ssid and ssid not in [name for name, _ in pairs]:
            pairs.append((ssid, entry.get("pass") or ""))
    return pairs


def connected():
    # True once Wi-Fi has an address; tries the stored networks in order
    # when it has none. Call it before every message, so the networks are tried
    # again by themselves when the link drops. USB setup is answered before and
    # during each attempt, so a board that cannot join is still set up.
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return True

    for ssid, password in networks(config):
        service(PROV_PASS_S)
        if _join(wlan, ssid, password):
            return True

    print("chirp: no stored network joined -- will retry")
    return False


def _join(wlan, ssid, password):
    # One attempt at one network. True once we have an address. The console
    # is read while we wait: a UART console holds only a few hundred bytes, and
    # a setup push that waited out a 12 s join would lose the rest.
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
        service(0.25)

    _drop(wlan)  # the half-open attempt, so the next try starts clean
    print("chirp: no join to", ssid, "within", WIFI_TIMEOUT_S, "s")
    return False


def _drop(wlan):
    # End a Wi-Fi attempt. The radio can refuse even that; the next try
    # goes ahead anyway.
    try:
        wlan.disconnect()
    except OSError:
        pass


# ---- the clock -------------------------------------------------------------

# The nonce needs Unix seconds. MicroPython counts from 2000 on the ESP32 and
# STM32 ports and from 1970 on some others (rp2, mimxrt).
_EPOCH_OFFSET = 946684800 if time.gmtime(0)[0] == 2000 else 0
_CLOCK_SANE = 1735689600  # Minimum Unix time accepted as a set clock

# An RTC drifts, and a nonce has to stay inside the server's +/-5 min:
# re-sync this often, and sooner after a refused signed message -- but not on
# every message, since a bad token or key is refused the same way.
NTP_RESYNC_S = 6 * 3600
NTP_AFTER_401_S = 15 * 60

_last_nonce = 0
_ntp_at = None  # time.time() at the last NTP try
_ntp_wait = 0   # seconds after that try before the next one


def sync_clock():
    # Try NTP to set the clock. Certificate checks need it, and a signed
    # message uses it as its nonce.
    global _ntp_at, _ntp_wait
    _ntp_at = time.time()
    _ntp_wait = NTP_RESYNC_S
    try:
        import ntptime
        ntptime.settime()
        _ntp_at = time.time()  # the clock may have jumped
    except Exception as e:
        print("chirp: ntp sync failed:", e)


def require_clock():
    # Certificate dates are checked against the RTC, so an unset clock gets
    # one more NTP try and then fails the request.
    if time.time() + _EPOCH_OFFSET < _CLOCK_SANE:
        sync_clock()
    if time.time() + _EPOCH_OFFSET < _CLOCK_SANE:
        raise OSError("no clock, so the server certificate cannot be checked "
                      "-- allow NTP on this network")
    if _ntp_at is None or time.time() - _ntp_at > _ntp_wait:
        sync_clock()  # a failed re-sync keeps the clock it has


def clock_suspect():
    # A signed request was refused: the nonce may be outside the window, so
    # the next NTP try comes sooner.
    global _ntp_wait
    _ntp_wait = NTP_AFTER_401_S


def nonce_ms():
    # Unix ms, strictly increasing. Call it after require_clock().
    global _last_nonce
    ms = (time.time() + _EPOCH_OFFSET) * 1000 + (time.ticks_ms() % 1000)
    if ms <= _last_nonce:
        ms = _last_nonce + 1  # the contract forbids two signed sends on one ms
    _last_nonce = ms
    return ms


# ---- signing: HMAC-SHA256 --------------------------------------------------
# Every signature covers the same shape of bytes:
#     <nonce> "\n" "tag=" <tag> "\n" <body>
# Both lines are always there. A field the message does not have is left
# empty, so the bytes say which fields were sent and a nonce or tag cannot be
# moved into the body.

BLOCK = 64


def _digest(data):
    h = sha256()
    h.update(data)
    return h.digest()


def hmac_sha256(signing_key_bytes, message):
    if len(signing_key_bytes) > BLOCK:
        signing_key_bytes = _digest(signing_key_bytes)
    ipad = bytearray(BLOCK)
    opad = bytearray(BLOCK)
    ipad[:len(signing_key_bytes)] = signing_key_bytes
    opad[:len(signing_key_bytes)] = signing_key_bytes
    for i in range(BLOCK):
        ipad[i] ^= 0x36
        opad[i] ^= 0x5C
    return _digest(bytes(opad) + _digest(bytes(ipad) + message))


def signed_input(nonce, tag, body):
    # Return the bytes a signature covers: "<nonce>\ntag=<tag>\n" + body.
    # None means the message has no such field; it is signed as empty.
    # Example: signed_input(None, None, b"temp=21.4") == b"\ntag=\ntemp=21.4".
    nonce = "" if nonce is None else str(nonce)
    tag = "" if tag is None else str(tag)
    return nonce.encode() + b"\ntag=" + tag.encode() + b"\n" + body


def sign(signing_key_base64, nonce, body, tag=None):
    # Return the hex HMAC-SHA256 for X-Chirp-Signature. Pass the nonce and
    # tag this message sends as headers, or None for a header it leaves out.
    return hexlify(hmac_sha256(a2b_base64(signing_key_base64),
                               signed_input(nonce, tag, body))).decode()


def sign_poll(signing_key_base64, hwid, nonce):
    # Return the hex HMAC-SHA256 a signed device sends when it asks for
    # commands: over "chirp-down\n<hwid>\n<nonce>".
    challenge = "chirp-down\n{}\n{}".format(hwid, nonce).encode()
    return hexlify(hmac_sha256(a2b_base64(signing_key_base64), challenge)).decode()


def sign_raw(signing_key_base64, body, tag=None):
    # Return the 32-byte HMAC-SHA256 for MQTT/TCP payload prefixes. These
    # transports carry no nonce, so the nonce field is empty; a tag travels in
    # the topic or line and is signed here. Only the body is sent.
    return hmac_sha256(a2b_base64(signing_key_base64), signed_input("", tag, body))


# ---- TLS: the roots Warblet's certificates chain to ------------------------

# Let's Encrypt roots. X2 and X1 anchor today's chains; YE and YR are their successors.
# ISRG Root X2: https://letsencrypt.org/certs/isrg-root-x2.pem
# SHA-256 (DER): 69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470
# ISRG Root X1: https://letsencrypt.org/certs/isrgrootx1.pem
# SHA-256 (DER): 96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6
# ISRG Root YE: https://letsencrypt.org/certs/gen-y/root-ye.pem
# SHA-256 (DER): E14FFCAD5B0025731006CAA43A121A22D8E9700F4FB9CF852F02A708AA5D5666
# ISRG Root YR: https://letsencrypt.org/certs/gen-y/root-yr.pem
# SHA-256 (DER): E57B7E6F150C419102E8D5C055729FF967B9D1A829BF00CEC89CA604EBF4A86F
CA_ROOTS = """-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIB2TCCAWCgAwIBAgIRAKQCa6LvbHwg1AR+XmWmk4AwCgYIKoZIzj0EAwMwLjEL
MAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWUUwHhcN
MjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzENMAsG
A1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZRTB2MBAGByqGSM49AgEGBSuBBAAi
A2IABDwS/6vhrcVqcbBo+wgdI3fwn9x7DNJJOY/lTOti0vkwuRN87RhEhTH17E7X
yFjWsPYhIPt/wzOqxTd2b+4ZJNy9ID04YywF9U5zasDVyGSNErVNtz8uSGh5izW8
7j77GaNCMEAwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0O
BBYEFKPIJlqOoUzQNWP8myPIOq5W809WMAoGCCqGSM49BAMDA2cAMGQCMHhMr8N9
LdL1VQKs9BdV81r76eXRB6mtjuNjzk6/lBsPNToWLTDzGYgtQKO1jl63uAIwGV7m
onyF377c+MM1oqVNs17sgu7F9YKZwgLmVbeOMDbKAXHtKMDLbiGllCcs8f47
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFKTCCAxGgAwIBAgIRAOxGNJNgz0sP+KmC2Tqpyj0wDQYJKoZIhvcNAQELBQAw
LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw
HhcNMjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzEN
MAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjCCAiIwDQYJKoZIhvcNAQEB
BQADggIPADCCAgoCggIBANvGJnN78CTJdWL3+eGfsLN5TrNBJs+VH9hRXqRbwxu9
sGNiB0BD1fcOxbSUQCJIM1xE13Db+5Cw1w0s0EBYsvuIP/6joF0w8cuImbgR1OGg
YbSQ4OpzI+DG8SGuTlcE873OCS+kh3srlo6vl43M5OJg4Aeo1sfHp6kTJDoIiFBN
JAY+OKfX/FUvYKuhjT+no49lmqmupSBI5PkBQiqrEGtWU5uxU/cQWHGu8jSjFBzn
ZqvbNPLMXMLFxCb3WTfrJBXXjqvWG+v4bjzxjjeAtOlU7qarRDvNOyAuQYLln904
M+faKx8hnLCpJ15ZqaEgcNlY+9MMWcC5yvL2A2j3l9+2buggZX+dOE91zYmIdawT
vSZuVvlbRrAlLxIB6pwMBjneXCjYQ8+3BCCjssbSNpZU3hTcBDdhfAlEDlYr6pEa
tnMdmDT5BqnKC92bd0EhM1fbLHioLccLCuievT8ZkPhZrq7Mii7gNXAcUEAR8+lz
Yal+9zTg7C5DALyVOeG/CqfRAMn1KSHCR0NSA6P8tn/mGRlnCct5rtVCLnVySVpU
6H1qGg3DgTOuskf8eahTMiYbI5ezPJmO5ertalskQ1utp74+eDy92PI4ftHKTbq9
IWhH4YZKh3WnJEIt+oQvlYZbY8tpEroKrFB6PFGzrJIDRyts4HqvuH52RFj2zv/B
AgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud
DgQWBBTe51tg0CJtQCh9Pw0B/qS1UrRRlDANBgkqhkiG9w0BAQsFAAOCAgEAWHnf
713Bdkq7t5yN2dNIgQakUb94X9WuyhMEHHkgx4oDpSUlnG0w4g94MoqaEUE31ZjR
LU7L5LD1g9ujFHTQu8AD215AHMVQFbm6j8hQxdXHAzDajFNQnOlDJrLjzIx176oy
AjvUtejZx2NNmdb5fd0WGVGsCdoAJ3N8ozo7ajE8t6vfxStZb4BQ9WYJGHUDrv2N
i5tJF6CNiPnlzs3BUfECRbE4JSk+jvy8+VoGiFE8qsH/j78x2fjgQhAQFV7P7Zxy
dBTZ1wEkNpZNW2qnaK1SKBLa+xf6E06YRIq5uaI+HWH8SY1y5VbRgzq40EKg3yxP
06fz+uYAUIFJoLNfhwRCc3Q6pQVuMX3yAjHAes4gk4moGcLQ5p7HAh39yeylZc1J
41sx/jKwLIkPE6Rr1Nf4pxdsxf9SA4yOEiAkDgq04DVxn8hgYFdUtBCuiuVC2heA
EiqVEa+8QZjuw8Gj0EbHXcRd1nInvGqRS1o9Is7YBdQN57X1AYveGBNNqjICSb7c
awuw1EawTDrs13VUlJVEsbQ0/O/1aaV73mCdOQ8azqL2KTv1Ewu1xbquE2S+kdQU
To9TUwat3wUA6cwXh1EfpS/3fJ0aGah5hdpRyoCLDlsSn8tkrjMfFFX0viC+GxHc
sI1ANRYvqSFC2X1VRZfDg+wD6E21BccmifG4yWc=
-----END CERTIFICATE-----
"""

_ctx = None


def tls_context():
    # The context that refuses any chain not ending at CA_ROOTS, built once:
    # one per request holds its parsed roots until gc runs, and a dozen
    # back-to-back handshakes exhaust the heap. wrap_socket(server_hostname=...)
    # adds the host name check. It needs ssl.SSLContext with cadata, which a
    # recent MicroPython has (these demos pin 1.28.0) and v1.14 has not.
    global _ctx
    if _ctx is None:
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(cadata=CA_ROOTS)
        _ctx = ctx
    return _ctx
