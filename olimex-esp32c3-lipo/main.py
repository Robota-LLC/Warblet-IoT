# Olimex revision C button reports, heartbeat and LED commands over HTTPS.
# The payload contains a press count and flags. Battery voltage is not measured.
import struct
import time

import machine
import network

import chirp_prov
import chirp_send

INTERVAL_S = 30      # heartbeat gap; a press does not wait for it
COMMAND_POLL_S = 5   # wait after each command check; a check takes about 1 s
WIFI_TIMEOUT_S = 12  # per-attempt association wait
BUTTON_POLL_INTERVAL_MS = 20   # how often we look at the button
DEBOUNCE_MS = 40     # a press must still read down this much later
PROV_PASS_S = 0.5    # responder time before each association attempt
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-olimex-c3"

# A message sent because of a press carries this tag, so a routine can
# react to presses without decoding the payload.
PRESS_TAG = "press"

# Revision C pins: GPIO8 is an active-low LED; GPIO9 is a button to ground.
# Use the internal button pull-up. Holding GPIO9 low at reset selects the bootloader.
LED_PIN = 8
LED_ACTIVE_LOW = True
BUTTON_PIN = 9
BUTTON_ACTIVE_LOW = True

led_is_on = False     # what the last command asked for


def open_led():
    """The onboard LED, or None on a board that hasn't got one."""
    if LED_PIN is None:
        return None
    try:
        led = machine.Pin(LED_PIN, machine.Pin.OUT)
        led.value(1 if LED_ACTIVE_LOW else 0)  # start dark
        return led
    except Exception as e:
        print("chirp: no LED on pin", LED_PIN, "--", e)
        return None


def drive_led(led, on):
    """Light or darken the LED; ignore LED errors."""
    if led is None:
        return
    try:
        led.value((0 if on else 1) if LED_ACTIVE_LOW else (1 if on else 0))
    except Exception:
        pass


def set_led(led, on):
    """Turn the LED on or off and remember it."""
    global led_is_on
    led_is_on = on
    drive_led(led, on)


def blink(led):
    """Flip the LED for a moment, then put it back the way it was."""
    drive_led(led, not led_is_on)
    time.sleep_ms(60)
    drive_led(led, led_is_on)


def on_downlink(led, msg):
    """Match commands as bytes and ignore unrecognized ones."""
    cmd = bytes(msg)
    if cmd == b"led:on":
        set_led(led, True)
    elif cmd == b"led:off":
        set_led(led, False)
    elif cmd == b"led:toggle":
        set_led(led, not led_is_on)
    elif cmd == b"led:blink":
        blink(led)
    else:
        # A command can carry anything, so print its size, never its bytes.
        print("chirp: down ignored:", len(cmd), "bytes")
        return
    print("chirp: down", cmd.decode(), "-- LED", "ON" if led_is_on else "OFF")


def check_for_command(config, led):
    """Ask Warblet for one waiting command and act on it. Another waiting
    command comes at the next check."""
    if not network.WLAN(network.STA_IF).isconnected():
        return
    try:
        code, command = chirp_send.poll(config)
    except Exception as e:
        print("chirp: command check failed:", e)
        return
    if code == 200 and command is not None:
        on_downlink(led, command)
    elif code not in (200, 204):
        print("chirp: command check replied", code)


def open_button():
    """BUT1 as an input with the pull-up it has no board resistor for."""
    try:
        return machine.Pin(BUTTON_PIN, machine.Pin.IN, machine.Pin.PULL_UP)
    except Exception as e:
        print("chirp: no button on pin", BUTTON_PIN, "--", e)
        return None


def pressed(button):
    """True while the button is held. False on a board without one."""
    if button is None:
        return False
    return (button.value() == 0) if BUTTON_ACTIVE_LOW else (button.value() == 1)


def check_button(button, was_up):
    """Return (new_press, is_up). A press counts once: the button went down
    after being up, and still reads down DEBOUNCE_MS later, so contact
    bounce or a glitch is not a press."""
    up = not pressed(button)
    if was_up and not up:
        time.sleep_ms(DEBOUNCE_MS)  # let the contact settle, then look again
        up = not pressed(button)
        return not up, up
    return False, up


