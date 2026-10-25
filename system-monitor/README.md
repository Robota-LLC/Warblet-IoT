# System monitor

## What happens

Your computer sends disk usage and system readings to [Warblet](https://warbletiot.com) about once a minute over HTTPS. Warblet shows them on a dashboard and emails you when a disk reaches 90% full. The script sends readings; it does not receive commands.

Incoming messages have up to seven days of history, subject to your plan and available storage.

## What you need

- A computer running Linux, Raspberry Pi OS, macOS or Windows.
- Python 3.8 or newer. The script uses only the standard library; there is nothing to build.
- Internet access and a Warblet account.
- `curl` (or Git) to download the script.
- A terminal for the first run, and an alert email address.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash). Choose **Computer & other devices**, then **System monitor** if a choice appears. Sign in when asked.
2. Choose **Prepare setup** and supply an alert email if asked. Setup prepares the demo's spec, including its decoder, and enables its disk-alert routine.
3. Under **Instructions for**, choose your operating system. In a terminal in your chosen folder, run the first command to download `warblet_monitor.py` from this repository's release tag on GitHub. Run the second command to start it.
4. Copy **Setup code** from the page and paste it at the script's prompt. The code stays hidden as you paste. Keep it private.
5. Leave the script running. When the page says **Your device is sending data**, choose **View device**.

<details>
<summary>Download with Git instead</summary>

After preparing setup, replace `RELEASE_TAG` below with the tag in the page's download command, then run:

```sh
git clone --depth 1 --branch RELEASE_TAG https://github.com/Robota-LLC/Warblet-IoT.git
cd Warblet-IoT/system-monitor
python3 warblet_monitor.py
```

On Windows, use `python` instead of `python3`. Paste the setup code when asked, then continue with step 5. This gets the same script as the setup page's download command.

</details>

## Run and stop

Press **Ctrl-C** to stop. To restart, open a terminal in the script's folder and run:

```sh
python3 warblet_monitor.py
```

On Windows, use `python` instead of `python3`; if Windows says Python was not found, use `py`.

For automatic startup, first complete setup interactively. Use the Linux instructions below, or configure your operating system's scheduler to run the script as the same user, with absolute paths to Python and the script. The script does not install a startup service.

A copy already running this way uses what a later `--setup` saves from its next report (within the hour while Warblet does not know the computer); it need not be restarted.

### Start at boot on Linux or Raspberry Pi OS

On a system using systemd, complete setup without `sudo`, confirm a report arrives, then stop the terminal copy with **Ctrl-C**. Keep the script in a permanent folder.

This exact service configuration has not been tested through installation and reboot. After enabling it, confirm reports arrive and resume after a reboot before relying on automatic startup.

Create `/etc/systemd/system/warblet-monitor.service` with `sudoedit /etc/systemd/system/warblet-monitor.service` and paste:

```ini
[Unit]
Description=Warblet system monitor
Wants=network-online.target
After=network-online.target

[Service]
User=YOUR_USER
Environment="HOME=/home/YOUR_USER"
ExecStart="/usr/bin/python3" -u "/home/YOUR_USER/Warblet-IoT/system-monitor/warblet_monitor.py"
Restart=always
RestartSec=60

[Install]
WantedBy=multi-user.target
```

Replace `YOUR_USER` with the user who completed setup, set `HOME` to that user's actual home folder, and replace the Python and script paths with their full paths. `command -v python3` shows the Python path. Keep the quotes around paths, including any that contain spaces; do not put `~` or `$HOME` in this file.

The service reads `HOME/.warblet-monitor.json`, so using the same home folder preserves your computer's identity. Then start it and enable startup at boot:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now warblet-monitor
journalctl -u warblet-monitor -f
```

**Ctrl-C** exits the log view without stopping the service. Use `sudo systemctl stop warblet-monitor` to stop reporting, `sudo systemctl start warblet-monitor` to resume, and `sudo systemctl disable warblet-monitor` to prevent startup at boot. Disabling it does not stop a copy already running.

## Readings and alerts

The report includes hostname, operating system, memory use and uptime when available. Linux and macOS also report one-minute load. Linux can report its first thermal-zone sensor's temperature; on Raspberry Pi OS, this is the processor temperature also reported by `vcgencmd measure_temp`. Unavailable readings are omitted.

Up to eight local disks report used percentage and free space; the script names any it leaves out. Windows uses fixed drives; Linux uses supported writable local filesystems, one per device; macOS uses `/` and visible, writable local volumes, one per APFS container. Network shares are excluded, and a disk that does not answer within 15 seconds is skipped.

On a standard Raspberry Pi OS SD-card installation, the disk readings include `/` and `/boot/firmware`.

Free space labelled **GB** is in GiB: 1,073,741,824 bytes per unit.

The routine is listed as **System monitor: email when a disk is nearly full**. It emails once per disk at **90% or higher**, then again only when the disk is 90% or fuller after usage drops **below 85%** or goes a week unreported. Change `LIMIT` in the routine to adjust the threshold; rearming remains five percentage points below it.

For an always-on computer, enable **Tell me if this device goes quiet** on its device page. It is off by default; a sleeping computer also looks quiet.

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
