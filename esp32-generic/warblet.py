# Warblet for MicroPython: USB setup, Wi-Fi, and messages to your dashboard.
# This file sets the board up and keeps it online. A part beside it sends the
# messages: warblet_https.py, over HTTPS. main.py imports both, and the part
# adds itself to this file as it is imported:
#
#     import warblet                             # setup, Wi-Fi, clock, signing
#     import warblet_https                       # needed: it does the sending
#     warblet.demo("demo-olimex-c3", "v1.6.0")   # what the setup page sees
#     warblet.on_command(on_command)             # called with each command's bytes
#     warblet.begin()                            # waits for USB setup the first time
#     while True:
#         if warblet.connected():                # joins Wi-Fi when it must
#             warblet.send(report(), tag)        # True when it went: the part's send() says what that means
#         warblet.wait(INTERVAL_S)               # answers USB setup, collects commands
#
# The setup page at warbletiot.com/flash sends the Wi-Fi networks, the board's
# id, its token and, for signed messages, its key over the USB cable
# (CHIRP-PROV v1.8). They are kept in chirp_cfg.json. Messages are signed
# once a key is stored. The calls copy the Warblet Arduino library's names.
# A setting in capitals can be changed from main.py after the imports, for
# example warblet.WIFI_TIMEOUT_S = 30. The part adds its own settings here as
# it is imported: warblet_https.py adds warblet.HOST and
# warblet.COMMAND_EVERY_S.
import json
import machine
import network
import os
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
NEW_PATH = "chirp_cfg.new"   # a new copy, before it takes the old one's place

# The push fields this firmware acts on, and the type each must have. Any
# other field is refused by name. A part adds the fields it takes (add_part).
FIELDS = {"ssid": str, "pass": str, "aps": list, "hwid": str, "host": str,
          "claim": str, "token": str, "key": str}

# The routes the parts send over, which the setup page is told (add_part).
TRANSPORTS = []

MAX_NETWORKS = 8     # CHIRP-PROV caps the list; a longer one is refused
MAX_LINE = 4096      # a setup line is never this long; more is junk
WIFI_TIMEOUT_S = 12  # per-attempt association wait
PROV_PASS_S = 0.5    # USB setup time before each association attempt
SETTINGS_S = 10      # after an announce, keep reading this long for the push
LINE_IDLE_MS = 1500  # a setup line still arriving may finish past the deadline,
                     # unless the console is seen quiet this long: longer than
                     # the 1 s a hidden browser tab waits between pieces of a push
LINE_LATE_S = 70     # ...and for at most this long past it: MAX_LINE in
                     # 64-byte pieces a second apart takes 64 s
LATE_JUNK = 4096     # past the deadline, other input is read up to this many
                     # bytes: more than a console holds (a UART or USB console
                     # here keeps a few hundred, the Pyboard's 1 KB), so what
                     # waited is read and input that never stops is left

config = {}          # the stored settings; begin() fills it
_slug = ""           # the demo this firmware is (demo())
_fw = "warblet-micropython"  # its version (demo())
_handler = None      # on_command()
_send = None         # the part's send() (add_part)
_upkeep = None       # the part's upkeep(), called by wait() (add_part)
_turn_at = 0         # when the part wants its next turn (time.ticks_ms())
_handling = False    # a command's handler is running (command())

_poller = select.poll()
_poller.register(sys.stdin, select.POLLIN)
# Read the console as bytes. A text read waits for the rest of a character,
# so one stray byte -- from a program on the computer probing serial ports,
# say -- could stop the board for ever. A runtime without stdin.buffer falls
# back to the text stream.
_console = getattr(sys.stdin, "buffer", sys.stdin)
_buf = bytearray()   # the console line so far, as bytes
_start = -1          # 0 once "CHIRP" has come (_buf starts there), else -1
_skip = False        # the rest of a line that was too long: dropped to its end
_last = 0            # ticks_ms() since which every look at the console found
                     # nothing; 0 once a look finds a byte
_probed = False      # a CHIRP? read and not yet answered


SLUG_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789-"
FW_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"


