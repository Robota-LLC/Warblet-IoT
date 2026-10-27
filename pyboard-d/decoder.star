# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# Two signed big-endian bytes hold tenths of Celsius: 00d7 is 21.5 C;
# ffce is -5.0 C. The output field is temp_c.

def decode(payload, meta):
    t = int(payload[0]) << 8 | int(payload[1])
    if t >= 0x8000:          # interpret values with the high bit set as negative
        t -= 0x10000
    return {"temp_c": t / 10.0}
