# Two boards, two places, no hub: a press on either board sends the other
# board a light command. Any board works if it tags its press messages
# "press" and acts on the command below. Both boards must be in the account
# that owns this routine.
#
# Leave this routine unscoped (no spec) so presses from both boards wake it,
# or scope it to the spec both boards share. Set its minimum interval to 0, so
# a reading that arrives just before a press cannot make the routine skip it.

# Set these to your two boards' hardware IDs.
FIRST_BOARD_HW_ID = "your-first-board-hw-id"
SECOND_BOARD_HW_ID = "your-second-board-hw-id"

# A press turns the other board's light ON. Each board's own press turns its
# own light off again (see main.py), so the person who was called clears it.
LIGHT_COMMAND = "led:on"

def routine(event, state, mem):
    if event["tag"] != "press":
        return
    if event["hw_id"] == FIRST_BOARD_HW_ID:
        send(SECOND_BOARD_HW_ID, LIGHT_COMMAND)
    elif event["hw_id"] == SECOND_BOARD_HW_ID:
        send(FIRST_BOARD_HW_ID, LIGHT_COMMAND)
