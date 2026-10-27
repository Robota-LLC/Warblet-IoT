# Tasmota: the plug posts its power meter's readings once a minute, as text
# in one small JSON object, like
#   {"Power":"42","Voltage":"121","Current":"0.413","Today":"0.123"}
# The rule in the setup line builds it from Tasmota's ENERGY readings; nothing
# else from the plug is in it. sample-messages.txt has a few.
#
# Each reading becomes a number on your dashboard. One the meter does not
# have arrives empty and is left out.

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