def demo(slug, fw):
    # Name the demo and its version for the setup page (t= and fw=). One that
    # breaks its grammar is not used, and the console says so.
    global _slug, _fw
    if 3 <= len(slug) <= 40 and all(c in SLUG_CHARS for c in slug):
        _slug = slug
    else:
        print("chirp: demo(): not a demo slug (3 to 40 of a-z, 0-9 and -), so the announce leaves it out")
    if 0 < len(fw) <= 64 and fw[0] not in "._-" and all(c in FW_CHARS for c in fw):
        _fw = fw
    else:
        print("chirp: demo(): not a version (1 to 64 of A-Z a-z 0-9 . _ -), so the board says", _fw)


def on_command(handler):
    # Call handler(command_bytes) for each command from the dashboard.
    global _handler
    _handler = handler


def command(data):
    # Hand one command to the function given to on_command(). A part calls
    # this from its upkeep(), which wait() runs outside any send or
    # connection, so the handler can send (and connect) as main.py does. A
    # handler that raises is said on the console in one line, and the loop
    # goes on. While it runs, a wait() inside it hands over no other command.
    global _handling
    if _handler:
        _handling = True
        try:
            _handler(data)
        except Exception as e:
            print("chirp: the command handler failed:", e)
        finally:
            _handling = False


def add_part(transports, send, upkeep, fields=None):
    # How a part such as warblet_https.py adds itself as main.py imports it:
    # the routes it sends over; its send(); its upkeep(handler), which wait()
    # calls on every pass, and which does what is due (a command check, a
    # ping) and returns how soon, in seconds, it wants the next call; and any
    # push field it takes, with its type (a TLS switch would be {"tls": bool}).
    # One part only: a second would take over the first one's messages.
    global _send, _upkeep
    if _send:
        raise RuntimeError("warblet: a second part (%s): import one warblet_ part"
                           % ",".join(transports))
    TRANSPORTS.extend(transports)
    FIELDS.update(fields or {})
    _send, _upkeep = send, upkeep


def send(payload, tag=None):
    # Send the payload bytes, with an optional tag saying what the message is
    # (for example "press"), over the part's route. True when it went; what
    # "went" means is the part's (its send() says). False when it did not,
    # and the console says why.
    turn_now()
    return _send(payload, tag)


def turn_now():
    # Give the part its turn at once, at the next pass of wait(): after a
    # message, an answer to it may be waiting. A part calls it too.
    global _turn_at
    _turn_at = time.ticks_ms()


# ---- the store -------------------------------------------------------------
# The promise: after one failed operation in a save, or a power cut at any
# point, and any number of later saves, the board still holds a complete copy
# of its settings, and the answer it gave is what the next start reads. The
# one exception: when the flash cannot confirm a store (os.sync() fails), the
# answer is store-failed and the board cannot know which copy the next start
# reads; another push settles it. Two failures inside one save are not
# covered.
#
# A save writes the new settings whole to NEW_PATH, syncs, and renames it over
# CFG_PATH. On LittleFS that rename replaces the file in one step; FAT's rename
# removes the old file first, so a failure there can leave only the new copy,
# which the next start then reads. settle() runs first, at every start and
# every save.

ENOENT = 2  # "no such file": the only error that means a file is not there


def _read(path):
    # The settings in this file, or None when there is no file or it does not
    # parse as settings. An error reading a file that is there raises OSError:
    # that file may be good, so nothing is decided from it.
    try:
        with open(path) as f:
            stored = json.load(f)  # parsed as it is read: no copy of the whole text
    except OSError as e:
        if e.args[0] == ENOENT:
            return None
        raise
    except ValueError:
        return None
    return stored if isinstance(stored, dict) else None


def _remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


def _sync():
    # Write the file system's cache to flash, on a runtime that has os.sync()
    # (the Pyboard's flash keeps a cache, and close() does not empty it).
    sync = getattr(os, "sync", None)
    if sync:
        sync()


def settle():
    # Leave the stored settings under the live name, and return (them, True),
    # or (them, False) when the new copy must be kept as it is. A live file
    # that is ready() wins, and a new copy beside it is a save that did not
    # finish: it goes. Otherwise a new copy that is ready() is a save that
    # finished all but its rename: it takes the live name, synced before that
    # name is written again. A live file that cannot be read may be good: the
    # board runs on the new copy for now, and nothing is replaced or removed.
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
        return live or {}, False  # the new copy may be the good one: keep it
    if not (new and ready(new)):
        return live or {}, True
    try:
        os.rename(NEW_PATH, CFG_PATH)
        _sync()
    except OSError:
        return new, False  # the board runs on the new copy for now
    return new, True


def load():
    # The stored settings, or an empty dict when there are none.
    return settle()[0]


