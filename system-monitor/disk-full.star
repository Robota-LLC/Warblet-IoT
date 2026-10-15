# System monitor: email when a disk passes LIMIT percent full. One email per
# disk, then nothing more until that disk is back under LIMIT - 5, so a disk
# that sits at 91% does not mail you every minute.

LIMIT = 90

def routine(event, state, mem):
    for name, used in event["fields"].items():
        if not name.endswith(" used %"):
            continue
        disk = name[:-len(" used %")]
        key = event["hw_id"] + " " + disk        # one latch per computer and disk
        if used >= LIMIT and not mem.get(key):
            free = event["fields"].get(disk + " free GB", "?")
            alert("%s: %s is %s%% full" % (event["hw_id"], disk, used),
                  ("%s on %s is %s%% full, %s GB free. You will hear again " +
                   "only after it drops under %d%% and fills up again.") %
                  (disk, event["hw_id"], used, free, LIMIT - 5))
            mem[key] = True
        elif used < LIMIT - 5 and mem.get(key):
            mem.pop(key)
