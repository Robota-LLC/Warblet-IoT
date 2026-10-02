# Report ESP32 chip temperature over HTTPS and act on LED commands.
# Check LED_PIN for your board; None disables it.
# A missing temperature API skips the reading.
import struct
import time

import machine
import network

import chirp_prov
import chirp_send

INTERVAL_S = 30      # seconds between readings
COMMAND_POLL_S = 5   # wait after each command check; a check is one HTTPS request
WIFI_TIMEOUT_S = 12  # per-attempt association wait
PROV_PASS_S = 0.5    # responder time before each association attempt
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-esp32-generic"

# A valid GPIO number does not mean an LED is connected there.
LED_PIN = 2
LED_ACTIVE_LOW = False

led_is_on = False  # what the last command asked for


def open_led():
    """The status LED, or None on a board that hasn't got one."""
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


def wait_and_handle_commands(config, led, seconds):
    """Between readings: answer USB setup and ask for a command every
    COMMAND_POLL_S."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        check_for_command(config, led)
        left_s = time.ticks_diff(deadline, time.ticks_ms()) / 1000.0
        chirp_prov.service(config, max(0, min(COMMAND_POLL_S, left_s)))


def read_temperature_c():
    """Read chip temperature in Celsius, or return None when unavailable.
    Convert the classic ESP32 Fahrenheit API to Celsius."""
    try:
        import esp32
    except ImportError:
        return None

    try:
        return float(esp32.mcu_temperature())
    except (AttributeError, OSError, ValueError):
        pass
    try:
        return (float(esp32.raw_temperature()) - 32.0) / 1.8
    except (AttributeError, OSError, ValueError):
        pass
    return None


def encode_temperature(celsius):
    """Encode Celsius in tenths as a signed big-endian int16."""
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


def main():
    led = open_led()
    chirp_prov.set_slug(DEMO_SLUG)
    config = chirp_prov.load()

    # Wait for USB setup. A successful config push restarts the board.
    if not chirp_prov.ready(config):
        print("chirp: not provisioned -- waiting on USB, warbletiot.com/flash")
        while True:
            chirp_prov.service(config, 1)

    print("chirp: reporting as", config["hwid"],
          "signed" if config.get("key") else "token only")

    clock_sync_attempted = False
    while True:
        if connect_wifi(config):
            if not clock_sync_attempted:
                # One try only: without NTP the board signs without a nonce.
                chirp_send.sync_clock()
                clock_sync_attempted = True
            celsius = read_temperature_c()
            if celsius is None:
                print("chirp: this chip has no die temperature -- nothing to "
                      "send. Give read_temperature_c() a real sensor.")
            else:
                try:
                    code = chirp_send.send(config,
                                           encode_temperature(celsius))
                    if 200 <= code < 300:
                        print("chirp: sent {:.1f} C".format(celsius))
                        blink(led)
                    else:
                        print("chirp: ingest replied", code)
                except Exception as e:
                    print("chirp: send failed:", e)

        # Check for commands and USB setup between reports.
        wait_and_handle_commands(config, led, INTERVAL_S)


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
