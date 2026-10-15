#!/usr/bin/env python3
# Warblet system monitor. Once a minute this script tells Warblet how full this
# computer's disks are, plus a few vitals: memory in use, load, uptime and, on a
# Raspberry Pi, the chip's temperature. Your device page shows them, and a routine
# emails you when a disk is nearly full. To hear when the computer stops reporting,
# switch on "Tell me if this board goes quiet" on its device page. It only sends:
# nothing comes back from Warblet.
# Python 3.8 or newer, nothing to install: Linux (Raspberry Pi OS too), macOS
# and Windows.
#
#   python3 warblet_monitor.py                    the first time, it asks for your code
#   python3 warblet_monitor.py --setup            paste a new code or token
#   python3 warblet_monitor.py --setup --new-id   ... and take a new id as well
import ctypes, getpass, json, os, platform, re, secrets, shutil  # all part of Python
import socket, subprocess, sys, threading, time, urllib.error, urllib.request, zlib
from pathlib import Path

HOST = "https.warbletiot.com"                     # Warblet's HTTPS door
URL = "https://" + HOST + "/ingest/"
KEY_FILE = Path.home() / ".warblet-monitor.json"  # this computer's id and code
CODE = r"[A-Za-z0-9_-]{8,200}"                    # a code or a token from Warblet
ID = r"[A-Za-z0-9:_.-]{1,128}"                    # an id Warblet accepts
REPORT_S = 60      # a report every minute
UNKNOWN_S = 3600   # once an hour while Warblet does not know this computer: only
                   # --setup here mends that, and each try hands Warblet its name and disks
WAIT_S = 15        # each step of a request waits 15 seconds at most
SYSTEM = platform.system()                        # "Linux", "Darwin" or "Windows"
LOCAL = {"ext2", "ext3", "ext4", "xfs", "btrfs", "f2fs", "vfat", "exfat",
         "ntfs", "ntfs3", "fuseblk", "zfs"}       # Linux disks, never network shares


def say(*words):
    try:
        print(time.strftime("%H:%M:%S"), *words, flush=True)
    except (OSError, ValueError):                # its terminal closed, its log's disk full:
        pass                                     # it keeps reporting, unseen


SAID = set()


def once(what, *words):
    """Say something the first time only, not every minute."""
    if what not in SAID:
        SAID.add(what)
        say(*words)


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
    # "/dev/mmcblk0p2 / ext4 rw,noatime 0 0". A path's bytes are kept as they are,
    # even ones that are not UTF-8 (a stick named on an old system).
    for line in open("/proc/mounts", encoding="utf-8", errors="surrogateescape"):
        device, path, kind, options = line.split()[:4]
        if kind in LOCAL and "ro" not in options.split(",") and device not in found:
            # A space is written \040, a tab \011, a backslash \134: decode them all.
            found[device] = re.sub(r"\\([0-7]{3})", lambda m: chr(int(m.group(1), 8)), path)
    return list(found.values())


STUCK = {}                                       # disks that did not answer, by path


def usage(path):
    """shutil.disk_usage, waiting WAIT_S at most. A disk whose driver hangs (a USB
    disk through NTFS-3G, or another FUSE driver) is left out, not waited for."""
    if path in STUCK and STUCK[path].is_alive():   # still not answering: not asked again
        raise TimeoutError("it did not answer in %d s" % WAIT_S)
    answer = []

    def ask():
        try:
            answer.append(shutil.disk_usage(path))
        except Exception as error:               # said by readings(), as any disk's is
            answer.append(error)
    asking = threading.Thread(target=ask, daemon=True)
    asking.start()
    asking.join(WAIT_S)
    if not answer:
        STUCK[path] = asking
        raise TimeoutError("it did not answer in %d s" % WAIT_S)
    if isinstance(answer[0], Exception):
        raise answer[0]
    return answer[0]


def label(path, clash=0):
    """The name a disk is sent under: "C:" for "C:\\", and a path longer than 60
    bytes cut to "..." and its end, the part a person knows. Its fields' names
    ("<disk> used %") stay short enough for the dashboard's history. When two
    long paths end the same, the second also carries four hex digits of its own
    checksum, the same every minute, so neither disk hides the other."""
    path = path.rstrip("\\") or path
    # A byte that is not UTF-8 is shown as the replacement mark, U+FFFD.
    path = path.encode("utf-8", "surrogateescape").decode("utf-8", "replace")
    mark = "~%04x" % (zlib.crc32(("%d %s" % (clash, path)).encode()) & 0xFFFF) if clash else ""
    if len(path.encode()) + len(mark) > 60:
        while len(path.encode()) > 57 - len(mark):
            path = path[1:]
        path = "..." + path
    return path + mark


