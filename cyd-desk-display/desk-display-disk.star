# Desk display: show the fullest disk of each computer running the System
# monitor on every desk display in this account. A line goes out when it
# differs from the last one sent (with two computers, their lines take
# turns), and to a display that has just started: its first report is
# tagged "hello". Nothing to type in: both demos are found by name.

MONITORS = ["demo-system-monitor"]
DISPLAYS = ["demo-cyd-desk-display"]
FRESH = 10                               # minutes: an older line is not given to a display that starts

def fullest_disk(fields):
    """One short line about the disk closest to full, or None."""
    best, used = None, -1
    for name, value in fields.items():
        if name.endswith(" used %") and value > used:
            best, used = name[:-len(" used %")], value
    if best == None:
        return None
    line = "%d%% full: %s %s" % (int(used + 0.5), fields.get("host", "?"), best)
    return line[:40]                     # two lines of the screen; the number comes first

def routine(event, state, mem):
    hw = event["hw_id"]
    minute = event["ts_ms"] // 60000
    if event["tag"] == "hello":          # a display just started: give it the last line
        if minute - mem.get("minute", 0) <= FRESH and hw in devices_from(DISPLAYS):
            send(hw, "info:" + mem["line"])
        return
    line = fullest_disk(event["fields"])
    if line == None or hw not in devices_from(MONITORS):
        return                           # not a System monitor report
    if line != mem.get("line"):
        for display in devices_from(DISPLAYS):
            send(display, "info:" + line)
    mem["line"] = line                   # the last line, and the minute it was last true
    mem["minute"] = minute
