# ESPHome: the device sends one JSON object of its readings, by name, like
#   {"Wi-Fi signal": -61, "Uptime": 3600, "Chip temperature": 41.3, "Door": false}
# Every reading becomes a field as it is. A reading with no value yet is left
# out, and a device with more than 64 readings sends its first 64.

def decode(payload, meta):
    fields = {}
    for name, value in json.decode(payload).items():
        if value != None and len(fields) < 64:
            fields[name] = value
    return fields
