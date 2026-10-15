# System monitor

## What happens

Your computer sends disk usage and system readings to [Warblet](https://warbletiot.com) about once a minute over HTTPS. Warblet shows them on a dashboard, keeps messages for seven days, and emails you when a disk reaches 90% full. The script sends readings; it does not receive commands.

## What you need

- A computer running Linux, Raspberry Pi OS, macOS or Windows.
- Python 3.8 or newer. The script uses only the standard library; there is nothing to build.
- Internet access and a Warblet account.
- `curl` for the download command shown during setup.
- A terminal for the first run, and an alert email address.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash). Choose **Computer & other devices**, then **System monitor** if a choice appears. Sign in when asked.
2. Choose **Prepare setup** and supply an alert email if asked. Setup prepares the demo's spec, including its decoder, and enables its disk-alert routine.
3. Under **Instructions for**, choose your operating system. Run the displayed commands in a terminal in your chosen folder. They download `warblet_monitor.py` and start it.
4. Copy **Setup code** from the page and paste it at the script's prompt. The code stays hidden as you paste. Keep it private.
5. Leave the script running. When the page says **Your device is sending data**, choose **View device**.

## Run and stop

Press **Ctrl-C** to stop. To restart, open a terminal in the script's folder and run:

```sh
python3 warblet_monitor.py
```

On Windows, use `python` instead of `python3`; if Windows says Python was not found, use `py`.

For automatic startup, first complete setup interactively. Configure your operating system's scheduler or service manager to run the script as the same user, with absolute paths to Python and the script. The script does not install a startup service.

A copy already running this way uses what a later `--setup` saves from its next report (within the hour while Warblet does not know the computer); it need not be restarted.

## Readings and alerts

The report includes hostname, operating system, memory use and uptime when available. Linux and macOS also report one-minute load. Linux can report its first thermal-zone sensor's temperature. Unavailable readings are omitted.

Up to eight local disks report used percentage and free space; the script names any it leaves out. Windows uses fixed drives; Linux uses supported writable local filesystems, one per device; macOS uses `/` and visible, writable local volumes, one per APFS container. Network shares are excluded, and a disk that does not answer within 15 seconds is skipped.

Free space labelled **GB** is in GiB: 1,073,741,824 bytes per unit.

The routine is listed as **System monitor: email when a disk is nearly full**. It emails once per disk at **90% or higher**, then again only when the disk is 90% or fuller after usage drops **below 85%** or goes a week unreported. Change `LIMIT` in the routine to adjust the threshold; rearming remains five percentage points below it.

For an always-on computer, enable **Tell me if this board goes quiet** on its device page. It is off by default; a sleeping computer also looks quiet.

## Keys and identity

An unused setup code can claim one device until it expires. Once used, it becomes that device's persistent token.

The script saves its ID and key in `~/.warblet-monitor.json`. Anyone who can read that file can send readings as this computer; keep it out of shared files and Git.

After rotating the device's token, run the command below and paste the new token. A copy already running, such as a service, uses it from its next report; if one is, press **Ctrl-C** after the paste so only it reports:

```sh
python3 warblet_monitor.py --setup
```

This preserves the device ID. A copied key file, including one on a cloned disk, reuses the same identity. For a separate computer, get a new setup code and run `python3 warblet_monitor.py --setup --new-id` on the copy.

## Troubleshooting

- **`accepted`:** Warblet accepted the report; check the dashboard for readings.
- **`unclaimed`:** prepare a new setup code and run `--setup`; a copy already running, such as a service, tries again once an hour with what that saves. If one is, press **Ctrl-C** in the copy you started by hand once it reports.
- **`401`:** if you rotated the token, run `--setup` and paste the new one. A running copy uses it from its next report. If this computer never reached your account, another device has its ID: get a new setup code and run `python3 warblet_monitor.py --setup --new-id` (on Windows, use `python`).
- **`will try again`:** check internet access. If the line mentions a certificate, check the date and time; on a Mac with Python from python.org, run Install Certificates in its Python folder once. The script waits a minute before another report attempt.
- **`no terminal here to paste the code into`:** run `python3 warblet_monitor.py --setup` once by hand in a terminal (on Windows, PowerShell or Command Prompt; in Git Bash, `winpty python warblet_monitor.py --setup`), then start it through the scheduler. If you were using `--new-id`, keep it in that command.
- **`cannot use ~/.warblet-monitor.json`:** run `python3 warblet_monitor.py --setup` and paste a code; this computer gets a new ID.

## Make it real

Add your own sensor or service readings in `readings()` in [warblet_monitor.py](warblet_monitor.py).

The JSON decoder passes through top-level numbers, text and true/false values and flattens disk readings; any other nested value makes the whole message fail to decode. Change [decoder.star](decoder.star) when changing that structure. Keep the disk field names aligned with [disk-full.star](disk-full.star), or adapt the routine for your own alerts. Save decoder and routine changes in Warblet too; editing local files alone does not update your account. Verify new readings on the dashboard, and keep credentials out of source and logs.
