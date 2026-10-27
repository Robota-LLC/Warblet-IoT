# Starlark decoder: decode(payload, meta) processes incoming payload bytes.
# Return a dict of named fields for Warblet dashboards and routines.
# JPEGs return bytes and a ready camera status. Camera health JSON returns
# its status fields; other payloads return only their size as bytes.

def decode(payload, meta):
    # Only a small JSON payload with warbletCameraHealth: 1 is a health report.
    if len(payload) <= 192 and payload[:1] == b"{":
        health = json.decode(payload, None)
        if type(health) != "dict" or type(health.get("warbletCameraHealth")) != "int" or health.get("warbletCameraHealth") != 1:
            return {"bytes": len(payload)}
        if health.get("camera") not in ["initializing", "ready", "unavailable", "stalled"]:
            fail("invalid camera state")
        if health.get("snap") not in ["none", "failed"]:
            fail("invalid snap result")
        for field in ["error", "attempts", "uptime_s"]:
            if type(health.get(field)) != "int":
                fail("invalid camera health number")
        if health["attempts"] < 0 or health["attempts"] > 3 or health["uptime_s"] < 0:
            fail("invalid camera health range")
        return {"warbletCameraHealth": 1, "camera": health["camera"],
                "error": health["error"], "attempts": health["attempts"],
                "uptime_s": health["uptime_s"], "snap": health["snap"]}
    if len(payload) >= 3 and payload[:3] == b"\xff\xd8\xff":
        return {"bytes": len(payload), "warbletCameraHealth": 1,
                "camera": "ready", "error": 0, "snap": "none"}
    return {"bytes": len(payload)}
