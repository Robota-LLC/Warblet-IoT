# Olimex revision C button reports, heartbeat and LED commands over HTTPS.
# The payload contains a press count and flags. Battery voltage is not measured.
import struct
import time

import machine

import warblet         # USB setup, Wi-Fi, the clock and signing
import warblet_https   # reports and commands over HTTPS

INTERVAL_S = 30      # heartbeat gap; a press does not wait for it
COMMAND_POLL_S = 5   # how often to ask Warblet for a command; a check takes about 1 s
BUTTON_POLL_INTERVAL_MS = 20   # how often we look at the button
DEBOUNCE_MS = 40     # a press must still read down this much later
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# What the setup page sees: this demo, and the release its files last changed in.
DEMO_SLUG = "demo-olimex-c3"
DEMO_VERSION = "v1.6.0"

# A message sent because of a press carries this tag, so a routine can
# react to presses without decoding the payload.
PRESS_TAG = "press"

# Revision C pins: GPIO8 is an active-low LED; GPIO9 is a button to ground.
# Use the internal button pull-up. Holding GPIO9 low at reset selects the bootloader.
LED_PIN = 8
LED_ACTIVE_LOW = True
BUTTON_PIN = 9
BUTTON_ACTIVE_LOW = True

led = None            # the LED, once open_led() has run
led_is_on = False     # what the last command asked for


def open_led():
    """The onboard LED, or None on a board that hasn't got one."""
    if LED_PIN is None:
        return None
    try:
        # A Signal turns "on" into the pin level the LED needs.
        led = machine.Signal(machine.Pin(LED_PIN, machine.Pin.OUT),
                             invert=LED_ACTIVE_LOW)
        led.off()  # start dark
        return led
    except Exception as e:
        print("chirp: no LED on pin", LED_PIN, "--", e)
        return None


def drive_led(led, on):
    """Light or darken the LED; ignore LED errors."""
    if led is None:
        return
    try:
        led.value(on)
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


def on_command(command):
    """Each command from the dashboard, as bytes. Match them as bytes and
    ignore unrecognized ones."""
    cmd = bytes(command)
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


def open_button():
    """BUT1 as an input with the pull-up it has no board resistor for."""
    try:
        # A Signal reads 1 while the button is held, whichever level that is.
        return machine.Signal(
            machine.Pin(BUTTON_PIN, machine.Pin.IN, machine.Pin.PULL_UP),
            invert=BUTTON_ACTIVE_LOW)
    except Exception as e:
        print("chirp: no button on pin", BUTTON_PIN, "--", e)
        return None


def pressed(button):
    """True while the button is held. False on a board without one."""
    if button is None:
        return False
    return button.value() == 1


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


def wait_for_press(button, was_up, seconds):
    """Between reports: watch the button while warblet.wait() answers USB
    setup and asks for commands. Return (press_detected, button_is_up) as
    soon as there is a press, or when the time is up."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        warblet.wait(BUTTON_POLL_INTERVAL_MS / 1000.0)
        new_press, was_up = check_button(button, was_up)
        if new_press:
            return True, was_up
    return False, was_up


def main():
    global led
    led = open_led()
    button = open_button()
    warblet.demo(DEMO_SLUG, DEMO_VERSION)
    warblet.on_command(on_command)
    warblet.check_commands_every(COMMAND_POLL_S)
    warblet.begin()       # the first time, waits here for USB setup

    presses = 0
    was_up = not pressed(button)
    on_press = False
    while True:
        if warblet.connected():
            flags = 0
            if pressed(button):
                flags |= FLAG_BUTTON_DOWN
            if on_press:
                flags |= FLAG_ON_PRESS
            tag = PRESS_TAG if on_press else None
            code = warblet.send(encode_button_report(presses, flags), tag)
            if 200 <= code < 300:
                print("chirp: sent presses={} flags=0x{:02x}{}".format(
                    presses, flags, " tag=" + tag if tag else ""))
                blink(led)

        if on_press:
            # Your own press clears your own light: the other board's
            # press turned it on. This runs once the press has been sent.
            set_led(led, False)
            print("chirp: own press -- LED OFF")

        # Check the button, commands and USB setup between reports.
        on_press, was_up = wait_for_press(button, was_up, INTERVAL_S)
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
