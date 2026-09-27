# Decode the simulated reading: two unsigned big-endian bytes in tenths of Celsius.
# 00 c9 means 20.1 C. Use signed packing and decoding for negative values.


def decode(payload, meta):
    t = int(payload[0]) << 8 | int(payload[1])
    return {"temp_c": t / 10.0}
