# Decode two unsigned big-endian bytes in tenths of Celsius.
# The transport removes the CHIRP1 prefix before decoding.
# Use signed packing and decoding for negative sensor readings.

def decode(payload, meta):
    return {"temp_c": (int(payload[0]) << 8 | int(payload[1])) / 10.0}
