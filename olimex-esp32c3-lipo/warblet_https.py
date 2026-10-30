# HTTPS reports and commands, for warblet.py: in a file of its own so that
# neither file is too big to compile on a board with little memory. main.py
# imports this one, which brings warblet.py's calls with it:
#
#     import warblet_https as warblet
from warblet import *

# ---- HTTPS: reports and commands -------------------------------------------
# send() posts a reading; send_answer() also says whether Warblet knows the
# board; poll() asks for one waiting command. The server certificate must
# chain to a root above and match the host name, or the request fails.
# Certificate dates need a set clock, so nothing is sent without one, and a
# signed message always carries a timestamp nonce.
import socket

TRANSPORTS.append("https")  # what the setup page is told this firmware sends over

HOST = "https.warbletiot.com"  # default; a stored host overrides it
PORT = 443
TIMEOUT_S = 20

# Dashboard commands are at most 256 bytes; a bigger reply is not a command.
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
    # One HTTPS request with `Connection: close`. Returns (HTTP status code,
    # reply body or None). Only a 200 answer to a GET carries a body we use,
    # and a 2xx answer to a POST when the caller wants Warblet's word on it;
    # for anything else the status is enough, so we do not wait for more.
    addr = socket.getaddrinfo(host, PORT)[0][-1]
    s = socket.socket()
    try:
        s.settimeout(TIMEOUT_S)
        s.connect(addr)
        try:
            s = tls_context().wrap_socket(s, server_hostname=host)
        except (OSError, ValueError) as e:
            # MicroPython raises ValueError for a rejected certificate.
            raise OSError("TLS to {} refused: {}".format(host, e))
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
    # (for example "press"). Returns the HTTP status code, or 0 when no answer
    # came; a refusal or a failure is said on the console.
    return _said(payload, tag, False)[0]


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
    require_clock()
    headers = {
        "Content-Type": "application/octet-stream",
        "X-Chirp-Token": config["token"],
    }
    # A claim code rides in both headers until it binds and becomes the token.
    if config["token"].startswith("CHIRP-"):
        headers["X-Chirp-Claim"] = config["token"]
    if tag is not None:
        headers["X-Chirp-Tag"] = tag

    # require_clock() above means there is a nonce.
    key = signing_key()
    if key:
        nonce = nonce_ms()
        headers["X-Chirp-Nonce"] = str(nonce)
        headers["X-Chirp-Signature"] = sign(key, nonce, payload, tag)

    code, reply = _request("POST", config.get("host") or HOST,
                           "/ingest/{}".format(config["hwid"]),
                           headers, payload, want_reply)
    if code == 401 and key:
        clock_suspect()
    return code, reply


def poll():
    # Ask Warblet for the next waiting command (GET .../down).
    # Returns (HTTP status code, command bytes or None).
    # 200 carries one command; 204 means nothing is waiting.
    require_clock()
    headers = {"X-Chirp-Token": config["token"]}

    # A signed device proves it holds its key, so a leaked token alone cannot
    # read its commands. require_clock() above means there is a nonce.
    key = signing_key()
    if key:
        nonce = nonce_ms()
        headers["X-Chirp-Nonce"] = str(nonce)
        headers["X-Chirp-Signature"] = sign_poll(key, config["hwid"], nonce)

    code, reply = _request("GET", config.get("host") or HOST,
                           "/ingest/{}/down".format(config["hwid"]), headers)
    if code == 401 and key:
        clock_suspect()
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


def _check_commands(handler):
    # Ask Warblet for one waiting command and hand it to the handler.
    # Another waiting command comes at the next check.
    if not network.WLAN(network.STA_IF).isconnected():
        return
    try:
        code, command = poll()
    except Exception as e:
        print("chirp: command check failed:", e)
        return
    if code == 200 and command is not None:
        handler(command)
    elif code not in (200, 204):
        print("chirp: command check replied", code)


use_commands(_check_commands)
