# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# The board sends a count, a space, then the last text from the Message box.
# For example, "3 hello" returns count 3 and heard "hello".

def decode(payload, meta):
    line = str(payload)                   # the bytes as text
    count, _, heard = line.partition(" ")
    if not count.isdigit():
        fail("expected a count, a space, then text, like '3 hello'; got: " + line[:40])
    return {"count": int(count), "heard": heard}