def save(settings):
    # Store the settings (ready() ones: apply_config_push() checks), or raise
    # OSError (store-failed) when the next start will read the previous ones,
    # or the flash cannot confirm the store.
    if not settle()[1]:
        # The good copy may be the new one, or the live one cannot be read:
        # writing now could lose it.
        raise OSError("the stored settings cannot be replaced safely now")
    try:
        with open(NEW_PATH, "w") as f:
            json.dump(settings, f)
        _sync()
    except OSError:
        _remove(NEW_PATH)
        _sync()  # the removal on flash before the answer: a candidate that reached it stays refused
        raise
    try:
        os.rename(NEW_PATH, CFG_PATH)
    except OSError:
        try:
            os.stat(CFG_PATH)
        except OSError as e:
            if e.args[0] != ENOENT:
                raise  # no telling which copy is there, so both are kept
            _sync()    # FAT removed the old file: the new copy is the stored one
            return
        _remove(NEW_PATH)  # the old file stands
        _sync()            # and the candidate's removal is on flash before the answer
        raise
    _sync()


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
    # nothing, and a UART console holds only a few hundred bytes. Past the
    # pass's end, a setup line already begun (from its "CHIRP") is read to its
    # end, for up to LINE_LATE_S more; other input is read up to LATE_JUNK
    # bytes, so junk that keeps coming cannot hold the loop. (Probes that keep
    # coming each extend the pass, one answer per SETTINGS_S: a page probes
    # for 15 s at most.) A setup line is given up wherever it is, over the
    # limit or not, once the console has been seen quiet for LINE_IDLE_MS:
    # looks that found nothing, that far apart, in one call or across two
    # (bytes that came between would still be waiting). Inside the pass the
    # pass goes on, and a probe sent after a stalled line is answered. Bytes
    # found waiting when the board comes back from other work join the line
    # that was arriving: nothing shows they paused, so a good push is kept.
    global _probed, _buf, _start, _skip, _last
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    junk = 0  # bytes read past the deadline in no setup line
    while True:
        now = time.ticks_ms()
        remaining = time.ticks_diff(deadline, now)
        late = time.ticks_diff(time.ticks_add(deadline, LINE_LATE_S * 1000), now)
        held = _start >= 0 or _skip  # a line being collected or thrown away
        if _probed:
            wait = 0  # owing an answer: only look for more input
        elif held:
            # A setup line is arriving, or one is being thrown away: its next
            # byte, until the console has been seen quiet LINE_IDLE_MS.
            wait = max(0, min(LINE_IDLE_MS - time.ticks_diff(now, _last or now), late))
        else:
            wait = max(0, remaining)
        byte = _console.read(1) if _poller.poll(wait) else None
        if not (byte or _last):
            _last = now  # nothing waited, nothing came: quiet from here
        # The look ended when the poll's wait did: time spent after it is not
        # quiet the board saw. The wait counts whole: in MicroPython 1.28.0,
        # poll returns empty only once its timeout has run out (EINTR loops,
        # anything else raises: extmod/modselect.c), and
        # sys.stdin.buffer.read(n) takes each byte with mp_hal_stdin_rx_chr()
        # and returns n bytes, never nothing (shared/runtime/sys_stdio_mphal.c).
        idle = held and not byte and time.ticks_diff(now, _last) + wait >= LINE_IDLE_MS
        if byte:
            _last = 0
            if remaining <= 0 and _start >= 0 and late <= 0:
                # LINE_LATE_S past the pass and still coming: given up, and
                # the rest of it is dropped through its line ending.
                _buf, _start, _skip = bytearray(), -1, True
                _feed(byte)
                return
            _feed(byte)
            if remaining > 0 or _start >= 0:
                continue  # drain the rest of the line without waiting again
            junk += 1
            if junk < LATE_JUNK:
                continue
        elif not _probed and (idle or _start >= 0):
            # It will not end now. Seen quiet for LINE_IDLE_MS, it is given up,
            # over the limit or not, and the next byte starts afresh; LINE_LATE_S
            # running out past the pass drops the rest through its line ending.
            _buf, _start, _skip = bytearray(), -1, not idle
            continue
        if _probed:
            _probed = False
            print(announce())
            window = time.ticks_add(time.ticks_ms(), SETTINGS_S * 1000)
            if time.ticks_diff(window, deadline) > 0:
                deadline = window  # the push follows: keep reading for it
        if time.ticks_diff(deadline, time.ticks_ms()) > 0 or _start >= 0:
            continue  # the pass goes on, or a line is still arriving
        return


