# Paste this Starlark decoder into the device spec. main/main.c encodes signed
# tenths of Celsius in two big-endian bytes: 00d7 is 21.5 C; ffce is -5.0 C.

def decode(payload, meta):
    t = int(payload[0]) << 8 | int(payload[1])
    if t >= 0x8000:          # sign-extend: the C side sends int16, not uint16
        t -= 0x10000
    return {"temp_c": t / 10.0}
