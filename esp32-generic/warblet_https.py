# Reports and commands: the part of warblet.py that sends. It comes as two
# files that differ only in the block marked "this part's own" below:
# warblet_https.py speaks HTTPS, warblet_http.py plain HTTP. main.py imports
# one of them after warblet, and its last line adds it to warblet:
#
#     import warblet
#     import warblet_https    # or warblet_http
#
# Its two main settings go on warblet, where main.py changes them after the
# imports: warblet.HOST and warblet.COMMAND_EVERY_S (below). A setting in
# capitals here can be changed the same way, for example
# warblet_https.TIMEOUT_S = 30.

# ---- reports and commands ---------------------------------------------------
# send() posts a reading; send_answer() also says whether Warblet knows the
# board; poll() asks for one waiting command. A signed message always carries
# a timestamp nonce, so nothing signed is sent without a set clock.
import network
import socket
import time

import warblet

# ---- this part's own: HTTPS -------------------------------------------------
# Every request is TLS to port 443. The server certificate must chain to a
# root in CA_ROOTS below and match the host name, or the request fails.
# Certificate dates need a set clock, so nothing is sent without one.
ROUTES = ["https"]
HOST = "https.warbletiot.com"
PORT = 443
CHECKS_DATES = True   # a certificate's dates: every request needs a set clock

# Let's Encrypt roots. X2 and X1 anchor today's chains; YE and YR are their
# successors. One string each, loaded one at a time: all four in one string
# would need 5 KB in one piece, and on an ESP32-C3 a running demo's heap may
# not have it, so MicroPython takes 64 KB from the memory TLS needs.
# ISRG Root X2: https://letsencrypt.org/certs/isrg-root-x2.pem
# SHA-256 (DER): 69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470
# ISRG Root X1: https://letsencrypt.org/certs/isrgrootx1.pem
# SHA-256 (DER): 96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6
# ISRG Root YE: https://letsencrypt.org/certs/gen-y/root-ye.pem
# SHA-256 (DER): E14FFCAD5B0025731006CAA43A121A22D8E9700F4FB9CF852F02A708AA5D5666
# ISRG Root YR: https://letsencrypt.org/certs/gen-y/root-yr.pem
# SHA-256 (DER): E57B7E6F150C419102E8D5C055729FF967B9D1A829BF00CEC89CA604EBF4A86F
CA_ROOTS = (
"""-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
""",
"""-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
""",
"""-----BEGIN CERTIFICATE-----
MIIB2TCCAWCgAwIBAgIRAKQCa6LvbHwg1AR+XmWmk4AwCgYIKoZIzj0EAwMwLjEL
MAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWUUwHhcN
MjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzENMAsG
A1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZRTB2MBAGByqGSM49AgEGBSuBBAAi
A2IABDwS/6vhrcVqcbBo+wgdI3fwn9x7DNJJOY/lTOti0vkwuRN87RhEhTH17E7X
yFjWsPYhIPt/wzOqxTd2b+4ZJNy9ID04YywF9U5zasDVyGSNErVNtz8uSGh5izW8
7j77GaNCMEAwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0O
BBYEFKPIJlqOoUzQNWP8myPIOq5W809WMAoGCCqGSM49BAMDA2cAMGQCMHhMr8N9
LdL1VQKs9BdV81r76eXRB6mtjuNjzk6/lBsPNToWLTDzGYgtQKO1jl63uAIwGV7m
onyF377c+MM1oqVNs17sgu7F9YKZwgLmVbeOMDbKAXHtKMDLbiGllCcs8f47
-----END CERTIFICATE-----
""",
"""-----BEGIN CERTIFICATE-----
MIIFKTCCAxGgAwIBAgIRAOxGNJNgz0sP+KmC2Tqpyj0wDQYJKoZIhvcNAQELBQAw
LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw
HhcNMjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzEN
MAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjCCAiIwDQYJKoZIhvcNAQEB
BQADggIPADCCAgoCggIBANvGJnN78CTJdWL3+eGfsLN5TrNBJs+VH9hRXqRbwxu9
sGNiB0BD1fcOxbSUQCJIM1xE13Db+5Cw1w0s0EBYsvuIP/6joF0w8cuImbgR1OGg
YbSQ4OpzI+DG8SGuTlcE873OCS+kh3srlo6vl43M5OJg4Aeo1sfHp6kTJDoIiFBN
JAY+OKfX/FUvYKuhjT+no49lmqmupSBI5PkBQiqrEGtWU5uxU/cQWHGu8jSjFBzn
ZqvbNPLMXMLFxCb3WTfrJBXXjqvWG+v4bjzxjjeAtOlU7qarRDvNOyAuQYLln904
M+faKx8hnLCpJ15ZqaEgcNlY+9MMWcC5yvL2A2j3l9+2buggZX+dOE91zYmIdawT
vSZuVvlbRrAlLxIB6pwMBjneXCjYQ8+3BCCjssbSNpZU3hTcBDdhfAlEDlYr6pEa
tnMdmDT5BqnKC92bd0EhM1fbLHioLccLCuievT8ZkPhZrq7Mii7gNXAcUEAR8+lz
Yal+9zTg7C5DALyVOeG/CqfRAMn1KSHCR0NSA6P8tn/mGRlnCct5rtVCLnVySVpU
6H1qGg3DgTOuskf8eahTMiYbI5ezPJmO5ertalskQ1utp74+eDy92PI4ftHKTbq9
IWhH4YZKh3WnJEIt+oQvlYZbY8tpEroKrFB6PFGzrJIDRyts4HqvuH52RFj2zv/B
AgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud
DgQWBBTe51tg0CJtQCh9Pw0B/qS1UrRRlDANBgkqhkiG9w0BAQsFAAOCAgEAWHnf
713Bdkq7t5yN2dNIgQakUb94X9WuyhMEHHkgx4oDpSUlnG0w4g94MoqaEUE31ZjR
LU7L5LD1g9ujFHTQu8AD215AHMVQFbm6j8hQxdXHAzDajFNQnOlDJrLjzIx176oy
AjvUtejZx2NNmdb5fd0WGVGsCdoAJ3N8ozo7ajE8t6vfxStZb4BQ9WYJGHUDrv2N
i5tJF6CNiPnlzs3BUfECRbE4JSk+jvy8+VoGiFE8qsH/j78x2fjgQhAQFV7P7Zxy
dBTZ1wEkNpZNW2qnaK1SKBLa+xf6E06YRIq5uaI+HWH8SY1y5VbRgzq40EKg3yxP
06fz+uYAUIFJoLNfhwRCc3Q6pQVuMX3yAjHAes4gk4moGcLQ5p7HAh39yeylZc1J
41sx/jKwLIkPE6Rr1Nf4pxdsxf9SA4yOEiAkDgq04DVxn8hgYFdUtBCuiuVC2heA
EiqVEa+8QZjuw8Gj0EbHXcRd1nInvGqRS1o9Is7YBdQN57X1AYveGBNNqjICSb7c
awuw1EawTDrs13VUlJVEsbQ0/O/1aaV73mCdOQ8azqL2KTv1Ewu1xbquE2S+kdQU
To9TUwat3wUA6cwXh1EfpS/3fJ0aGah5hdpRyoCLDlsSn8tkrjMfFFX0viC+GxHc
sI1ANRYvqSFC2X1VRZfDg+wD6E21BccmifG4yWc=
-----END CERTIFICATE-----
""",
)

