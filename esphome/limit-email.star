# ESPHome: email when a reading passes a limit. One email, then nothing more
# until the reading has come back MARGIN past the limit, so a value that sits
# near the line does not mail you every minute. And never more than one email
# an hour for each device: a reading that keeps crossing the line would
# otherwise use up the account's 10 alert emails an hour, which every other
# alert needs too.
#
# Out of the box it watches the Wi-Fi signal, which every device with the
# Warblet package sends. To watch something else, change these lines to the
# reading's name as it shows on the device's page. For a tank: "Tank level",
# 20 and True.

FIELD = "Wi-Fi signal"     # the reading to watch
LIMIT = -80                # the line
BELOW = True               # True: email when it drops under LIMIT; False: over it
MARGIN = 5                 # how far back it must come before the next email

NUMBERS = ("int", "float")

HOUR = 60                  # in minutes: at most one email an hour per device
WEEK = 7 * 24 * 60         # in minutes

def routine(event, state, mem):
    # mem keeps one number for each device it has emailed about. While the
    # reading is past the line, it is the minute the reading last said so. Once
    # the reading has come back, it is that minute with a minus sign, and the
    # next email waits until an hour after it. A device not heard from for a
    # week is forgotten, and one back for an hour needs nothing kept, so
    # deleted devices do not fill mem up.
    now = event["ts_ms"] // 60000
    for key in list(mem.keys()):
        if now - abs(mem[key]) > WEEK or (mem[key] < 0 and now + mem[key] >= HOUR):
            mem.pop(key)
    value = event["fields"].get(FIELD)
    if type(value) not in NUMBERS:         # no reading, or not a number
        return
    passed = value < LIMIT if BELOW else value > LIMIT
    rearm = LIMIT + MARGIN if BELOW else LIMIT - MARGIN
    back = value >= rearm if BELOW else value <= rearm
    device = event["hw_id"]
    last = mem.get(device, 0)
    if passed and last <= 0:
        if now + last < HOUR:              # came back less than an hour ago: wait
            return
        alert("%s: %s is %s" % (device, FIELD, value),
              ("%s on %s is %s, past your limit of %s. You will hear about it again, " +
               "at most once an hour, after it comes back to %s and then passes the " +
               "limit again, or passes it after a week without this reading.") %
              (FIELD, device, value, LIMIT, rearm))
        mem[device] = now
    elif last > 0:
        if back:
            mem[device] = -last     # back: the next email waits an hour from here
        else:
            mem[device] = now       # still past the line, or close to it
