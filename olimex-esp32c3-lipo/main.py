# Olimex revision C button reports and heartbeat over HTTPS.
# The payload contains a press count and flags. Battery voltage is not measured.
import struct
import time

import machine
import network

import chirp_prov
import chirp_send

INTERVAL_S = 30      # heartbeat gap; a press does not wait for it
WIFI_TIMEOUT_S = 12  # per-attempt association wait
BUTTON_POLL_INTERVAL_MS = 200  # how often we look at the button
DEBOUNCE_MS = 40     # a contact settles well inside this
PROV_PASS_S = 0.5    # responder time before each association attempt

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-olimex-c3"

# Revision C pins: GPIO8 is an active-low LED; GPIO9 is a button to ground.
# Use the internal button pull-up. Holding GPIO9 low at reset selects the bootloader.
LED_PIN = 8
LED_ACTIVE_LOW = True
BUTTON_PIN = 9
BUTTON_ACTIVE_LOW = True

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


def blink(led):
    """Flash the LED after an accepted message; ignore LED errors."""
    if led is None:
        return
    try:
        led.value(0 if LED_ACTIVE_LOW else 1)
        time.sleep_ms(60)
        led.value(1 if LED_ACTIVE_LOW else 0)
    except Exception:
        pass


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


def wait_for_press(config, button, was_down, seconds):
    """Poll for a debounced press while servicing USB setup.
    Return (press_detected, button_is_down); a held button counts once."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        chirp_prov.service(config, BUTTON_POLL_INTERVAL_MS / 1000.0)
        down = pressed(button)
        if down and not was_down:
            time.sleep_ms(DEBOUNCE_MS)
            if pressed(button):        # still down: a real press, not a bounce
                return True, True
            down = False
        was_down = down
    return False, was_down


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
    was_down = pressed(button)
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
            try:
                code = chirp_send.send(config, encode_button_report(presses,
                                                                 flags))
                if 200 <= code < 300:
                    print("chirp: sent presses={} flags=0x{:02x}".format(
                        presses, flags))
                    blink(led)
                else:
                    print("chirp: ingest replied", code)
            except Exception as e:
                print("chirp: send failed:", e)

        # Check the button and handle USB setup commands between reports.
        on_press, was_down = wait_for_press(config, button, was_down, INTERVAL_S)
        if on_press:
            presses += 1


main()
