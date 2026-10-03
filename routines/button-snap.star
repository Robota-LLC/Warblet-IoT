# Scope to the button spec. A message sent because of a press queues a snap
# command for the camera. The board's press count needs no bookkeeping here:
# the decoder's `presses` field is already on the device for other routines.

# Set this to the camera that should receive snap commands.
CAMERA_HW_ID = "your-camera-hw-id"

def routine(event, state, mem):
    if event["fields"].get("on_press"):
        send(CAMERA_HW_ID, b"snap")