def _feed(byte):
    # One byte off the console into the line buffer. A line counts from its
    # first "CHIRP": stray bytes in front of a command are dropped and do not
    # spoil it. A line longer than MAX_LINE from there is thrown away whole,
    # through its line ending, so no part of it is read as a command.
    global _buf, _probed, _start, _skip
    if isinstance(byte, str):  # the text-stream fallback hands us characters
        byte = byte.encode()
    if byte not in (b"\r", b"\n"):  # the portal sends CRLF; a bare LF is tolerated
        if _skip:
            return
        if len(_buf) >= MAX_LINE:
            _buf, _start, _skip = bytearray(), -1, True  # far too long for a setup line
            return
        if _start < 0 and len(_buf) >= 64:
            _buf = _buf[-4:]  # nothing before "CHIRP" counts, so junk is not kept
        _buf += byte
        if _start < 0 and byte == b"P" and len(_buf) >= 5 and _buf[-5:] == b"CHIRP":
            _buf, _start = bytearray(b"CHIRP"), 0  # the line starts here
        return
    start, _start, _skip = _start, -1, False
    if start < 0:
        _buf = bytearray()
        return
    line, _buf = memoryview(_buf), bytearray()
    try:
        line = str(line, "utf-8").strip()
    except UnicodeError:
        return  # not text, so not a setup line
    if line == "CHIRP?":
        _probed = True  # answered by service() once the input is read
    elif line.startswith("CHIRP+"):
        apply_config_push(line[6:].strip())


HWID_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-_."
NAME_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
HOST_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-"


def valid_hwid(value):
    # True when a pushed `hwid` is the platform's grammar: letters, digits and
    # : - _ . only, at most 128. Empty means no id.
    return len(value) <= 128 and all(c in HWID_CHARS for c in value)


def _controls(text):
    # A character below U+0020, or U+007F: never in a network's name or password.
    return any(c < " " or c == "\x7f" for c in text)


def fits(name, value):
    # True when a pushed text is within the contract's bounds: an ssid of 1 to
    # 32 bytes and a pass of up to 64, neither holding a control character; a
    # claim or token of 1 to 63 printable ASCII characters without the space;
    # a host of up to 63 letters, digits, . and -; an id in the platform's
    # grammar; and a key that will sign.
    if name == "ssid":
        return 0 < len(value.encode()) <= 32 and not _controls(value)
    if name == "pass":
        return len(value.encode()) <= 64 and not _controls(value)
    if name in ("claim", "token"):
        return 0 < len(value) <= 63 and all("!" <= c <= "~" for c in value)
    if name == "host":
        return len(value) <= 63 and all(c in HOST_CHARS for c in value)
    if name == "hwid":
        return valid_hwid(value)
    if name == "key":
        return usable_key(value)
    return True


def valid_networks(entries):
    # True when a pushed `aps` list is storable: 1 to MAX_NETWORKS entries,
    # each exactly an ssid and a pass, within their bounds.
    if not isinstance(entries, list) or not 0 < len(entries) <= MAX_NETWORKS:
        return False
    for entry in entries:
        if not (isinstance(entry, dict) and sorted(entry) == ["pass", "ssid"]
                and isinstance(entry["ssid"], str) and isinstance(entry["pass"], str)
                and fits("ssid", entry["ssid"]) and fits("pass", entry["pass"])):
            return False
    return True


