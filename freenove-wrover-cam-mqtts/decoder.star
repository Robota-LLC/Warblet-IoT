# Optional decoder: report JPEG size alongside the stored image.

def decode(payload, meta):
    return {"bytes": len(payload)}
