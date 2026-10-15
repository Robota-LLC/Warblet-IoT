# System monitor

## What happens

Your computer sends disk usage and system readings to [Warblet](https://warbletiot.com) about once a minute over HTTPS. Warblet shows them on a dashboard, keeps messages for seven days, and emails you when a disk reaches 90% full. The script sends readings; it does not receive commands.

## What you need

- A computer running Linux, Raspberry Pi OS, macOS or Windows.
- Python 3.8 or newer. The script uses only the standard library; there is nothing to build.
- Internet access and a Warblet account.
- `curl` for the download command shown during setup.
- A terminal for the first run, and an email address for disk alerts.

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

On Windows, use `python` instead of `python3`.

For automatic startup, first complete setup interactively. Configure your operating system's scheduler or service manager to run the script as the same user, with absolute paths to Python and the script. The script does not install a startup service.

## Readings and alerts

The report includes hostname, operating system, memory use and uptime when available. Linux and macOS also report one-minute load. Linux can report its first thermal-zone sensor's temperature. Unavailable readings are omitted.

Up to eight local disks report used percentage and free space. Windows uses fixed drives; Linux uses supported writable local filesystems; macOS uses `/` and visible, writable local volumes. Network shares are excluded. Free space labelled **GB** uses 1,073,741,824 bytes per unit.

The disk routine emails once per disk at **90% or higher**. It rearms only after usage drops **below 85%**. Change `LIMIT` in the routine to adjust the threshold; rearming remains five percentage points below it.

For an always-on computer, enable **Tell me if this board goes quiet** on its device page; a sleeping computer also looks quiet.

## Keys and identity

An unused setup code expires after three days and can claim one device. Once used, it becomes that device's persistent token.

The script saves its ID and key in `~/.warblet-monitor.json`. Anyone who can read that file can send readings as this computer. Keep it out of shared files and Git.

After rotating the device's token, stop the script and run:

```sh
python3 warblet_monitor.py --setup
```

Paste the replacement token. This preserves the device ID. A copied key file, including one on a cloned disk, reuses the same identity. For a separate computer, remove that file on the copy and start with a new setup code.

## Troubleshooting

- **`accepted`:** Warblet accepted the report; check the dashboard for readings.
- **`unclaimed`:** stop the script, prepare a new setup code, then restart with `--setup`.
- **`401`:** check the saved token; after rotation, replace it with `--setup`.
- **`will try again`:** check internet access. The script waits a minute before another report attempt.
- **`not set up yet`:** run once in an interactive terminal before starting it through a scheduler.

## Make it real

Add your own sensor or service readings in `readings()` in [warblet_monitor.py](warblet_monitor.py).

The JSON decoder passes through top-level values and flattens disk readings. Change [decoder.star](decoder.star) when changing that structure. Keep the disk field names aligned with [disk-full.star](disk-full.star), or adapt the routine for your own alerts. Save decoder and routine changes in Warblet too; editing local files alone does not update your account. Verify new readings on the dashboard, and keep credentials out of source and logs.
