# XIAO ESP32-C6 temperature reports, a button and LED commands over MQTTS.
# The transport module manages TLS, credentials, and optional signatures.
import gc
import struct
import time

import machine
import network

import chirp_mqtt
import chirp_prov

INTERVAL_S = 30      # seconds between readings
WIFI_TIMEOUT_S = 12  # per-attempt association wait
COMMAND_POLL_INTERVAL_MS = 200  # gap between command checks; network work can add delay
BUTTON_POLL_INTERVAL_MS = 20    # how often we look at the button
DEBOUNCE_MS = 40     # a press must still read down this much later
PROV_PASS_S = 0.5    # responder time before each association attempt
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-xiao-c6"

# A reading sent because of a press carries this tag, so a routine can
# react to presses without decoding the payload.
PRESS_TAG = "press"

# XIAO C6 GPIO15 is active low. Check the pin map when moving to another board.
LED_PIN = 15
LED_ACTIVE_LOW = True
# The BOOT button is GPIO9, to ground. Holding it during reset selects the bootloader.
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
    """Match downlink commands as bytes and ignore unrecognized payloads."""
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


def open_button():
    """The BOOT button as an input with the internal pull-up."""
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


def read_temperature_c():
    """The C6's own die temperature, in degrees Celsius."""
    import esp32
    return float(esp32.mcu_temperature())


def encode_temperature(celsius):
    """Encode signed tenths of Celsius in two big-endian bytes. Clamp values to
    the int16 range and keep decoder.star consistent."""
    temperature_tenths_c = int(round(celsius * 10.0))
    if temperature_tenths_c < -32768:
        temperature_tenths_c = -32768
    elif temperature_tenths_c > 32767:
        temperature_tenths_c = 32767
    return struct.pack(">h", temperature_tenths_c)


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


def wait_and_handle_commands(config, button, was_up, seconds):
    """Service USB setup, poll MQTT and watch the button during the reporting
    interval. Return (press_detected, button_is_up) as soon as there is a
    press, or when the time is up."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    next_check = time.ticks_ms()
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        chirp_prov.service(config, BUTTON_POLL_INTERVAL_MS / 1000.0)
        if time.ticks_diff(time.ticks_ms(), next_check) >= 0:
            chirp_mqtt.poll()
            next_check = time.ticks_add(time.ticks_ms(), COMMAND_POLL_INTERVAL_MS)
        new_press, was_up = check_button(button, was_up)
        if new_press:
            return True, was_up
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

    was_up = not pressed(button)
    on_press = False
    while True:
        if connect_wifi(config):
            try:
                if not chirp_mqtt.connected():
                    # connect() sets the clock before opening TLS.
                    gc.collect()
                    heap_before_connect_bytes = gc.mem_free()
                    connect_started_ms = time.ticks_ms()
                    chirp_mqtt.connect(config,
                                       lambda msg: on_downlink(led, msg))
                    connect_duration_ms = time.ticks_diff(time.ticks_ms(),
                                                          connect_started_ms)
                    gc.collect()
                    # What the TLS session costs in time and heap; a port to a smaller part needs this.
                    print("chirp: {} session up on port {}, {} -- {} ms, "
                          "heap {} -> {}".format(
                              "mqtts" if chirp_mqtt.use_tls(config) else "mqtt",
                              chirp_mqtt.port_for(config),
                              chirp_mqtt.topic_up(config["hwid"]),
                              connect_duration_ms, heap_before_connect_bytes,
                              gc.mem_free()))
                celsius = read_temperature_c()
                tag = PRESS_TAG if on_press else None
                chirp_mqtt.publish(config, encode_temperature(celsius), tag)
                # QoS 0: nothing confirms the platform accepted the publish.
                print("chirp: publish attempted, {:.1f} C{}".format(
                    celsius, " tag=" + tag if tag else ""))
            except Exception as e:
                # Close so the next pass reconnects.
                print("chirp: mqtt failed:", e)
                chirp_mqtt.close()

        if on_press:
            # Your own press clears your own light: the other board's
            # press turned it on. This runs once the press has been sent.
            set_led(led, False)
            print("chirp: own press -- LED OFF")

        on_press, was_up = wait_and_handle_commands(config, button, was_up,
                                                    INTERVAL_S)


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