def _merged(body, settings):
    # (a copy of the settings with the push merged in, None), or (None, the
    # reason it is refused), in the contract's order: not one JSON object; a
    # field this firmware does not act on (the first by name, so every board
    # names the same one); a bad value; then, in apply_config_push(), an
    # incomplete result. The settings themselves are never changed. A NUL,
    # raw or sent as \u0000, is bad-json wherever it is: a C string ends there.
    if "\x00" in body:
        return None, "bad-json"
    try:
        push = json.loads(body)
    except ValueError:
        return None, "bad-json"
    if not isinstance(push, dict) or _has_nul(push):
        return None, "bad-json"
    unknown = [name for name in push if name not in FIELDS]
    if unknown:
        # A name that is not a short plain word cannot go on the answer line.
        if not all(0 < len(n) <= 32 and all(c in NAME_CHARS for c in n) for n in unknown):
            return None, "bad-json"
        return None, "unsupported " + min(unknown)
    # One credential slot: a push carrying both a claim and a token is refused.
    if "claim" in push and "token" in push:
        return None, "bad-json"
    # aps is the whole list. A top-level ssid or pass sent with it must be its
    # first entry's.
    aps = push.get("aps")
    if "aps" in push and (not valid_networks(aps) or any(
            k in push and push[k] != aps[0][k] for k in ("ssid", "pass"))):
        return None, "bad-json"

    candidate = settings.copy()
    for name, value in push.items():
        if not isinstance(value, FIELDS[name]) or \
                (isinstance(value, str) and not fits(name, value)):
            return None, "bad-json"
        candidate["token" if name == "claim" else name] = value

    if aps:
        # Its first entry is the network tried first.
        candidate["ssid"], candidate["pass"] = aps[0]["ssid"], aps[0]["pass"]
    elif "ssid" in push:
        # A top-level ssid without aps replaces the stored network list.
        candidate.pop("aps", None)
    elif "pass" in push and candidate.get("aps"):
        # A pass alone is the password of the network tried first, which is
        # the list's first entry: it changes there too, and the list keeps
        # its other networks.
        first = dict(candidate["aps"][0])
        first["pass"] = push["pass"]
        candidate["aps"] = [first] + candidate["aps"][1:]
    # A new device does not sign with the old device's key: the key goes,
    # unless this same push brings one. A token, networks or a key alone
    # keep it.
    if _new_device(push, settings) and "key" not in push:
        candidate.pop("key", None)
    return candidate, None


def _has_nul(value):
    # True when a name or a text anywhere in the parsed push holds a NUL.
    if isinstance(value, dict):
        value = list(value.items())
    if isinstance(value, (list, tuple)):
        return any(_has_nul(x) for x in value)
    return isinstance(value, str) and "\x00" in value


def _new_device(push, settings):
    # True when a push sets the board up as a new device: it carries a claim
    # code, or an id other than the stored one. Ids are compared exactly, as
    # Warblet tells devices apart: "sensor" and "SENSOR" are two.
    hwid = push.get("hwid")
    return bool(push.get("claim")) or bool(hwid and hwid != settings.get("hwid", ""))


def apply_config_push(body):
    # Check a push and merge it into a copy of the settings, then save it and
    # restart. A refused push, or one that could not be stored, changes
    # nothing: ok is said only once the new settings are stored.
    candidate, refused = _merged(body, config)
    if refused:
        print("CHIRP= err " + refused)
        return
    if not ready(candidate):
        print("CHIRP= err missing-field")
        return
    try:
        save(candidate)
    except OSError:
        print("CHIRP= err store-failed")
        return
    # The live settings are left alone: the restart below reloads the file.
    print("CHIRP= ok")
    time.sleep_ms(200)  # let the ok leave the wire before the reset
    machine.reset()


def wait(seconds):
    # Wait this long, answering USB setup, and give the part its turn after
    # every pass: it does what is due (a command check, a ping) and says how
    # soon it wants the next, which ends the pass early enough. A turn running
    # when the time is up finishes first. Called from a command's handler, the
    # part gets no handler: USB setup, pings and reads go on, and no second
    # command is handed over inside the first.
    global _turn_at
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while True:
        left = time.ticks_diff(deadline, time.ticks_ms())
        if _upkeep:
            left = min(left, time.ticks_diff(_turn_at, time.ticks_ms()))
        service(max(0, left) / 1000)
        if _upkeep:
            _turn_at = time.ticks_add(time.ticks_ms(), int(_upkeep(None if _handling else _handler) * 1000))
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            return


# ---- Wi-Fi -----------------------------------------------------------------

def networks(settings):
    # Return (ssid, password) pairs in order, each pair once. One name with
    # two passwords is two networks to try: only an identical pair is dropped.
    pairs = []
    primary = settings.get("ssid")
    if primary:
        pairs.append((primary, settings.get("pass") or ""))
    for entry in settings.get("aps") or []:
        if not isinstance(entry, dict):
            continue
        pair = (entry.get("ssid"), entry.get("pass") or "")
        if pair[0] and pair not in pairs:
            pairs.append(pair)
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
        raise OSError("no clock: a signed message's nonce and a certificate's dates "
                      "need one -- allow NTP on this network")
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
