#!/usr/bin/env python3
"""Serial provisioning exercise for NUCLEO-WBA65RI.
Probes, pushes settings, and optionally enters DFU. Requires pyserial.
Run with a serial port argument; --help lists the options."""

import argparse
import hashlib
import json
import re
import sys
import time

try:
    import serial  # type: ignore
except ImportError:
    sys.exit("pyserial is missing:  python -m pip install pyserial")


BAUD = 115200
SAMPLE = {
    "name": "chirp-lab",
    "key": "00112233445566778899aabbccddeeff",
    "panid": "1234",
    "xpanid": "1111111122222222",
    "channel": 15,
}

# Values of these keys are credentials: the console shows a fingerprint instead.
SECRET = re.compile(r'"(claim|token|key|pass|dataset)"(\s*:\s*)"((?:[^"\\]|\\.)*)"')


def read_lines(port, seconds, echo=True):
    """Collect whole lines for `seconds`, echoing them as they arrive."""
    out, deadline, buf = [], time.time() + seconds, b""
    while time.time() < deadline:
        chunk = port.read(256)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            out.append(line)
            if echo:
                print(f"    < {line}")
    return out


def redact(line):
    """The line with every credential value replaced by a short SHA-256 fingerprint."""
    return SECRET.sub(lambda m: '"{}"{}"sha256:{}"'.format(
        m.group(1), m.group(2), hashlib.sha256(m.group(3).encode()).hexdigest()[:8]), line)


def send(port, line):
    print(f"    > {redact(line)}")
    port.write((line + "\n").encode("utf-8"))
    port.flush()


def expect(lines, prefix, what):
    for line in lines:
        if line.startswith(prefix):
            print(f"  OK   {what}: {line}")
            return line
    print(f"  FAIL {what}: no line starting with {prefix!r}")
    return None


def main():
    ap = argparse.ArgumentParser(description="CHIRP-PROV v1.5 bench test")
    ap.add_argument("port", help="serial port, e.g. COM13 or /dev/ttyACM0")
    ap.add_argument("--claim", default="CHIRP-BENCH0001", help="claim code to push")
    # Use the plain-HTTP host required by this firmware.
    ap.add_argument("--host", default="http.warbletiot.com")
    ap.add_argument("--name", default=SAMPLE["name"], help="Thread network name")
    ap.add_argument("--key", default=SAMPLE["key"], help="Thread network key (32 hex)")
    ap.add_argument("--dataset", default=None, help="use the dataset form instead (hex TLVs)")
    ap.add_argument("--no-dfu", action="store_true", help="skip the DFU re-entry step")
    ap.add_argument("--bad", action="store_true", help="also push malformed configs and check the error replies")
    ap.add_argument("--blank", action="store_true",
                    help="with --bad: the config page was erased before this run, so the "
                         "no-credential push MUST be refused (an ok is a failure)")
    args = ap.parse_args()

    thread = {"dataset": args.dataset} if args.dataset else {
        "name": args.name,
        "key": args.key,
        "panid": SAMPLE["panid"],
        "xpanid": SAMPLE["xpanid"],
        "channel": SAMPLE["channel"],
    }
    cfg = {"host": args.host, "claim": args.claim, "level": 1, "thread": thread}
    # separators: no spaces — the line must stay one line and stay small
    push = "CHIRP+ " + json.dumps(cfg, separators=(",", ":"))

    failures = 0
    with serial.Serial(args.port, BAUD, timeout=0.2) as port:
        print(f"[open] {args.port} @ {BAUD} 8N1")
        time.sleep(0.3)
        port.reset_input_buffer()

        print("\n[1] probe")
        send(port, "CHIRP?")
        ann = expect(read_lines(port, 2.0), "CHIRP! ", "announce")
        failures += ann is None
        if ann and " v=2" not in ann:
            print("  WARN announce is not v=2 — this firmware predates the thread object")
        if ann and " sig=1" not in ann:
            print("  WARN announce has no sig=1 — this firmware predates v1.5 signing")

        if args.bad:
            # Before a claim is stored. Without --blank, a saved credential may satisfy the merge.
            print("\n[1b] no-credential push (needs a blank store)")
            send(port, 'CHIRP+ {"level":1}')
            got = expect(read_lines(port, 2.0), "CHIRP= ", "reply")
            if got and "err missing-field" in got:
                print("  OK   no credential stored or pushed: refused")
            elif got and got.startswith("CHIRP= ok") and not args.blank:
                print("  SKIP the store already held a credential, so the merge kept it.")
                print("       Not tested. Erase the config page and rerun with --bad --blank.")
            else:
                print("  FAIL wanted err missing-field")
                failures += 1

        print("\n[2] push thread config")
        send(port, push)
        res = expect(read_lines(port, 3.0), "CHIRP= ", "result")
        failures += res is None or not res.startswith("CHIRP= ok")

        if args.bad:
            # Each case fails a per-field rule judged on the push alone, so the
            # claim stored in [2] cannot change the answer.
            print("\n[2b] malformed pushes")
            for line, want in [
                ('CHIRP+ {"claim":"X","thread":{', "err bad-json"),
                ('CHIRP+ {"claim":"X","thread":{"name":"n","key":"tooshort"}}', "err bad-json"),
                ('CHIRP+ {"claim":"X","thread":{"name":"n"}}', "err missing-field"),
                ('CHIRP+ {"claim":"X","token":"Y","ssid":"s"}', "err bad-json"),
                ("CHIRP& nonsense", "err unsupported"),
            ]:
                send(port, line)
                got = expect(read_lines(port, 2.0), "CHIRP= ", "reply")
                if not got or want not in got:
                    print(f"  FAIL wanted {want}")
                    failures += 1

        print("\n[3] power-cycle check")
        print("    Unplug/replug the board (or press B4 reset), then watch for the")
        print("    'CHIRP. cfg ... thread=...' summary line. Listening 12 s...")
        got = read_lines(port, 12.0)
        if any(l.startswith("CHIRP. cfg") for l in got):
            print("  OK   stored config was read back after reset")
        else:
            print("  note no boot summary seen — did the board actually reset?")

        if not args.no_dfu:
            print("\n[4] DFU re-entry (the port will disappear — that is success)")
            send(port, "CHIRP~ dfu")
            expect(read_lines(port, 2.0), "CHIRP~ ok", "dfu ack")
            print("    Now check for a USB DFU device (0483:df11) on the USER USB-C")
            print("    connector, e.g. Device Manager -> 'STM32 BOOTLOADER'.")

    print(f"\n{'PASS' if failures == 0 else f'{failures} FAILURE(S)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
