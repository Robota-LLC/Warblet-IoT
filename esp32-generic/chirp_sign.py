"""HMAC-SHA256 for MicroPython.
Accept a base64 key; return lowercase hex for HTTP or raw bytes for MQTT/TCP.

Every signature covers the same shape of bytes:

    <nonce> "\\n" "tag=" <tag> "\\n" <body>

Both lines are always there. A field the message does not have is left
empty, so the bytes say which fields were sent and a nonce or tag cannot be
moved into the body."""
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


def signed_input(nonce, tag, body):
    """Return the bytes a signature covers: "<nonce>\\ntag=<tag>\\n" + body.
    None means the message has no such field; it is signed as empty.
    Example: signed_input(None, None, b"temp=21.4") == b"\\ntag=\\ntemp=21.4"."""
    nonce = "" if nonce is None else str(nonce)
    tag = "" if tag is None else str(tag)
    return nonce.encode() + b"\ntag=" + tag.encode() + b"\n" + body


def sign(signing_key_base64, nonce, body, tag=None):
    """Return the hex HMAC-SHA256 for X-Chirp-Signature. Pass the nonce and
    tag this message sends as headers, or None for a header it leaves out."""
    return hexlify(hmac_sha256(a2b_base64(signing_key_base64),
                               signed_input(nonce, tag, body))).decode()


def sign_poll(signing_key_base64, hwid, nonce):
    """Return the hex HMAC-SHA256 a signed device sends when it asks for
    commands: over "chirp-down\n<hwid>\n<nonce>"."""
    challenge = "chirp-down\n{}\n{}".format(hwid, nonce).encode()
    return hexlify(hmac_sha256(a2b_base64(signing_key_base64), challenge)).decode()


def sign_raw(signing_key_base64, body, tag=None):
    """Return the 32-byte HMAC-SHA256 for MQTT/TCP payload prefixes. These
    transports carry no nonce, so the nonce field is empty; a tag travels in
    the topic or line and is signed here. Only the body is sent."""
    return hmac_sha256(a2b_base64(signing_key_base64),
                       signed_input("", tag, body))
