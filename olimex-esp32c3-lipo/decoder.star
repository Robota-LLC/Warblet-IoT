# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# Two unsigned big-endian bytes hold presses; the third byte holds flags:
# 0x01 is button_down and 0x02 is on_press.

def decode(payload, meta):
    presses = int(payload[0]) << 8 | int(payload[1])
    flags = int(payload[2])
    return {
        "presses": presses,
        "button_down": flags & 0x01 != 0,
        "on_press": flags & 0x02 != 0,
    }
