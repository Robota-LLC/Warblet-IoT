# Report ESP32 chip temperature over HTTPS.
# Check LED_PIN for your board; None disables it.
# A missing temperature API skips the reading.
import struct
import time

import machine
import network

import chirp_prov
import chirp_send

INTERVAL_S = 30      # seconds between readings
WIFI_TIMEOUT_S = 12  # per-attempt association wait
PROV_PASS_S = 0.5    # responder time before each association attempt

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-esp32-generic"

# A valid GPIO number does not mean an LED is connected there.
LED_PIN = 2
LED_ACTIVE_LOW = False


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

        # Handle USB setup commands between reports.
        chirp_prov.service(config, INTERVAL_S)


main()
