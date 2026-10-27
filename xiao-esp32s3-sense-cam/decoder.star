# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# The bytes field is the payload size, shown alongside the stored JPEG.

def decode(payload, meta):
    return {"bytes": len(payload)}
