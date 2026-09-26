# Email camera frames tagged snap, subject to a cooldown.
# Load the triggering frame only after the cooldown check.
# Read the button's tracked count for the email text.

# Set this to the device whose press count should appear in the email.
BUTTON_HW_ID = "your-button-hw-id"
COOLDOWN_MS = 120000

def routine(event, state, mem):
    if event["tag"] != "snap":
        return
    elapsed_since_last_alert_ms = event["ts_ms"] - mem.get("last_alert_queued_ms", 0)
    if elapsed_since_last_alert_ms < COOLDOWN_MS:
        print("snap frame %d ms after the last alert, not mailed"
              % elapsed_since_last_alert_ms)
        return
    frame = raw_latest(event["hw_id"])
    if frame == None:
        print("the snap frame is no longer in the store, nothing to attach")
        return
    alert(
        "Alert: photo",
        ("%s frame taken %s, %d bytes, attached as %s.\n" +
         "%s has been pressed %d times.") %
        (event["hw_id"], frame["ts"], len(frame["data"]), frame["name"],
         BUTTON_HW_ID, state.get(BUTTON_HW_ID, "presses", 0)),
        attachments=[frame],
    )
    # Queued, not delivered: the mail leaves the platform after this returns.
    mem["last_alert_queued_ms"] = event["ts_ms"]
    print("queued mail with %s, %d bytes" % (frame["name"], len(frame["data"])))
