# Tasmota: email when the plug's power passes a limit. One email, then nothing
# more until the power has come back MARGIN past the limit, so a load that sits
# near the line does not mail you every minute. And never more than one email
# an hour for each plug: a heater that cycles would otherwise use up the
# account's 10 alert emails an hour, which every other alert needs too.
#
# Out of the box it watches "Power" (watts) and mails you when it goes over
# 1000 W. To be told when something stops instead (a washer that finished),
# set LIMIT to 5 and BELOW to True: you get one email the first time it sees
# the plug idle, then one after each run that went over LIMIT + MARGIN.

FIELD = "Power"            # the reading to watch, as it shows on the device's page
LIMIT = 1000               # the line, in watts
BELOW = False              # True: email when it drops under LIMIT; False: over it
MARGIN = 100               # how far back it must come before the next email

NUMBERS = ("int", "float")

HOUR = 60                  # in minutes: at most one email an hour per plug
WEEK = 7 * 24 * 60         # in minutes

def routine(event, state, mem):
    # mem keeps one number for each device it has emailed about. While the
    # power is past the line, it is the minute the power last said so. Once the
    # power has come back, it is that minute with a minus sign, and the next
    # email waits until an hour after it. A device not heard from for a week
    # is forgotten, and one back for an hour needs nothing kept, so deleted
    # devices do not fill mem up.
    now = event["ts_ms"] // 60000
    for key in list(mem.keys()):
        seen = mem[key]
        back_an_hour = seen < 0 and now + seen >= HOUR
        if now - abs(seen) > WEEK or back_an_hour:
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
