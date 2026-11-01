# MQTT reports and commands: the part of warblet.py that sends, for a board on
# Warblet's MQTT broker. main.py imports it after warblet, and its last line
# adds it to warblet:
#
#     import warblet
#     import warblet_mqtt
#
# Its main setting goes on warblet, where main.py changes it after the imports:
# warblet.HOST (a host the setup page stored wins). A setting in capitals here
# can be changed the same way, for example warblet_mqtt.KEEPALIVE_S = 120.

# ---- MQTT: reports and commands --------------------------------------------
# The session uses TLS on port 8883, its certificate checked against the roots
# below, unless the setup page stored "tls": false, which takes
# plain port 1883 (an enforceTls device is then refused at ingest). The
# credential is the CONNECT password, and a stored signing key puts a 32-byte
# HMAC in front of each payload. Commands come on the session as they are sent:
# upkeep() hands each one over, pings the broker, and drops a session that has
# gone quiet; the next send() connects again.
import time

import warblet

warblet.HOST = "mqtt.warbletiot.com"

PORT = 8883
PORT_PLAINTEXT = 1883

# Longer than the report interval, short enough to notice a client killed by a
# NAT timeout.
KEEPALIVE_S = 90

# Each step of connecting (TCP, TLS, the broker's answer) may take this long.
CONNECT_TIMEOUT_S = 20

# A session can die without a word, for example when the path to the broker
# goes quiet. So ping the broker this often, and give the session up when it
# has sent nothing at all for SILENCE_LIMIT_S; the next send() reconnects.
PING_S = 30
SILENCE_LIMIT_S = 75

# How often upkeep() looks at the session for a command, at most.
CHECK_EVERY_S = 0.2

# A command waits from the moment it arrives until upkeep() hands it over: at
# most QUEUE_MAX of them, QUEUE_BYTES together, so a burst cannot take the
# memory the next TLS handshake needs. Past either, the newest is dropped and
# the console says so. One command of any size fits an empty queue: Warblet
# sends at most 8 KiB in one.
QUEUE_MAX = 8
QUEUE_BYTES = 8192


# ---- TLS: the roots Warblet's certificates chain to ------------------------

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

_client = None     # the umqtt client while a session is open
_incoming = None   # select.poll watching the session's socket
_heard_ms = 0      # ticks_ms when the broker last sent us anything
_pinged_ms = 0     # ticks_ms when we last pinged it
_queue = []        # commands waiting for upkeep(), oldest first
_queued = 0        # their bytes together


def send(payload, tag=None):
    # Publish the payload bytes, with an optional tag saying what the message is
    # (for example "press"): it becomes one more topic level, chirp/<id>/up/<tag>,
    # and is signed with the payload. Connects first when there is no session.
    # True when the publish was written to the session. MQTT at QoS 0 sends no
    # answer, so that is all True means: the message left the board, not that
    # Warblet stored it. False when it could not be written, and the console
    # says why. main.py calls it as warblet.send().
    try:
        if _client is None:
            _connect()
        topic = "chirp/{}/up".format(warblet.config["hwid"])
        if tag is not None:
            topic += "/" + tag
        key = warblet.signing_key()
        if key:
            payload = warblet.sign_raw(key, payload, tag) + payload
        _client.publish(topic, payload)
        return True
    except Exception as e:
        print("chirp: mqtt failed:", e)
        _close()
        return False


def upkeep(handler):
    # warblet.wait() calls this after every pass, outside any send: read what
    # has arrived, ping the broker every PING_S, drop a session that has been
    # silent for SILENCE_LIMIT_S, and hand the oldest waiting command to
    # warblet.command(). One per turn, with or without a session, so its
    # handler can send, and connect, as main.py does. Returns how soon to call
    # it again, in seconds.
    global _heard_ms, _pinged_ms, _queued
    if _client is not None:
        now = time.ticks_ms()
        try:
            if _incoming.poll(0):
                _heard_ms = now  # a command, a ping answer, or the session ending
                _client.check_msg()
            if time.ticks_diff(now, _pinged_ms) >= PING_S * 1000:
                _client.ping()
                _pinged_ms = now
            if time.ticks_diff(now, _heard_ms) >= SILENCE_LIMIT_S * 1000:
                raise OSError("no answer from the broker for {} s".format(SILENCE_LIMIT_S))
        except Exception as e:
            print("chirp: mqtt link lost:", e)
            _close()
    if _queue:
        data = _queue.pop(0)
        _queued -= len(data)
        warblet.command(data)
        if _queue:
            return 0  # the next one at the next turn
    return CHECK_EVERY_S if _client is not None else 3600  # no session: the next send() connects


def _arrived(topic, msg):
    # A command on chirp/<id>/down, as its bytes, inside subscribe() or
    # check_msg(): only queued. upkeep() hands it over.
    global _queued
    data = bytes(msg)
    if len(_queue) >= QUEUE_MAX or (_queue and _queued + len(data) > QUEUE_BYTES):
        print("chirp: a command dropped ({} bytes): {} already waiting".format(len(data), len(_queue)))
        return
    _queue.append(data)
    _queued += len(data)


def _connect():
    # Open a session: TLS unless "tls" is stored as false, with the clock set
    # first (certificate dates are checked at the handshake), then subscribe to
    # this board's commands. Errors propagate, with the half-open connection
    # closed; each step gives up after CONNECT_TIMEOUT_S. A command the broker
    # sends inside subscribe() is queued for upkeep(), like any other.
    global _client, _incoming, _heard_ms, _pinged_ms
    _close()
    from umqtt.simple import MQTTClient  # imported when first needed
    config = warblet.config
    host = config.get("host") or warblet.HOST
    tls = config.get("tls") is not False  # plain only when the setup page said so
    kwargs = {}
    if tls:
        warblet.require_clock()
        kwargs["ssl"] = tls_context()  # roots that do not load: no session
    client = MQTTClient(config["hwid"], host, port=PORT if tls else PORT_PLAINTEXT,
                        user=config["hwid"], password=config["token"],
                        keepalive=KEEPALIVE_S, **kwargs)
    try:
        try:
            client.connect(timeout=CONNECT_TIMEOUT_S)
        except (OSError, ValueError) as e:
            # MicroPython raises ValueError for a rejected certificate.
            if tls:
                raise OSError("TLS to {} refused: {}".format(host, e))
            raise
        client.set_callback(_arrived)
        client.subscribe("chirp/{}/down".format(config["hwid"]))
        import select
        _incoming = select.poll()
        _incoming.register(client.sock, select.POLLIN)
    except Exception:
        try:
            client.sock.close()  # no session: nothing may stay open
        except Exception:
            pass
        raise
    _heard_ms = _pinged_ms = time.ticks_ms()
    _client = client
    import gc
    gc.collect()
    # gc.mem_free() is the Python heap's free bytes plus the largest block it
    # could still take from the system (MicroPython 1.28), not the heap alone.
    print("chirp:", "mqtts" if tls else "mqtt", "session up, free memory", gc.mem_free())


def _close():
    # Drop the session. Safe at any time, including when there is none.
    global _client
    if _client is None:
        return
    try:
        _client.disconnect()
    except Exception:
        pass  # the socket is going away either way
    _client = None


# This part's routes, its send() and its upkeep(), and the push field it takes:
# "tls", true or false, the only setting that picks the plain route.
warblet.add_part(["mqtt", "mqtts"], send, upkeep, fields={"tls": bool})
