# Warblet desk display for the Cheap Yellow Display (ESP32-2432S028R).
# Type in the Message box on its Warblet device page and the words fill the
# screen. Tap the screen and Warblet hears that the message was seen.
# A routine can send a line for the lower part of the screen ("info:...");
# the one that comes with this demo sends how full your computer's disk is.
import gc
import json
import time

import machine
import network

import chirp_prov
import chirp_send
import screen

DEMO_SLUG = "demo-cyd-desk-display"
REPORT_EVERY_S = 30   # a report with the Wi-Fi signal; it also shows Warblet the board is there
CHECK_EVERY_S = 5     # how often to ask Warblet for new messages
MOST_PER_CHECK = 10   # messages taken in one check, so a long queue cannot hold up the loop
WIFI_TIMEOUT_S = 12   # per attempt to join a network
RESTART_AFTER_ERROR_S = 10

# Tags say what a message is. The first report after a start says hello, so
# the routine can give a display that just started the last info line again.
HELLO_TAG = "hello"
SEEN_TAG = "seen"
OLD_INFO_S = 600      # the info line turns grey when nothing new has come for this long

# Colours: red, green, blue, each 0-255.
BLACK = screen.rgb(0, 0, 0)
BAR = screen.rgb(30, 30, 40)
WHITE = screen.rgb(255, 255, 255)
GREY = screen.rgb(130, 130, 130)
YELLOW = screen.rgb(255, 210, 0)
GREEN = screen.rgb(0, 210, 90)
AMBER = screen.rgb(255, 150, 0)
RED = screen.rgb(240, 50, 50)

# Where things go on the 320 x 240 screen: a strip at the top, the message,
# two lines for the routine's info, two lines at the bottom.
MESSAGE_TOP, MESSAGE_HEIGHT = 24, 128
INFO_TOP = 156
FOOTER_TOP = 200

message = ""          # what was typed on the dashboard
message_at = 0        # when it came (seconds)
info = ""             # the routine's line
info_at = 0
seen = ""             # "", "sending", "sent" or "waiting" (tapped while offline)
drawn = {}            # what each line of the screen shows now


def now():
    return time.time()