# Payload flags must match decoder.star.
FLAG_BUTTON_DOWN = 0x01  # the button is held as this message is built
FLAG_ON_PRESS = 0x02     # this message exists because of a press, not the timer


def encode_button_report(presses, flags):
    """Encode the press count as a big-endian uint16, followed by the flags
    byte. Clamp the transmitted count at 65535."""
    if presses < 0:
        presses = 0
    elif presses > 0xFFFF:
        presses = 0xFFFF
    return struct.pack(">HB", presses, flags & 0xFF)


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

    wlan.disconnect()  # drop the half-open attempt so the next try starts clean
    print("chirp: no join to", ssid, "within", WIFI_TIMEOUT_S, "s")
    return False


def connect_wifi(config):
    """Try the stored networks in order. True once we have an address.
    Called before every reading, so the ladder reruns by itself when the link drops."""
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return True

    for ssid, password in chirp_prov.networks(config):
        # Answer USB setup before the blocking join.
        chirp_prov.service(config, PROV_PASS_S)
        if join(wlan, ssid, password):
            return True

    print("chirp: no stored network joined -- will retry")
    return False


def wait_for_press(config, led, button, was_up, seconds):
    """Between reports: answer USB setup, watch the button, and ask for a
    command every COMMAND_POLL_S. Return (press_detected, button_is_up)
    as soon as there is a press, or when the time is up."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    next_check = time.ticks_ms()
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        chirp_prov.service(config, BUTTON_POLL_INTERVAL_MS / 1000.0)
        new_press, was_up = check_button(button, was_up)
        if new_press:
            return True, was_up
        if time.ticks_diff(time.ticks_ms(), next_check) >= 0:
            check_for_command(config, led)
            next_check = time.ticks_add(time.ticks_ms(), COMMAND_POLL_S * 1000)
    return False, was_up


def main():
    led = open_led()
    button = open_button()
    chirp_prov.set_slug(DEMO_SLUG)
    config = chirp_prov.load()

    # Wait for USB setup. A successful config push restarts the board.
    if not chirp_prov.ready(config):
        print("chirp: not provisioned -- waiting on USB, warbletiot.com/flash")
        while True:
            chirp_prov.service(config, 1)

    print("chirp: reporting as", config["hwid"],
          "signed" if config.get("key") else "token only")

    presses = 0
    was_up = not pressed(button)
    on_press = False
    clock_sync_attempted = False
    while True:
        if connect_wifi(config):
            if not clock_sync_attempted:
                # One try only: without NTP the board signs without a nonce.
                chirp_send.sync_clock()
                clock_sync_attempted = True
            flags = 0
            if pressed(button):
                flags |= FLAG_BUTTON_DOWN
            if on_press:
                flags |= FLAG_ON_PRESS
            tag = PRESS_TAG if on_press else None
            try:
                code = chirp_send.send(config, encode_button_report(presses,
                                                                 flags), tag)
                if 200 <= code < 300:
                    print("chirp: sent presses={} flags=0x{:02x}{}".format(
                        presses, flags, " tag=" + tag if tag else ""))
                    blink(led)
                else:
                    print("chirp: ingest replied", code)
            except Exception as e:
                print("chirp: send failed:", e)

        if on_press:
            # Your own press clears your own light: the other board's
            # press turned it on. This runs once the press has been sent.
            set_led(led, False)
            print("chirp: own press -- LED OFF")

        # Check the button, commands and USB setup between reports.
        on_press, was_up = wait_for_press(config, led, button, was_up,
                                          INTERVAL_S)
        if on_press:
            presses += 1


try:
    main()
except KeyboardInterrupt:
    raise  # Ctrl-C stops the app on purpose: USB setup and bench tools use it
except Exception as e:
    # An error the loop did not expect. Say what it was, then start again
    # rather than sit silent at the prompt.
    print("chirp: stopped by an unexpected error:", repr(e))
    print("chirp: restarting in", RESTART_AFTER_ERROR_S, "s")
    time.sleep(RESTART_AFTER_ERROR_S)
    machine.reset()
