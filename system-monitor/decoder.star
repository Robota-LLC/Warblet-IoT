# System monitor: the computer sends one JSON object, like
#   {"host": "kitchen-pi", "os": "Linux", "uptime_h": 51.2, "load": 0.12,
#    "memory_pct": 38.5, "temp_c": 47.2,
#    "disks": {"/": {"used_pct": 61.4, "free_gb": 11.3}}}
# Every reading becomes a field as it is. Each disk becomes two fields,
# "/ used %" and "/ free GB", so the dashboard shows one line per disk and the
# routine can watch every disk the same way.

def decode(payload, meta):
    data = json.decode(payload)
    fields = {}
    for name, value in data.items():
        if name != "disks" and value != None:
            fields[name] = value
    for disk, numbers in data.get("disks", {}).items():
        fields[disk + " used %"] = numbers["used_pct"]
        fields[disk + " free GB"] = numbers["free_gb"]
    return fields
