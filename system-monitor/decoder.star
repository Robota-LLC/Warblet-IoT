# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# The computer sends a JSON object. Non-null top-level readings keep their
# names; each disk becomes "<path> used %" and "<path> free GB" fields.

def decode(payload, meta):
    data = json.decode(payload)
    fields = {}
    for name, value in data.items():
        if name != "disks" and value != None:
            fields[name] = value
    for disk, numbers in data.get("disks", {}).items():
        if len(disk) > 60:                       # a long path keeps its end, the part
            disk = "..." + disk[-57:]            # a person knows, so names stay short
        fields[disk + " used %"] = numbers["used_pct"]
        fields[disk + " free GB"] = numbers["free_gb"]
    return fields
