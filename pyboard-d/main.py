# Pyboard D SF2W chip-temperature reports over plain HTTP.
import struct
import time

import machine
import network
import pyb

import warblet        # USB setup, Wi-Fi, the clock and signing
import warblet_http   # enables reports over plain HTTP; keep this import

INTERVAL_S = 30      # seconds between readings
RESTART_AFTER_ERROR_S = 10  # pause before restarting after an unexpected error

# What the setup page sees: this demo, and the release its files last changed in.
DEMO_SLUG = "demo-pyboard-d"
DEMO_VERSION = "v1.6.0"

# SF2W RGB LED channels: 1 red, 2 green, 3 blue.
LED_OK = 2
LED_ERR = 1

# The internal-channel mask for pyb.ADCAll. See open_sensor().
ADC_INTERNAL_MASK = 0x70000

# The Wi-Fi chip's power saving, off. On the bench, with its default
# (0xa11142), about one connection in five was answered late (3 s or more),
# and on one access point some failed; with power saving off, none failed.
# Rejoins kept the setting and only a reset brought the default back, so it
# is checked before every report. The board runs on USB here; on a battery,
# power saving off draws more.
WIFI_PM = 0xa11140


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


def encode_temperature(celsius):
    """Encode signed tenths of Celsius in two big-endian bytes. Clamp values to
    the int16 range and keep decoder.star consistent."""
    temperature_tenths_c = max(-32768, min(32767, int(round(celsius * 10.0))))
    return struct.pack(">h", temperature_tenths_c)


def power_saving_off():
    """Wi-Fi power saving off, unless it already is."""
    wlan = network.WLAN(network.STA_IF)
    if wlan.config("pm") != WIFI_PM:
        wlan.config(pm=WIFI_PM)
        print("chirp: Wi-Fi power saving off")


def main():
    ok_led = open_led(LED_OK)
    err_led = open_led(LED_ERR)
    warblet.demo(DEMO_SLUG, DEMO_VERSION)
    warblet.begin()       # the first time, waits here for USB setup

    adc = open_sensor()
    while True:
        if warblet.connected():
            power_saving_off()
            celsius = read_temperature_c(adc)
            if warblet.send(encode_temperature(celsius)):
                print("chirp: sent {:.1f} C".format(celsius))
                blink(ok_led)
            else:
                blink(err_led)   # the console has said why

        # Handle USB setup between reports.
        warblet.wait(INTERVAL_S)


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
