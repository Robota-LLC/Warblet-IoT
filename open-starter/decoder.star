# Paste this Starlark decoder into the device spec. The board sends one line
# of text: a count, a space, then the last thing it heard from the Message
# box. "3 hello" is count 3, heard "hello"; "4 hi there" is count 4, heard
# "hi there".

def decode(payload, meta):
    line = str(payload)                   # the bytes as text
    count, _, heard = line.partition(" ")
    if not count.isdigit():
        fail("expected a count, a space, then text, like '3 hello'; got: " + line[:40])
    return {"count": int(count), "heard": heard}
