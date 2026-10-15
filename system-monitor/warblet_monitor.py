#!/usr/bin/env python3
# Warblet system monitor. Once a minute this script tells Warblet how full this
# computer's disks are, plus a few vitals: memory in use, load, uptime and, on a
# Raspberry Pi, the chip's temperature. Your device page shows them, a routine
# emails you when a disk is nearly full, and the quiet alert tells you when the
# computer stops reporting. It only sends: nothing comes back from Warblet.
# Python 3.8 or newer, nothing to install: Linux (Raspberry Pi OS too), macOS
# and Windows.
#
#   python3 warblet_monitor.py           the first time, it asks for your code
#   python3 warblet_monitor.py --setup   paste a new code or token
import ctypes, getpass, json, os, platform, re, secrets, shutil  # all part of Python
import socket, subprocess, sys, time, urllib.error, urllib.request
from pathlib import Path

HOST = "https.warbletiot.com"                     # Warblet's HTTPS door
URL = "https://" + HOST + "/ingest/"
KEY_FILE = Path.home() / ".warblet-monitor.json"  # this computer's id and code
REPORT_S = 60      # a report every minute
WAIT_S = 15        # each step of a request waits 15 seconds at most
SYSTEM = platform.system()                        # "Linux", "Darwin" or "Windows"
LOCAL = {"ext2", "ext3", "ext4", "xfs", "btrfs", "f2fs", "vfat", "exfat",
         "ntfs", "ntfs3", "fuseblk", "zfs"}       # Linux disks, never network shares


def say(*words):
    print(time.strftime("%H:%M:%S"), *words, flush=True)


def run(command):
    """A built-in macOS tool's output (this script never runs anything else)."""
    return subprocess.run(command.split(), capture_output=True, text=True,
                          timeout=10).stdout


def disks():
    """The local disks to report. Network shares are skipped: a dead one can hang."""
    if SYSTEM == "Windows":
        kernel = ctypes.windll.kernel32
        mask = kernel.GetLogicalDrives()         # one bit per drive letter, A: first
        drives = [chr(65 + i) + ":\\" for i in range(26) if mask >> i & 1]
        return [d for d in drives if kernel.GetDriveTypeW(d) == 3]   # 3 is a fixed disk
    if SYSTEM == "Darwin":                       # "/dev/disk4s1 on /Volumes/USB (apfs, local, ...)"
        found = ["/"]
        for line in run("/sbin/mount").splitlines():
            path, _, options = line.partition(" on ")[2].rpartition(" (")
            if path.startswith("/Volumes/") and "local" in options and \
                    "read-only" not in options and "nobrowse" not in options:
                found.append(path)
        return found
    found = {}
    for line in open("/proc/mounts"):            # "/dev/mmcblk0p2 / ext4 rw,noatime 0 0"
        device, path, kind, options = line.split()[:4]
        if kind in LOCAL and "ro" not in options.split(",") and device not in found:
            found[device] = path.replace("\\040", " ")   # a space is written \040
    return list(found.values())


def memory_pct():
    if SYSTEM == "Linux":
        info = {}
        for line in open("/proc/meminfo"):       # "MemAvailable:  2210436 kB"
            name, value = line.split(":")
            info[name] = int(value.split()[0])
        return 100 - 100 * info["MemAvailable"] / info["MemTotal"]
    if SYSTEM == "Darwin":                       # "System-wide memory free percentage: 63%"
        return 100 - int(re.search(r"percentage: (\d+)", run("memory_pressure")).group(1))

    class Status(ctypes.Structure):              # Windows' MEMORYSTATUSEX
        _fields_ = [("size", ctypes.c_ulong), ("load", ctypes.c_ulong)] + \
                   [("n%d" % i, ctypes.c_ulonglong) for i in range(7)]
    status = Status(size=ctypes.sizeof(Status))
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
    return status.load                           # percent of memory in use


