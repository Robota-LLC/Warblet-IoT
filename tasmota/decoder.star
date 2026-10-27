# Tasmota: one decoder for every message the plug sends.
#
# Tasmota ends each topic with a word (STATE, SENSOR, RESULT, LWT, ...) and
# Warblet keeps that word as the message's tag. A decoder sees only the bytes,
# so this one tells the messages apart by what is in them:
#   STATE   {"UptimeSec":3600,"Heap":25,"MqttCount":1,"POWER":"ON","Wifi":{"Signal":-61,...},...}
#   SENSOR  {"ENERGY":{"Power":42,"Voltage":121,"Current":0.413,"Today":0.123,...}}
#   RESULT  {"POWER":"OFF"}           after a command
#   POWER   OFF                       a plain word
#   LWT     Online                    a plain word, each time it connects
# sample-messages.txt has one of each.
#
# Only readings become fields. STATE also carries your Wi-Fi network's name,
# your router's address and the plug's own address: they stay in the stored
# message and are never made fields.

ENERGY = {                         # Tasmota's name: the name on your dashboard
    "Power": "Power",              # watts
    "Voltage": "Voltage",          # volts
    "Current": "Current",          # amps
    "Factor": "Power factor",
    "Today": "Energy today",       # kWh since midnight, by the plug's clock
    "Yesterday": "Energy yesterday",
    "Total": "Energy total",
}

STATE = {
    "UptimeSec": "Uptime",         # seconds since it started
    "Heap": "Free memory",         # KB
    "MqttCount": "MQTT connects",  # goes up by one each time it connects again
}

NUMBERS = ("int", "float")

def relay(word):
    word = word.strip().upper()
    if word == "ON" or word == "OFF":
        return word
    return None

def decode(payload, meta):
    msg = json.decode(payload, None)
    if type(msg) != "dict":              # a plain word, like OFF or Online
        text = str(payload).strip()
        if not text:
            return {}
        if relay(text):
            return {"Relay": relay(text)}
        return {"Status": text[:40]}

    fields = {}
    energy = msg.get("ENERGY")
    if type(energy) == "dict":
        for name, label in ENERGY.items():
            if type(energy.get(name)) in NUMBERS:
                fields[label] = energy[name]
    for name, label in STATE.items():
        if type(msg.get(name)) in NUMBERS:
            fields[label] = msg[name]
    for name in ("POWER", "POWER1"):     # the relay: in STATE, and after a command
        if type(msg.get(name)) == "string" and relay(msg[name]):
            fields["Relay"] = relay(msg[name])
    wifi = msg.get("Wifi")
    if type(wifi) == "dict" and type(wifi.get("Signal")) in NUMBERS:
        fields["Wi-Fi signal"] = wifi["Signal"]        # dBm
    info = msg.get("Info1")
    if type(info) == "dict":
        for name, label in (("Module", "Module"), ("Version", "Firmware")):
            if type(info.get(name)) == "string":
                fields[label] = info[name][:80]
    info = msg.get("Info3")
    if type(info) == "dict":
        if type(info.get("RestartReason")) == "string":
            fields["Restart reason"] = info["RestartReason"][:80]
        if type(info.get("BootCount")) in NUMBERS:
            fields["Boot count"] = info["BootCount"]
    return fields
