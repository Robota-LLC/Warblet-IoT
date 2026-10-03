# Two boards, two places, no hub: a press on one board turns on the light
# of the other. Warblet adds this routine when you set up the Olimex button
# demo or the XIAO ESP32-C6 demo from warbletiot.com/flash, and every board
# in your account running either demo is part of the pair.
#
# It wakes for every message in your account (it has no spec), because the
# two boards run different demos. Its minimum interval is 0, so a reading
# that arrives just before a press cannot make it skip the press.

# The demos whose boards light each other.
PAIR_DEMOS = ["demo-olimex-c3", "demo-xiao-c6"]

# A press turns the other board's light ON. Each board's own press turns its
# own light off again (see main.py), so the person who was called clears it.
LIGHT_COMMAND = "led:on"

def routine(event, state, mem):
    if event["tag"] != "press":
        return
    boards = devices_from(PAIR_DEMOS)
    if event["hw_id"] not in boards:
        return  # a press from another board, such as the doorbell camera
    for hw_id in boards:
        if hw_id != event["hw_id"]:
            send(hw_id, LIGHT_COMMAND)
