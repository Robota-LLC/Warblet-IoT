# Scope to the button spec. A message sent because of a press keeps the
# board's press count on the device and queues a snap command for the camera.

# Set this to the camera that should receive snap commands.
CAMERA_HW_ID = "your-camera-hw-id"

def routine(event, state, mem):
    if event["fields"].get("on_press"):
        # The board counts its own presses; keep the count on the device for the
        # email routine to read.
        track("presses", event["fields"].get("presses", 0))
        send(CAMERA_HW_ID, b"snap")
