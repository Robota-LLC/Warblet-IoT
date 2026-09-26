"""MQTT sender with TLS enabled by default.
The credential is the CONNECT password. A signing key adds a raw HMAC prefix.
TLS requires a chain to a pinned root, hostname verification, and a set clock.
MQTT, SSL, and time modules are imported only when needed."""
import chirp_sign

# Default MQTT hostname; a stored host overrides it.
HOST = "mqtt.warbletiot.com"

# Use TLS on port 8883 by default; tls=false selects plaintext port 1883.
PORT = 8883
PORT_PLAINTEXT = 1883

# Longer than the report interval, short enough to notice a client killed by a NAT timeout.
KEEPALIVE_S = 90

# Three failed NTP tries mean the network has no NTP, not a busy moment.
NTP_TRIES = 3

# Let's Encrypt roots. X2 and X1 anchor today's chains; YE and YR are their successors.
# ISRG Root X2: https://letsencrypt.org/certs/isrg-root-x2.pem
# SHA-256 (DER): 69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470
# ISRG Root X1: https://letsencrypt.org/certs/isrgrootx1.pem
# SHA-256 (DER): 96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6
# ISRG Root YE: https://letsencrypt.org/certs/gen-y/root-ye.pem
# SHA-256 (DER): E14FFCAD5B0025731006CAA43A121A22D8E9700F4FB9CF852F02A708AA5D5666
# ISRG Root YR: https://letsencrypt.org/certs/gen-y/root-yr.pem
# SHA-256 (DER): E57B7E6F150C419102E8D5C055729FF967B9D1A829BF00CEC89CA604EBF4A86F
CA_ROOTS = """-----BEGIN CERTIFICATE-----
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
-----BEGIN CERTIFICATE-----
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
-----BEGIN CERTIFICATE-----
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
-----BEGIN CERTIFICATE-----
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
"""

_client = None
_clock_ok = False
_ctx = None
_ntp_at = None  # time.time() at the last NTP try

# An RTC drifts; re-sync a set clock this often before opening TLS.
NTP_RESYNC_S = 6 * 3600


def topic_up(hwid):
    return "chirp/{}/up".format(hwid)


def topic_down(hwid):
    return "chirp/{}/down".format(hwid)


def use_tls(config):
    """Return True for TLS unless config sets tls to false."""
    return config.get("tls", True)


def port_for(config):
    return PORT if use_tls(config) else PORT_PLAINTEXT


def connected():
    """True while a session is believed to be open."""
    return _client is not None


def ensure_time():
    """Try NTP when the clock is below the configured year threshold, and
    again every NTP_RESYNC_S on a set clock, since an RTC drifts.
    Return whether time is usable; the caller still attempts TLS after failure."""
    global _clock_ok, _ntp_at
    import time
    if _clock_ok or time.localtime()[0] >= 2024:
        _clock_ok = True
        if _ntp_at is None or time.time() - _ntp_at > NTP_RESYNC_S:
            _ntp_at = time.time()
            try:
                import ntptime
                ntptime.settime()
                _ntp_at = time.time()  # the clock may have jumped
            except Exception as e:
                print("chirp: ntp re-sync failed:", e)
        return True

    import ntptime
    for attempt in range(NTP_TRIES):
        try:
            ntptime.settime()
        except Exception as e:
            print("chirp: ntp try", attempt + 1, "of", NTP_TRIES, "failed:", e)
            time.sleep(2)
            continue
        if time.localtime()[0] >= 2024:
            _clock_ok = True
            _ntp_at = time.time()
            print("chirp: clock set from ntp --", time.localtime())
            return True

    print("chirp: NO CLOCK. ntp did not answer and the board still thinks the")
    print("chirp: year is", time.localtime()[0], "-- so TLS will fail, and it")
    print("chirp: will fail complaining about the certificate, not the date.")
    print("chirp: Fix ntp on this network, or set \"tls\": false in")
    print("chirp: chirp_cfg.json to fall back to port", PORT_PLAINTEXT,
          "in the clear")
    print("chirp: (an enforceTls device is then refused at ingest, by design).")
    return False


def ssl_context():
    """The context requiring a chain to one of the pinned roots, built once:
    one per connect holds its parsed roots until gc runs. umqtt.simple
    supplies the server hostname to the TLS wrapper."""
    global _ctx
    if _ctx is None:
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(cadata=CA_ROOTS)
        _ctx = ctx
    return _ctx


def connect(config, on_down=None):
    """Replace any session, connect, and subscribe if on_down is provided.
    The stored credential supplies the CONNECT password. Connection errors propagate."""
    global _client
    close()
    from umqtt.simple import MQTTClient  # lazy -- see the module docstring

    kwargs = {}
    if use_tls(config):
        # Set the clock before the socket: mbedtls checks certificate dates at handshake.
        ensure_time()
        kwargs["ssl"] = ssl_context()

    client = MQTTClient(config["hwid"], config.get("host") or HOST,
                        port=port_for(config),
                        user=config["hwid"], password=config["token"],
                        keepalive=KEEPALIVE_S, **kwargs)
    try:
        client.connect()
    except (OSError, ValueError) as e:
        # MicroPython raises ValueError for a rejected certificate.
        if use_tls(config):
            raise OSError("TLS to {} refused: {}".format(
                config.get("host") or HOST, e))
        raise
    if on_down is not None:
        client.set_callback(lambda topic, msg: on_down(msg))
        client.subscribe(topic_down(config["hwid"]))
    _client = client


def publish(config, payload):
    """Publish payload bytes, adding a 32-byte HMAC prefix when a key is stored."""
    if _client is None:
        raise OSError("no mqtt session")
    # The stored key is base64; chirp_sign decodes it.
    signing_key_base64 = config.get("key")
    if signing_key_base64:
        payload = chirp_sign.sign_raw(signing_key_base64, payload) + payload
    _client.publish(topic_up(config["hwid"]), payload)


def poll():
    """Check for a downlink message. Close a failed session so the app can reconnect."""
    if _client is None:
        return
    try:
        _client.check_msg()
    except Exception as e:
        print("chirp: mqtt link lost:", e)
        close()


def close():
    """Drop the session. Safe at any time, including when there isn't one."""
    global _client
    if _client is None:
        return
    try:
        _client.disconnect()
    except Exception:
        pass  # the socket is going away either way
    _client = None
