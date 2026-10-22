# ESPHome

## What happens

Your ESPHome device sends its readings to [Warblet](https://warbletiot.com) once a minute over HTTPS. Warblet shows them on a dashboard, keeps messages for seven days, and emails you when a reading passes a limit you choose.

You add a few lines to your device's YAML. They include [warblet.yaml](warblet.yaml) from this folder at a release tag, so you can read every line your device runs. Nothing is installed from Warblet's page.

## What you need

- An ESP32 board running ESPHome 2026.10 or newer.
- Wi-Fi with internet access.
- A Warblet account, and an email address for the alerts.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash) and choose **ESPHome**.
2. Put the line the page shows into your `secrets.yaml`. It holds your setup code; keep it private.
3. Add the `packages:` lines the page shows to your device's YAML, then install as usual.

## What it sends

Every sensor, binary sensor and text sensor on your device, by the name you gave it, plus `Wi-Fi signal` and `Uptime`. A sensor you mark `internal: true` stays on the device.

## Commands

[warblet-commands.yaml](warblet-commands.yaml) asks Warblet for a command every 15 seconds and puts its text in the `Warblet command` text sensor. Your own YAML says what each command does; [xiao-esp32s3.yaml](xiao-esp32s3.yaml) turns its LED on and off.

## Email

The routine **ESPHome: email when a reading passes a limit** is added to your account, switched on. It emails once when the Wi-Fi signal drops below -80 dBm, and again only after it has come back to -75 dBm and drops again, or drops again after a week without that reading.

## Make it real

Name the sensors you care about, then change the reading and the limit in [limit-email.star](limit-email.star).