_tls = None


def tls_context():
    # The TLS context for every request, built once (one per request would
    # hold its parsed roots until gc runs, and a dozen handshakes exhaust the
    # heap). It refuses any chain that does not end at CA_ROOTS, and
    # wrap_socket(..., server_hostname=host) adds the host name check. Roots
    # that will not load raise OSError: no connection, never an unchecked one.
    global _tls
    if _tls is None:
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.verify_mode = ssl.CERT_REQUIRED
        try:
            for root in CA_ROOTS:   # each adds to the context's roots
                ctx.load_verify_locations(cadata=root)
        except (OSError, ValueError) as e:
            raise OSError("the roots in this file do not load ({}), so nothing is sent".format(e))
        _tls = ctx
    return _tls


def _wrap(s, host):
    # The connection as TLS, checked against the host name. Roots that do not
    # load: no request, never an unchecked one.
    ctx = tls_context()
    try:
        return ctx.wrap_socket(s, server_hostname=host)
    except (OSError, ValueError) as e:
        # MicroPython raises ValueError for a rejected certificate.
        raise OSError("TLS to {} refused: {}".format(host, e))
# ---- end of this part's own -------------------------------------------------

# Where to send (a host the setup page stored wins), and how often to ask for
# a command (a check takes about 1 s).
warblet.HOST = HOST
warblet.COMMAND_EVERY_S = 5