def uptime_h():
    if SYSTEM == "Linux":
        seconds = float(open("/proc/uptime").read().split()[0])
    elif SYSTEM == "Darwin":                     # "{ sec = 1760400000, usec = 0 } ..."
        seconds = time.time() - int(re.search(r"sec = (\d+)", run("sysctl -n kern.boottime")).group(1))
    else:
        ctypes.windll.kernel32.GetTickCount64.restype = ctypes.c_ulonglong
        seconds = ctypes.windll.kernel32.GetTickCount64() / 1000
    return seconds / 3600


def load():
    return os.getloadavg()[0]                    # programs waiting for the CPU; not on Windows


def temp_c():
    # Linux's first temperature sensor: on a Raspberry Pi, the chip itself.
    return int(open("/sys/class/thermal/thermal_zone0/temp").read()) / 1000


def readings():
    """Everything this computer can tell, as one dict. What it cannot tell is left out."""
    data = {"host": socket.gethostname(), "os": {"Darwin": "macOS"}.get(SYSTEM, SYSTEM)}
    for name, read in [("uptime_h", uptime_h), ("load", load),
                       ("memory_pct", memory_pct), ("temp_c", temp_c)]:
        try:
            data[name] = round(read(), 1)
        except Exception:                        # not on this computer: leave it out
            pass
    data["disks"] = {}
    try:
        for path in disks()[:8]:                 # 8 disks at most
            total, used, free = shutil.disk_usage(path)
            if SYSTEM == "Darwin":               # APFS volumes share one container's space:
                used = total - free              # count what all of them use, not one
            data["disks"][path.rstrip("\\")] = {  # "C:\" is sent as "C:"
                "used_pct": round(100 * used / (used + free), 1),   # the way df counts
                "free_gb": round(free / 2**30, 1)}
    except Exception as error:                   # a drive unplugged as we looked
        say("disks:", error)
    return data


def call(path, body, headers):
    """POST to Warblet. Returns (status, reply); each step waits WAIT_S at most."""
    request = urllib.request.Request(URL + path, data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=WAIT_S) as reply:
            return reply.status, reply.read(4096)
    except urllib.error.HTTPError as error:      # 401, 429 and the like are answers too
        return error.code, error.read(4096)


def report(me):
    # The code from Warblet is this computer's token. Sent as a claim as well, it
    # adds the computer to your account the first time; after that it is ignored.
    headers = {"X-Chirp-Token": me["key"], "X-Chirp-Claim": me["key"],
               "Content-Type": "application/json"}
    status, reply = call(me["id"], json.dumps(readings()).encode(), headers)
    say("report ->", status, reply.decode(errors="replace").strip())
    if b"unclaimed" in reply:
        say("Warblet does not know this computer: its code was used or is over 3 days "
            "old. Get a new one and run: python3 warblet_monitor.py --setup")
    elif status == 401:
        say("Warblet refused the code. If you rotated the token, run --setup and paste it.")


def setup():
    """Ask for the code from Warblet and keep it where only you can read it."""
    if not sys.stdin.isatty():
        sys.exit("warblet: not set up yet; run it once by hand: python3 warblet_monitor.py")
    old = json.loads(KEY_FILE.read_text()) if KEY_FILE.exists() else {}
    key = ""
    while not key:
        key = getpass.getpass("Paste the code from Warblet (it stays hidden): ").strip()
    name = re.sub(r"[^a-z0-9]+", "-", socket.gethostname().lower()).strip("-")[:40]
    me = {"id": old.get("id") or "%s-%s" % (name or "computer", secrets.token_hex(3)),
          "key": key}
    with open(os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as f:
        json.dump(me, f)
    os.chmod(KEY_FILE, 0o600)                    # also when the file was already there
    say("saved in", KEY_FILE, "- only you (and an administrator) can read it")
    return me


def main():
    if "--setup" in sys.argv or not KEY_FILE.exists():
        me = setup()
    else:
        me = json.loads(KEY_FILE.read_text())
    say("reporting as", me["id"], "- see it at https://warbletiot.com/app")
    while True:
        try:
            report(me)
        except Exception as error:               # no network, no answer: try again
            say("will try again:", error)
        time.sleep(REPORT_S)                     # a failed report waits a minute too


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        say("stopped")
