# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# Two unsigned big-endian bytes hold tenths of Celsius, returned as temp_c.
# Negative readings need signed packing and decoding.

def decode(payload, meta):
    return {"temp_c": (int(payload[0]) << 8 | int(payload[1])) / 10.0}