TIMEOUT_S = 20

# The most of a command this part reads. Warblet permits up to 8 KiB; a
# longer reply is refused, said on the console, and that command is gone:
# Warblet settles it once it has sent it.
MAX_COMMAND_BYTES = 1024


def _read_exactly(s, count):
    # Exactly `count` bytes from the connection, or OSError if it ends first.
    data = b""
    while len(data) < count:
        piece = s.read(count - len(data))
        if not piece:
            raise OSError("reply cut short")
        data += piece
    return data


def _read_reply_body(s):
    # Read the reply headers, then the whole body. A reply that ends early
    # is an OSError: a cut-off command must never look like a shorter one.
    length = None
    chunked = False
    while True:
        line = s.readline()
        if not line:
            raise OSError("reply cut short")
        if line == b"\r\n":
            break
        name, _, value = line.partition(b":")
        name = name.strip().lower()
        if name == b"content-length":
            length = int(value.strip())
        elif name == b"transfer-encoding" and b"chunked" in value.lower():
            chunked = True
    if chunked:
        # The body comes in pieces, each after a line with its size in hex,
        # and ends with a piece of size 0.
        body = b""
        while True:
            line = s.readline()
            if not line.endswith(b"\n"):
                raise OSError("reply cut short")
            size = int(line.split(b";")[0].strip().decode(), 16)
            if size == 0:
                return body
            if len(body) + size > MAX_COMMAND_BYTES:
                raise OSError("reply over {} bytes".format(MAX_COMMAND_BYTES))
            body += _read_exactly(s, size)
            if _read_exactly(s, 2) != b"\r\n":
                raise OSError("reply garbled")
    if length is not None:
        if length > MAX_COMMAND_BYTES:
            raise OSError("reply over {} bytes".format(MAX_COMMAND_BYTES))
        return _read_exactly(s, length)
    # No length given: with `Connection: close` the body runs to the end.
    body = s.read(MAX_COMMAND_BYTES + 1) or b""
    if len(body) > MAX_COMMAND_BYTES:
        raise OSError("reply over {} bytes".format(MAX_COMMAND_BYTES))
    return body


def _request(method, host, path, headers, body=None, want_reply=False):
    # One request with `Connection: close`. Returns (HTTP status code,
    # reply body or None). Only a 200 answer to a GET carries a body we use,
    # and a 2xx answer to a POST when the caller wants Warblet's word on it;
    # for anything else the status is enough, so we do not wait for more.
    addr = socket.getaddrinfo(host, PORT)[0][-1]
    s = socket.socket()
    try:
        s.settimeout(TIMEOUT_S)
        s.connect(addr)
        s = _wrap(s, host)    # this part's connection: TLS, or as it is
        lines = ["{} {} HTTP/1.1".format(method, path),
                 "Host: {}".format(host),
                 "Connection: close"]
        if body is not None:
            lines.append("Content-Length: {}".format(len(body)))
        for name in headers:
            lines.append("{}: {}".format(name, headers[name]))
        s.write("\r\n".join(lines).encode() + b"\r\n\r\n")
        if body is not None:
            s.write(body)

        status = s.readline()  # b"HTTP/1.1 202 Accepted\r\n"
        parts = status.split(b" ")
        if len(parts) < 2:
            raise OSError("no status line")
        code = int(parts[1])
        if method == "GET" and code == 200:
            return code, _read_reply_body(s)
        if want_reply and 200 <= code < 300:
            try:
                return code, _read_reply_body(s)
            except (OSError, ValueError):
                return code, None  # taken all the same; only the word is lost
        return code, None
    finally:
        s.close()


def send(payload, tag=None):
    # POST the payload bytes, with an optional tag saying what the message is
    # (for example "press"). True when Warblet took it: an HTTP 2xx, which is
    # "stored", or "seen" while no account has the board (send_answer() tells
    # the two apart). False otherwise, and the console says why. main.py calls
    # it as warblet.send().
    return 200 <= _said(payload, tag, False)[0] < 300


def send_answer(payload, tag=None):
    # send(), and Warblet's word on it: a 202 says "accepted" (stored) or
    # "unclaimed" (no account has this board, so it was only seen; it is set up
    # at warbletiot.com/flash). Returns (HTTP status code or 0, that word, or
    # None when the answer has none).
    code, reply = _said(payload, tag, True)
    if reply and b'"unclaimed"' in reply:
        return code, "unclaimed"
    if reply and b'"accepted"' in reply:
        return code, "accepted"
    return code, None