def age(then):
    """How long ago, in a few words."""
    s = max(0, now() - then)
    if s < 60:
        return "just now"
    if s < 3600:
        return "%d min ago" % (s // 60)
    if s < 48 * 3600:
        return "%d h ago" % (s // 3600)
    return "%d days ago" % (s // 86400)


def wrap(words, width):
    """Split words into lines of at most width letters."""
    lines, line = [], ""
    for word in words.split():
        while len(word) > width:            # a word longer than a line is cut
            if line:
                lines.append(line)
                line = ""
            lines.append(word[:width])
            word = word[width:]
        if not line:
            line = word
        elif len(line) + 1 + len(word) <= width:
            line += " " + word
        else:
            lines.append(line)
            line = word
    if line:
        lines.append(line)
    return lines


def put(y, words, colour, background=BLACK):
    """One line of 20 big letters at height y, drawn only when it changes,
    so the screen does not flicker."""
    if drawn.get(y) == (words, colour):
        return
    drawn[y] = (words, colour)
    screen.fill(0, y, screen.WIDTH, 18, background)
    screen.text(4 if background == BAR else 0, y + 1, words[:20], 2, colour, background)


def show_status(words, colour):
    """The strip at the top: how the board is doing."""
    put(0, words, colour, BAR)


def show_message(words, colour=WHITE):
    """The middle: the message, as big as it fits."""
    screen.fill(0, MESSAGE_TOP, screen.WIDTH, MESSAGE_HEIGHT, BLACK)
    for size in (4, 3, 2, 1):
        pitch = 9 * size + 1                # the letters, and a gap between lines
        lines = wrap(words, screen.WIDTH // (8 * size))
        if len(lines) * pitch <= MESSAGE_HEIGHT:
            break
    for i, line in enumerate(lines[:MESSAGE_HEIGHT // pitch]):
        screen.text(0, MESSAGE_TOP + i * pitch, line, size, colour, BLACK)


def show_info():
    """Below it: the routine's line, grey once it is old."""
    colour = YELLOW if now() - info_at < OLD_INFO_S else GREY
    lines = wrap(info, 20) + ["", ""]
    put(INFO_TOP, lines[0], colour)
    put(INFO_TOP + 20, lines[1], colour)


def show_footer():
    """The bottom: when the message came, and whether it was seen."""
    if not message:
        put(FOOTER_TOP, "", GREY)
        put(FOOTER_TOP + 20, "", WHITE)
        return
    put(FOOTER_TOP, "Message " + age(message_at), GREY)
    words = {"": "Tap: I've seen it", "sending": "Seen: sending",
             "sent": "Seen: sent", "waiting": "Seen: not sent yet"}[seen]
    put(FOOTER_TOP + 20, words, GREEN if seen == "sent" else WHITE)


def show_waiting(config):
    show_message("Type a message for " + config["hwid"] + " on your Warblet dashboard.", GREY)


def text_of(raw):
    """Bytes from Warblet as text. Bytes that are not text keep the letters
    the screen can show."""
    try:
        return raw.decode().strip()
    except UnicodeError:
        return "".join(chr(b) if 32 <= b < 127 else "?" for b in raw).strip()


def on_command(command, config):
    """A message from Warblet, matched as bytes: "clear" clears the screen,
    "info:..." sets the lower lines, anything else is a message to show big."""
    global message, message_at, info, info_at, seen
    if command == b"clear":
        message, seen = "", ""
        show_waiting(config)
    elif command.startswith(b"info:"):
        info, info_at = text_of(command[5:]), now()
        show_info()
    else:
        message, message_at, seen = text_of(command), now(), ""
        show_message(message)
    show_footer()
    print("chirp: down", len(command), "bytes")   # its size, never its words


def check_messages(config):
    """Take every message waiting (up to MOST_PER_CHECK) and show each.
    Returns the HTTP status of the last check."""
    for _ in range(MOST_PER_CHECK):
        code, command = chirp_send.poll(config)
        if code != 200:
            return code
        if command is not None:
            on_command(command, config)
    return 200


def report(config, tag=None, body=None):
    """Send one message up. Returns the HTTP status."""
    if body is None:
        body = {"wifi_dbm": network.WLAN(network.STA_IF).status("rssi")}
    code = chirp_send.send(config, json.dumps(body).encode(), tag)
    gc.collect()
    print("chirp: sent", tag or "report", "->", code, "free", gc.mem_free())
    return code


def send_seen(config):
    """Tell Warblet the message on screen was seen."""
    global seen
    seen = "sending"
    show_footer()
    try:
        ok = 200 <= report(config, SEEN_TAG, {"seen": message[:60]}) < 300
    except Exception as e:
        print("chirp: seen not sent:", e)
        ok = False
    seen = "sent" if ok else "waiting"      # "waiting" is tried again at the next check
    show_footer()


def wait(config, seconds, online):
    """Between checks: answer USB setup and watch for a tap."""
    global seen
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    pressed = False
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        chirp_prov.service(config, 0.05)
        touch = screen.touched()
        if touch and not pressed and message and seen in ("", "waiting"):
            if online:
                send_seen(config)
            else:
                seen = "waiting"            # sent once the board is back online
                show_footer()
        pressed = touch


def join(wlan, ssid, password):
    """One attempt at one network. True once we have an address."""
    print("chirp: joining", ssid)
    try:
        wlan.connect(ssid, password)
    except OSError as e:
        print("chirp: connect failed:", e)
        return False
    deadline = time.ticks_add(time.ticks_ms(), WIFI_TIMEOUT_S * 1000)
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        if wlan.isconnected():
            print("chirp: wifi up", wlan.ifconfig()[0])
            return True
        time.sleep_ms(250)
    wlan.disconnect()                       # drop the half-open attempt
    return False


def connect_wifi(config):
    """Try the stored networks in order. True once we have an address."""
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return True
    show_status("Joining Wi-Fi", AMBER)
    for ssid, password in chirp_prov.networks(config):
        chirp_prov.service(config, 0.5)     # answer USB setup before the wait
        if join(wlan, ssid, password):
            return True
    show_status("No Wi-Fi, retrying", AMBER)
    return False


def check_in(config, said_hello):
    """Report when it is time, take the messages, send a waiting "seen".
    Returns the HTTP status of the message check, and whether hello was said."""
    global last_report
    if last_report is None or time.ticks_diff(time.ticks_ms(), last_report) >= REPORT_EVERY_S * 1000:
        code = report(config, None if said_hello else HELLO_TAG)
        said_hello = said_hello or 200 <= code < 300
        last_report = time.ticks_ms()
    code = check_messages(config)
    if seen == "waiting":
        send_seen(config)
    return code, said_hello


last_report = None


def main():
    chirp_prov.set_slug(DEMO_SLUG)
    print(screen.start())
    config = chirp_prov.load()
    if not chirp_prov.ready(config):
        show_status("Not set up", RED)
        show_message("Set me up at warbletiot.com/flash")
        print("chirp: not provisioned -- waiting on USB, warbletiot.com/flash")
        while True:
            chirp_prov.service(config, 1)

    show_waiting(config)
    clock_set = False
    said_hello = False
    while True:
        online = connect_wifi(config)
        if online:
            if not clock_set:
                chirp_send.sync_clock()     # the server's certificate needs the date
                clock_set = True
            try:
                code, said_hello = check_in(config, said_hello)
                if code == 401:
                    show_status("Warblet refused 401", RED)
                elif code == 429:
                    show_status("Warblet: slow down", AMBER)
                elif code in (200, 204):
                    show_status("Connected", GREEN)
                else:
                    show_status("Warblet: no answer", AMBER)
            except Exception as e:          # no answer, TLS refused, no DNS...
                print("chirp: check failed:", e)
                show_status("Warblet: no answer", AMBER)
        show_footer()                       # the ages move on
        if info:
            show_info()
        wait(config, CHECK_EVERY_S, online)


def stopped():
    """Say so on the screen, so a stopped board never shows Connected."""
    try:
        show_status("Stopped", RED)
    except Exception:
        pass                                # the screen never started


try:
    main()
except KeyboardInterrupt:
    stopped()
    raise                                   # Ctrl-C stops the app on purpose: USB setup uses it
except Exception as e:
    print("chirp: stopped by an unexpected error:", repr(e))
    stopped()
    print("chirp: restarting in", RESTART_AFTER_ERROR_S, "s")
    time.sleep(RESTART_AFTER_ERROR_S)
    machine.reset()
