# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# Two unsigned big-endian bytes hold the simulated temperature in tenths
# of Celsius: 00c9 is 20.1 C, returned as temp_c. Negative readings need
# signed packing and decoding.

def decode(payload, meta):
    t = int(payload[0]) << 8 | int(payload[1])
    return {"temp_c": t / 10.0}