def _said(payload, tag, want_reply):
    # _send(), with a refusal or a failure said on the console. A command
    # check follows each message: an answer to it may be waiting.
    try:
        code, reply = _send(payload, tag, want_reply)
    except Exception as e:
        print("chirp: send failed:", e)
        return 0, None
    finally:
        check_now()
    if not 200 <= code < 300:
        print("chirp: ingest replied", code)
    return code, reply


def _send(payload, tag, want_reply):
    key = warblet.signing_key()
    if key or CHECKS_DATES:
        warblet.require_clock()  # a signed message's nonce, or a certificate's dates
    headers = {
        "Content-Type": "application/octet-stream",
        "X-Chirp-Token": warblet.config["token"],
    }
    # A claim code rides in both headers until it binds and becomes the token.
    if warblet.config["token"].startswith("CHIRP-"):
        headers["X-Chirp-Claim"] = warblet.config["token"]
    if tag is not None:
        headers["X-Chirp-Tag"] = tag

    # With a key, require_clock() above means there is a nonce.
    if key:
        nonce = warblet.nonce_ms()
        headers["X-Chirp-Nonce"] = str(nonce)
        headers["X-Chirp-Signature"] = warblet.sign(key, nonce, payload, tag)

    code, reply = _request("POST", warblet.config.get("host") or warblet.HOST,
                           "/ingest/{}".format(warblet.config["hwid"]),
                           headers, payload, want_reply)
    if code == 401 and key:
        warblet.clock_suspect()
    return code, reply


def poll():
    # Ask Warblet for the next waiting command (GET .../down).
    # Returns (HTTP status code, command bytes or None).
    # 200 carries one command; 204 means nothing is waiting.
    key = warblet.signing_key()
    if key or CHECKS_DATES:
        warblet.require_clock()  # a signed request's nonce, or a certificate's dates
    headers = {"X-Chirp-Token": warblet.config["token"]}

    # A signed device proves it holds its key, so a leaked token alone cannot
    # read its commands. With a key, require_clock() above means there is a nonce.
    if key:
        nonce = warblet.nonce_ms()
        headers["X-Chirp-Nonce"] = str(nonce)
        headers["X-Chirp-Signature"] = warblet.sign_poll(key, warblet.config["hwid"], nonce)

    code, reply = _request("GET", warblet.config.get("host") or warblet.HOST,
                           "/ingest/{}/down".format(warblet.config["hwid"]), headers)
    if code == 401 and key:
        warblet.clock_suspect()
    if code != 200:
        return code, None
    if reply.startswith(b"CHIRPKEY1 "):
        # Warblet can send a signing key this way, and marks it delivered.
        # This firmware takes its key over USB, so it drops this one -- and
        # never prints it. Install the key over the USB setup instead.
        print("chirp: a signing key came in place of a command; not used "
              "(this firmware takes keys over USB)")
        return code, None
    return code, reply


def check_commands(handler):
    # Ask Warblet for one waiting command and hand it to warblet.command(),
    # which runs the handler. Another waiting command comes at the next check.
    if not network.WLAN(network.STA_IF).isconnected():
        return
    try:
        code, command = poll()
    except Exception as e:
        print("chirp: command check failed:", e)
        return
    if code == 200 and command is not None:
        warblet.command(command)
    elif code not in (200, 204):
        print("chirp: command check replied", code)


_next_check = 0  # when upkeep() next asks for a command (time.ticks_ms())


def check_now():
    # Make the next upkeep() ask for a command at once (after each report:
    # an answer to it may be waiting), and have wait() give upkeep() its turn
    # at once, whichever call sent the report.
    global _next_check
    _next_check = time.ticks_ms()
    warblet.turn_now()


def upkeep(handler):
    # warblet.wait() calls this on every pass. With a command function, ask
    # for a command every warblet.COMMAND_EVERY_S. Returns how soon to call
    # it again, in seconds.
    global _next_check
    if not handler:
        return 3600
    if time.ticks_diff(time.ticks_ms(), _next_check) >= 0:
        check_commands(handler)
        _next_check = time.ticks_add(time.ticks_ms(), int(warblet.COMMAND_EVERY_S * 1000))
    return max(0, time.ticks_diff(_next_check, time.ticks_ms())) / 1000


# This part's routes, its send() and its upkeep(), added to warblet.
warblet.add_part(ROUTES, send, upkeep)
