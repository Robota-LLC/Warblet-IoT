# Decode an unsigned big-endian press count followed by flags.
# 0x01: button_down. 0x02: on_press.

def decode(payload, meta):
    presses = int(payload[0]) << 8 | int(payload[1])
    flags = int(payload[2])
    return {
        "presses": presses,
        "button_down": flags & 0x01 != 0,
        "on_press": flags & 0x02 != 0,
    }