def memory_pct():
    if SYSTEM == "Linux":
        info = {}
        for line in open("/proc/meminfo"):       # "MemAvailable:  2210436 kB"
            name, value = line.split(":")
            info[name] = int(value.split()[0])
        return 100 - 100 * info["MemAvailable"] / info["MemTotal"]
    if SYSTEM == "Darwin":                       # "System-wide memory free percentage: 63%"
        return 100 - int(re.search(r"percentage: (\d+)", run("/usr/bin/memory_pressure")).group(1))

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
        seconds = time.time() - int(re.search(r"sec = (\d+)", run("/usr/sbin/sysctl -n kern.boottime")).group(1))
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
    data["disks"], containers, left = {}, set(), []
    try:
        paths = disks()
        if not paths:                            # a Raspberry Pi with its overlay root on
            once("no disks", "found no disk to report: it sends local disks it can write to, and"
                 " a read-only or overlay root is not one; the other readings still go")
    except Exception as error:
        paths = []
        once("disks", "cannot list the disks:", error)
    for path in paths:
        try:
            total, used, free = usage(path)
            if SYSTEM == "Darwin":               # APFS volumes share one container's space:
                used = total - free              # count what all of them use, not one
                if (total, free) in containers:  # and its numbers name the container:
                    continue                     # send each container once
                containers.add((total, free))
            if len(data["disks"]) == 8:          # 8 disks at most, once merged
                left.append(label(path))
                continue
            name, clash = label(path), 0
            while name in data["disks"]:         # two long paths that end the same
                clash += 1
                name = label(path, clash)
            data["disks"][name] = {
                "used_pct": round(100 * used / (used + free), 1),   # the way df counts
                "free_gb": round(free / 2**30, 1)}
        except Exception as error:               # a locked drive, another user's mount:
            once(path, "cannot read disk", label(path), "-", error)   # the others still go
    if left:
        once("left out " + " ".join(left), "8 disks at most, so these are not sent:", ", ".join(left))
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
    """Send one report and say Warblet's answer. Returns how long to wait."""
    # The code from Warblet is this computer's token. Sent as a claim as well, it
    # adds the computer to your account the first time; after that it is ignored.
    headers = {"X-Chirp-Token": me["key"], "X-Chirp-Claim": me["key"],
               "Content-Type": "application/json"}
    status, reply = call(me["id"], json.dumps(readings()).encode(), headers)
    say("report ->", status, reply.decode(errors="replace").strip())
    if b"unclaimed" in reply:
        once("unclaimed", "Warblet does not know this computer: its code was mistyped, used, expired,"
             " or its spec or device was deleted. Get a new code and run:", typed("--setup"),
             "(this copy tries again once an hour, with what that saves)")
        return UNKNOWN_S
    if status == 401:
        once("401", "Warblet refused the code. If you rotated the token, run --setup and paste it;"
             " this copy uses it from its next report. If this computer never reached your account,"
             " another device has its id: get a new code and run:", typed("--setup --new-id"))
    return REPORT_S


def typed(options=""):
    """The command a person types here: python on Windows, python3 elsewhere."""
    return (("python" if SYSTEM == "Windows" else "python3") + " warblet_monitor.py " + options).strip()


def can_ask():
    """Can a person paste the code here? Not in a service, a scheduled task, cron,
    pythonw or Git Bash: there is no terminal to type into."""
    if sys.stdin is None or not sys.stdin.isatty():
        return False
    if SYSTEM != "Windows":
        return True
    import msvcrt                                # Windows calls NUL a terminal too:
    try:                                         # ask for the console behind the input
        handle = msvcrt.get_osfhandle(sys.stdin.fileno())
    except (OSError, ValueError):
        return False
    return bool(ctypes.windll.kernel32.GetConsoleMode(ctypes.c_void_p(handle), ctypes.byref(ctypes.c_ulong())))


def setup():
    """Ask for the code from Warblet and keep it where only you can read it."""
    if not can_ask():
        sys.exit("warblet: no terminal here to paste the code into; run it once by hand in a terminal "
                 "(on Windows, PowerShell or Command Prompt): " + typed("--setup"))
    old = None if "--new-id" in sys.argv else saved()   # the id is kept from a good file only
    key = getpass.getpass("Paste the code from Warblet (it stays hidden): ").strip()
    while not re.fullmatch(CODE, key):
        print("That is not a Warblet code: it looks like CHIRP-8FKQ2WMR, letters, digits, - and _.")
        key = getpass.getpass("Paste the code from Warblet (it stays hidden): ").strip()
    # The id: this computer's name and 64 random bits, since many Pis share a name.
    name = re.sub(r"[^a-z0-9]+", "-", socket.gethostname().lower()).strip("-")[:40]
    me = {"id": old["id"] if old else "%s-%s" % (name or "computer", secrets.token_hex(8)), "key": key}
    temp = KEY_FILE.with_name(KEY_FILE.name + ".new")
    try:
        if temp.exists():                        # left by a save that was cut short
            temp.unlink()
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)   # only you, from the start
        with open(fd, "w") as f:
            json.dump(me, f)
        os.replace(temp, KEY_FILE)               # all at once: never half a file
    except OSError as error:                     # a full disk: what was there stays
        try:
            temp.unlink()
        except OSError:
            pass
        sys.exit("warblet: cannot save %s (%s); the old one is unchanged" % (KEY_FILE, error.strerror or error))
    say("saved in", KEY_FILE, "- only you (and an administrator) can read it")
    return me


def saved():
    """The id and code --setup saved, checked the way a paste is; None if the file
    cannot be read or is not that."""
    try:
        me = json.loads(KEY_FILE.read_text(encoding="utf-8"))
        if re.fullmatch(ID, me["id"]) and re.fullmatch(CODE, me["key"]):
            return {"id": me["id"], "key": me["key"]}
    except Exception:                            # unreadable, not JSON, not text, too deep
        pass
    return None


def main():
    if "--new-id" in sys.argv and "--setup" not in sys.argv:
        sys.exit("warblet: --new-id goes with --setup: " + typed("--setup --new-id"))
    me = setup() if "--setup" in sys.argv or not KEY_FILE.exists() else saved()
    if not me:
        sys.exit("warblet: cannot use %s; run %s (this computer then gets a new id)" % (KEY_FILE, typed("--setup")))
    say("reporting as", me["id"], "- see it at https://warbletiot.com/app")
    while True:
        now = saved()                            # --setup, run again by hand, saved another
        if now and now != me:                    # code: this copy uses it too (a service)
            me = now
            say("reporting as", me["id"], "with what --setup saved")
        wait = REPORT_S                          # a failed report waits a minute too
        try:
            wait = report(me)
        except Exception as error:               # no network, no answer: try again
            say("will try again:", error)
        time.sleep(wait)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        say("stopped")
