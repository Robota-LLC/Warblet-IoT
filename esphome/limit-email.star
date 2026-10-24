# ESPHome: email when a reading passes a limit. One email, then nothing more
# until the reading has come back MARGIN past the limit, so a value that sits
# near the line does not mail you every minute.
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

WEEK = 7 * 24 * 60         # in minutes

def routine(event, state, mem):
    # mem remembers each device that is past the line, and the minute it last
    # said so. A device not heard from for a week is forgotten, so deleted
    # devices do not fill mem up.
    now = event["ts_ms"] // 60000
    for key in list(mem.keys()):
        if now - mem[key] > WEEK:
            mem.pop(key)
    value = event["fields"].get(FIELD)
    if type(value) not in NUMBERS:         # no reading, or not a number
        return
    passed = value < LIMIT if BELOW else value > LIMIT
    rearm = LIMIT + MARGIN if BELOW else LIMIT - MARGIN
    back = value >= rearm if BELOW else value <= rearm
    device = event["hw_id"]
    if passed and device not in mem:
        alert("%s: %s is %s" % (device, FIELD, value),
              ("%s on %s is %s, past your limit of %s. You will hear about it again " +
               "after it comes back to %s and then passes the limit again, or " +
               "passes it after a week without this reading.") %
              (FIELD, device, value, LIMIT, rearm))
        mem[device] = now
    elif device in mem:
        if back:
            mem.pop(device)
        else:
            mem[device] = now       # still past the line, or close to it
