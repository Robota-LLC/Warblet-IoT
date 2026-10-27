# Tasmota: the plug posts its power once a minute, as text in one small JSON
# object, like
#   {"Power":"42"}
# The rule in the setup line builds it from Tasmota's ENERGY readings; nothing
# else from the plug is in it. sample-messages.txt has a few.
#
# Power becomes a number on your dashboard, in watts.
#
# To send Voltage, Current and Energy today as well: a rule can put only the
# reading that set it off into a post, so the other three are kept in rule
# variables first and sent with Power. Paste this line instead of the one the
# setup page showed, with your board id and setup code in place of
# <your-board-id> and <your-setup-code> (both are in the page's line):
#
#   Backlog Rule3 ON Tele-ENERGY#Voltage DO _Var14 %value% ENDON ON Tele-ENERGY#Current DO _Var15 %value% ENDON ON Tele-ENERGY#Today DO _Var16 %value% ENDON ON Tele-ENERGY#Power DO _WebQuery http://http.warbletiot.com/ingest/<your-board-id> POST [X-Chirp-Token:<your-setup-code>|X-Chirp-Claim:<your-setup-code>] {"Power":"%value%","Voltage":"%var14%","Current":"%var15%","Today":"%var16%"} ENDON; Rule3 1; TelePeriod 60
#
# The rule then overwrites Var14, Var15 and Var16
# each minute, which your own rules share.
# A reading your meter does not measure is sent as whatever its variable last
# held. This decoder already reads all four, so nothing else changes.

READINGS = {                       # the name in the post: the name on your dashboard
    "Power": "Power",              # watts
    "Voltage": "Voltage",          # volts
    "Current": "Current",          # amps
    "Today": "Energy today",       # kWh since midnight, by the plug's clock
}

def number(text):
    # Tasmota writes plain decimals: an optional minus, digits, at most one
    # point. Anything else is not a reading.
    text = text.strip()
    digits = text[1:] if text.startswith("-") else text
    if not digits or digits.count(".") > 1:
        return None
    for ch in digits.elems():
        if ch != "." and not ch.isdigit():
            return None
    if digits == ".":
        return None
    return float(text) if "." in text else int(text)

def decode(payload, meta):
    post = json.decode(payload, None)
    if type(post) != "dict":
        fail("expected the plug's readings, like {\"Power\":\"42\"}; got: " + str(payload)[:40])
    fields = {}
    for name, label in READINGS.items():
        value = post.get(name)
        if type(value) == "string":
            value = number(value)
        if type(value) in ("int", "float"):
            fields[label] = value
    return fields
