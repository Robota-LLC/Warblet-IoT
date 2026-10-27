# Pyboard D SF2W chip-temperature reports over HTTP.
import struct
import time

import machine
import pyb
import ubinascii
import uhashlib

import chirp_prov
import chirp_send

INTERVAL_S = 30      # seconds between readings
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-pyboard-d"

# SF2W RGB LED channels: 1 red, 2 green, 3 blue.
LED_OK = 2
LED_ERR = 1

# The internal-channel mask for pyb.ADCAll. See open_sensor().
ADC_INTERNAL_MASK = 0x70000


def open_led(index):
    """One onboard LED channel, or None if this board hasn't got it."""
    if index is None:
        return None
    try:
        led = pyb.LED(index)
        led.off()
        return led
    except Exception as e:
        print("chirp: no LED", index, "--", e)
        return None


def blink(led):
    """Flash the status LED, ignoring LED errors."""
    if led is None:
        return
    try:
        led.on()
        time.sleep_ms(60)
        led.off()
    except Exception:
        pass


def open_sensor():
    """Open internal ADC channels only. Omitting the mask also configures external pins."""
    return pyb.ADCAll(12, ADC_INTERNAL_MASK)


def read_temperature_c(adc):
    """The F722's own die temperature, in degrees Celsius."""
    return float(adc.read_core_temp())


def suggest_hwid():
    """Suggest a pybd-xxxxxx ID from a hash of the full chip UID.
    The configured hardware ID is used for reports; this short hash can collide."""
    try:
        h = uhashlib.sha256()
        h.update(machine.unique_id())
        return "pybd-" + ubinascii.hexlify(h.digest()[:3]).decode()
    except Exception:
        return "pybd-unknown"


def encode_temperature(celsius):
    """Encode signed tenths of Celsius in two big-endian bytes. Clamp values to
    the int16 range and keep decoder.star consistent."""
    temperature_tenths_c = int(round(celsius * 10.0))
    if temperature_tenths_c < -32768:
        temperature_tenths_c = -32768
    elif temperature_tenths_c > 32767:
        temperature_tenths_c = 32767
    return struct.pack(">h", temperature_tenths_c)


def main():
    ok_led = open_led(LED_OK)
    err_led = open_led(LED_ERR)
    chirp_prov.set_slug(DEMO_SLUG)
    config = chirp_prov.load()

    # Wait for USB setup. A successful config push restarts the board.
    if not chirp_prov.ready(config):
        print("chirp: not provisioned -- waiting on USB, warbletiot.com/flash")
        print("chirp: suggested hwid for this board:", suggest_hwid())
        while True:
            chirp_prov.service(config, 1)

    print("chirp: reporting as", config["hwid"],
          "signed" if config.get("key") else "token only")

    adc = open_sensor()
    while True:
        if chirp_prov.connect_wifi(config):
            try:
                celsius = read_temperature_c(adc)
                code = chirp_send.send(config, encode_temperature(celsius))
                if 200 <= code < 300:
                    print("chirp: sent {:.1f} C".format(celsius))
                    blink(ok_led)
                else:
                    print("chirp: ingest replied", code)
                    blink(err_led)
            except Exception as e:
                print("chirp: send failed:", e)
                blink(err_led)

        # Handle USB setup commands between reports.
        chirp_prov.service(config, INTERVAL_S)


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
