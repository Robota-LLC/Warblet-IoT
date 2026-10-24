# ESPHome

## What happens

Your ESPHome device sends its readings to [Warblet](https://warbletiot.com) once a minute over HTTPS. See them on your dashboard and get an email when a reading passes a limit you choose. Add an optional package to receive commands from Warblet and handle them in your own YAML.

You keep your existing ESPHome project. Warblet supplies the setup code, decoder and alert routine; ESPHome builds and installs the firmware. Incoming messages have up to seven days of history, subject to your plan and available storage.

## What you need

- An ESP32 board with Wi-Fi, running ESPHome 2026.10 or newer.
- An ESPHome device YAML with its board and Wi-Fi already configured.
- Internet access and a Warblet account.
- An alert email address for the routine's emails.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash), choose **Computer & other devices**, then **ESPHome**. Sign in and choose **Prepare setup**.
2. Add the first snippet to `secrets.yaml` beside your device's YAML. Keep the generated secret name: it lets several devices share one secrets file.
3. Add the second snippet to your device's YAML. It includes [warblet.yaml](warblet.yaml) from this repository at the release tag supplied by the page. If you already have a `packages:` section, add its `warblet:` entry there instead of creating a second section.
4. Install through your ESPHome dashboard, or run `esphome run your-device.yaml` from that folder.
5. Allow about a minute after startup for the first report. When Warblet says **Your device is sending data**, choose **View device**.

Use the setup code within 72 hours. Its first report adds the device to your account at **Token**; afterward, the same code remains its token. There is no separate key to copy back.

## Readings

The package sends named sensors, binary sensors and text sensors, plus **Wi-Fi signal** in dBm and **Uptime** in seconds. Mark a sensor `internal: true` to keep it out. Readings without a value are omitted.

The dashboard receives up to 64 fields, with Wi-Fi signal and uptime first. Units are not included; text readings are shortened to 200 bytes without splitting a character. Uptime comes from the preceding sensor update, so it can run about a minute behind.

## Email alerts

Setup enables **ESPHome: email when a reading passes a limit**. It emails once when Wi-Fi signal drops below **-80 dBm**. It can email again after the signal returns to **-75 dBm** and drops again, or after a week without that reading.

To watch something else, open the routine in Warblet and change `FIELD`, `LIMIT` and `BELOW` at the top. For a tank, use `"Tank level"`, `20` and `True`; for chip temperature above 70, use `"Chip temperature"`, `70` and `False`. Save the routine in Warblet; editing [limit-email.star](limit-email.star) locally does not update your account.

Emails are limited to ten per hour across the account. The sample routine can remember up to 113 devices in an active alert state; more can exceed its memory limit and stop its runs. Adapt it before using it for a larger fleet.

For loss of reports, enable the quiet alert on the device page. It is off by default and sends another notice when reports resume.

## Add commands

Commands are optional. Add this entry to the existing `packages:` → `warblet:` → `files:` list, beside `esphome/warblet.yaml`. Replace both occurrences of `<your-board-id>` with the same ID used by the reporting package:

```yaml
      - path: esphome/warblet-commands.yaml
        vars:
          id: <your-board-id>
          code: !secret warblet_<your-board-id>
```

The package checks for one command every 15 seconds while reports are being accepted. It places the text in the `Warblet command` sensor; your YAML decides what to do with it.

For example, if your YAML already defines a switch with `id: led`, add the following at the top level. If a `text_sensor:` section exists, append the list item to it:

```yaml
text_sensor:
  - id: !extend warblet_command
    on_value:
      - if:
          condition:
            lambda: return x == "led:on";
          then:
            - switch.turn_on: led
      - if:
          condition:
            lambda: return x == "led:off";
          then:
            - switch.turn_off: led
```

Install the updated firmware. In Warblet, open the device's spec and add buttons, a toggle or a text box that sends the matching text. The default spec has no controls; `led:on` and `led:off` only do something after you add their actions.

A command's delivery status does not confirm the action. Check the LED, or report the resulting state with a sensor. Commands up to 8,192 bytes are used only when the whole response arrives; a partial command is discarded.

## Build and adapt

[xiao-esp32s3.yaml](xiao-esp32s3.yaml) is a complete example for a plain Seeed XIAO ESP32-S3. It reports chip temperature and uses the orange user LED on GPIO21, which is active low. Fill in its device ID and secrets, and use the release tag supplied by setup.

Keep your existing board and framework settings when adding the package to another ESP32 project. ESP32-C6 uses ESP-IDF. This package requires `wifi:` and certificate verification; it does not support ESP8266, RP2040 or Ethernet-only configurations.

Requests hold ESPHome's main loop while they run, which can delay other automations. A slow request can produce an `interval took a long time` warning; a stalled request can cause a watchdog restart. Do not use this package where uninterrupted loop timing is required.

## Keep the key private

Anyone with the token can send readings as this device and collect its commands. Keep `secrets.yaml`, built firmware and ESPHome's `.esphome` data folder private; generated files contain the resolved secrets. Check `esphome config` output and error messages before sharing them.

Leave certificate checking on: never add `verify_ssl: false`. Keep `esp32:` → `framework:` → `log_level` below `DEBUG`; ESP-IDF's detailed request logs can expose the token. The package uses Token over HTTPS and does not support Signed authentication.

After rotating a token in Warblet, replace the value in `secrets.yaml` and install again. If you delete the device, prepare a new setup in Warblet and replace its setup snippets before reinstalling.

## Troubleshooting

| ESPHome log or symptom | What to do |
|---|---|
| `Warblet does not know this device` | Prepare new setup, update the ID and secret from its snippets, and install again. The package stops until a restart after an unrecognized setup. |
| `Warblet refused the code` | Check the secret; if you rotated the token, install with the new one. |
| `Warblet not reached` | Check Wi-Fi and internet access. Keep ESPHome current so its trusted certificates stay current; do not disable verification. |
| `report not stored: HTTP 429` | Check account storage and usage limits. The next report is attempted a minute later. |
| Reports stop with a custom `ca_certificate_path` | Your override replaces the usual trusted roots. Remove it unless its certificate authority also validates Warblet. |
| Restarts every 15 minutes with `api:` enabled | If no Home Assistant connects to this device, set `reboot_timeout: 0s` under `api:`. |

After a failed command check, the package waits for another accepted report before checking again. Reports missed while offline are not saved for later upload.

## Make it real

Name the readings that matter, set an email threshold, and connect your own actions to commands from Warblet.

Keep application changes in your device YAML so the shared packages can stay unchanged. Update the routine and dashboard controls in Warblet to match the names and command text you choose.
