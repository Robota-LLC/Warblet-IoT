# Scope to the doorbell camera's spec. A photo the camera took because its
# button was pressed (tag `press`) is emailed with the photo attached.
# Heartbeat and snap photos stay on the device page and are not mailed.

# The account sends at most 10 alert emails an hour. One email per six
# minutes keeps this routine inside that, even with a stuck button or a busy
# door. Presses inside the cooldown still reach the device page.
COOLDOWN_MS = 360000

def routine(event, state, mem):
    if event["tag"] != "press":
        return
    elapsed_since_last_alert_ms = event["ts_ms"] - mem.get("last_alert_queued_ms", 0)
    if elapsed_since_last_alert_ms < COOLDOWN_MS:
        print("press photo %d ms after the last email, not mailed"
              % elapsed_since_last_alert_ms)
        return
    # The message that triggered this run: the photo of this press.
    photo = raw_latest(event["hw_id"])
    if photo == None:
        print("the press photo is no longer in the store, nothing to attach")
        return
    alert(
        "Doorbell: the button was pressed",
        "%s took this photo at %s: %d bytes, attached as %s." %
        (event["hw_id"], photo["ts"], len(photo["data"]), photo["name"]),
        attachments=[photo],
    )
    # Queued, not delivered: the mail leaves the platform after this returns.
    mem["last_alert_queued_ms"] = event["ts_ms"]
    print("queued mail with %s, %d bytes" % (photo["name"], len(photo["data"])))
