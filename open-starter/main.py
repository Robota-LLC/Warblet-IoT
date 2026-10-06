# Warblet Open starter. Every 10 seconds this board posts one line of text,
# "<count> <what it last heard>", like "3 hello". Type in the Message box on
# its Warblet device page and the board says it back in its next line.
# Open level: no token, no TLS, no clock. Anyone who knows the id can post as it.
import socket

import open_setup                  # USB setup and Wi-Fi, from warbletiot.com/flash

HOST = "http.warbletiot.com"       # Warblet's plain-HTTP door, port 80
EVERY_S = 10                       # Open allows about one line every 5 seconds


def http(method, path, body=b""):
    """One HTTP request. Returns (status, reply); each step waits 15 s at most."""
    s = socket.socket()
    try:
        s.settimeout(15)
        s.connect(socket.getaddrinfo(HOST, 80)[0][-1])
        s.write(("%s %s HTTP/1.0\r\nHost: %s\r\nContent-Length: %d\r\n\r\n"
                 % (method, path, HOST, len(body))).encode() + body)
        head, _, reply = s.read(2048).partition(b"\r\n\r\n")
        return int(head.split(b" ")[1]), reply
    finally:
        s.close()


board_id = open_setup.start()      # waits for USB setup, then gives the board's id
count, heard = 0, "hello"
while True:
    try:
        open_setup.wifi()          # joins Wi-Fi, and joins again after a drop
        status, text = http("POST", "/ingest/%s/down" % board_id)
        if status == 200:          # someone typed in the Message box
            for cut in range(min(4, len(text)) or 1):  # a long one can end mid-character
                try:
                    heard = text[:len(text) - cut].decode()
                    break
                except UnicodeError:   # not text: keep what it heard before
                    pass
        count += 1
        line = "%d %s" % (count, heard)
        status, reply = http("POST", "/ingest/" + board_id, line.encode())
        print(line, "->", status, reply.decode().strip())  # "unclaimed" until claimed
    except Exception as e:         # no Wi-Fi, no answer: try again next time
        print("open: will try again:", e)
    open_setup.wait(EVERY_S)       # answers the setup page while it waits
