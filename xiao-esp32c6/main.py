# XIAO ESP32-C6 temperature reports and LED commands over MQTTS.
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
PROV_PASS_S = 0.5    # responder time before each association attempt

# Demo identifier included in the USB announce.
DEMO_SLUG = "demo-xiao-c6"

# XIAO C6 GPIO15 is active low. Check the pin map when moving to another board.
LED_PIN = 15
LED_ACTIVE_LOW = True


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


def set_led(led, on):
    """Set the LED state, ignoring LED errors."""
    if led is None:
        return
    try:
        led.value((0 if on else 1) if LED_ACTIVE_LOW else (1 if on else 0))
    except Exception:
        pass


def blink(led):
    """Blink once for the command, then leave the LED off."""
    set_led(led, True)
    time.sleep_ms(60)
    set_led(led, False)


def on_downlink(led, msg):
    """Match downlink commands as bytes and ignore unrecognized payloads."""
    cmd = bytes(msg)
    if cmd == b"led:on":
        set_led(led, True)
        print("chirp: down led:on -- LED ON")
    elif cmd == b"led:off":
        set_led(led, False)
        print("chirp: down led:off -- LED OFF")
    elif cmd == b"led:blink":
        blink(led)
        print("chirp: down led:blink -- LED blinked")
    else:
        print("chirp: down ignored:", cmd)


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


def wait_and_handle_commands(config, seconds):
    """Service USB setup and poll MQTT during the reporting interval."""
    deadline = time.ticks_add(time.ticks_ms(), int(seconds * 1000))
    while time.ticks_diff(deadline, time.ticks_ms()) > 0:
        chirp_prov.service(config, COMMAND_POLL_INTERVAL_MS / 1000.0)
        chirp_mqtt.poll()


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
                chirp_mqtt.publish(config, encode_temperature(celsius))
                # QoS 0: nothing confirms the platform accepted the publish.
                print("chirp: publish attempted, {:.1f} C".format(celsius))
            except Exception as e:
                # Close so the next pass reconnects.
                print("chirp: mqtt failed:", e)
                chirp_mqtt.close()

        wait_and_handle_commands(config, INTERVAL_S)


main()
