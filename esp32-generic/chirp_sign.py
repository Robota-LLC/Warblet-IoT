"""HMAC-SHA256 for MicroPython.
Accept a base64 key; return lowercase hex for HTTP or raw bytes for MQTT/TCP."""
try:
    from uhashlib import sha256
    from ubinascii import a2b_base64, hexlify
except ImportError:  # host CPython, so this file can be tested off the board
    from hashlib import sha256
    from binascii import a2b_base64, hexlify

BLOCK = 64


def _digest(data):
    h = sha256()
    h.update(data)
    return h.digest()


def hmac_sha256(signing_key_bytes, message):
    if len(signing_key_bytes) > BLOCK:
        signing_key_bytes = _digest(signing_key_bytes)
    ipad = bytearray(BLOCK)
    opad = bytearray(BLOCK)
    ipad[:len(signing_key_bytes)] = signing_key_bytes
    opad[:len(signing_key_bytes)] = signing_key_bytes
    for i in range(BLOCK):
        ipad[i] ^= 0x36
        opad[i] ^= 0x5C
    return _digest(bytes(opad) + _digest(bytes(ipad) + message))


def sign(signing_key_base64, nonce, body):
    """Return a hex HMAC-SHA256 over the body, prefixed by the nonce line if present."""
    if nonce is not None:
        body = str(nonce).encode() + b"\n" + body
    return hexlify(hmac_sha256(a2b_base64(signing_key_base64), body)).decode()


def sign_raw(signing_key_base64, body):
    """Return the 32-byte HMAC-SHA256 of the body for MQTT/TCP payload prefixes."""
    return hmac_sha256(a2b_base64(signing_key_base64), body)
