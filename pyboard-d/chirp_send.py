"""Plain-HTTP socket sender for Pyboard D.
Adds token and optional HMAC headers, with SNTP time sync for nonces.
This sender does not use TLS."""
import machine
import socket
import struct
import time

import chirp_sign

# Default plain-HTTP host; a stored host overrides it.
HOST = "http.warbletiot.com"
PORT = 80

# MicroPython's epoch is 2000; the nonce needs Unix seconds.
_EPOCH_2000 = 946684800
_CLOCK_SANE = 1735689600  # Minimum Unix time accepted as a set clock

# Seconds between the NTP epoch (1900) and MicroPython's (2000).
_NTP_DELTA = 3155673600
NTP_HOST = "pool.ntp.org"

_last_nonce = 0
_ntp_at = None  # time.time() at the last NTP try
_ntp_wait = 0   # seconds after that try before the next one

# An RTC drifts, and a nonce has to stay inside the server's +/-5 min:
# re-sync this often, and sooner after a refused signed send -- but not on
# every send, since a bad token or key is refused the same way.
NTP_RESYNC_S = 6 * 3600
NTP_AFTER_401_S = 15 * 60


def sync_clock():
    """Try SNTP to set the RTC. Without a clock, signatures cover only the body
    and provide no replay protection."""
    global _ntp_at, _ntp_wait
    _ntp_at = time.time()
    _ntp_wait = NTP_RESYNC_S
    try:
        query = bytearray(48)
        query[0] = 0x1B  # LI=0, VN=3, Mode=3 (client)
        addr = socket.getaddrinfo(NTP_HOST, 123)[0][-1]
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.settimeout(5)
            s.sendto(query, addr)
            msg = s.recv(48)
        finally:
            s.close()
        secs = struct.unpack("!I", msg[40:44])[0] - _NTP_DELTA
        tm = time.gmtime(secs)
        # pyb's RTC counts weekdays from 1 on Monday; time.gmtime counts from 0.
        machine.RTC().datetime((tm[0], tm[1], tm[2], tm[6] + 1,
                                tm[3], tm[4], tm[5], 0))
        _ntp_at = time.time()  # the clock may have jumped
    except Exception as e:
        print("chirp: ntp sync failed:", e)


def _nonce_ms():
    """Unix ms, strictly increasing, or None if the RTC was never set."""
    global _last_nonce
    secs = time.time() + _EPOCH_2000
    if secs < _CLOCK_SANE:
        return None
    ms = secs * 1000 + (time.ticks_ms() % 1000)
    if ms <= _last_nonce:
        ms = _last_nonce + 1  # the contract forbids two signed sends on one ms
    _last_nonce = ms
    return ms


def _post(host, path, headers, body, timeout=20):
    """One HTTP/1.1 POST with `Connection: close`, so only the status line is parsed.
    Returns the HTTP status code."""
    addr = socket.getaddrinfo(host, PORT)[0][-1]
    s = socket.socket()
    try:
        s.settimeout(timeout)
        s.connect(addr)
        lines = ["POST {} HTTP/1.1".format(path),
                 "Host: {}".format(host),
                 "Content-Length: {}".format(len(body)),
                 "Connection: close"]
        for name in headers:
            lines.append("{}: {}".format(name, headers[name]))
        s.write("\r\n".join(lines).encode() + b"\r\n\r\n")
        s.write(body)

        status = s.readline()  # b"HTTP/1.1 202 Accepted\r\n"
        parts = status.split(b" ")
        if len(parts) < 2:
            raise OSError("no status line")
        return int(parts[1])
    finally:
        s.close()


def send(config, payload):
    """POST the raw payload bytes. Returns the HTTP status code."""
    global _ntp_wait
    headers = {
        "Content-Type": "application/octet-stream",
        "X-Chirp-Token": config["token"],
    }
    # A claim code rides in both headers until it binds and becomes the token.
    if config["token"].startswith("CHIRP-"):
        headers["X-Chirp-Claim"] = config["token"]

    # The stored key is base64; chirp_sign decodes it.
    signing_key_base64 = config.get("key")
    if signing_key_base64:
        if _ntp_at is None or time.time() - _ntp_at > _ntp_wait:
            sync_clock()  # an RTC drifts; a failed re-sync keeps the clock it has
        nonce = _nonce_ms()
        if nonce is not None:
            headers["X-Chirp-Nonce"] = str(nonce)
        headers["X-Chirp-Signature"] = chirp_sign.sign(
            signing_key_base64, nonce, payload)

    code = _post(config.get("host") or HOST,
                 "/ingest/{}".format(config["hwid"]),
                 headers, payload)
    if code == 401 and signing_key_base64:
        _ntp_wait = NTP_AFTER_401_S  # the nonce may be outside the window
    return code
