# System monitor: email when a disk passes LIMIT percent full. One email per
# disk, then nothing more until it is LIMIT or fuller again after dropping
# under LIMIT - 5, or after going a week unreported, so a disk that sits at
# 91% does not mail you every minute.

LIMIT = 90
WEEK = 7 * 24 * 60                       # in minutes

def routine(event, state, mem):
    now = event["ts_ms"] // 60000        # minutes: a short number to keep
    for key in list(mem.keys()):         # disks unreported for a week
        if now - mem[key] > WEEK:
            mem.pop(key)
    for name, used in event["fields"].items():
        if not name.endswith(" used %"):
            continue
        disk = name[:-len(" used %")]
        # One latch per computer and disk, under a short name (mem holds 4 KiB,
        # room for about 180 full disks), keeping the minute it was last reported.
        key = hex(crypto.sha256(event["hw_id"] + " " + disk))[:10]
        if used >= LIMIT and key not in mem:
            free = event["fields"].get(disk + " free GB", "?")
            alert("%s: %s is %s%% full" % (event["hw_id"], disk, used),
                  ("%s on %s is %s%% full, %s GB free. You will hear about it again the next " +
                   "time it is %d%% or fuller after dropping under %d%%, or after going a week " +
                   "unreported.") % (disk, event["hw_id"], used, free, LIMIT, LIMIT - 5))
            mem[key] = now
        elif key in mem:
            if used < LIMIT - 5:
                mem.pop(key)
            else:
                mem[key] = now           # still at LIMIT - 5 or more: keep it
