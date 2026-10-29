# Tasmota

## What happens

Your Tasmota plug sends its power to [Warblet](https://warbletiot.com) once a minute. Warblet shows the readings and their history, and emails you when the power passes a limit. This demo is one way: there is no switch on the dashboard.

Paste one line into Tasmota's console; there is nothing to flash. The setup leaves your MQTT configuration unchanged and sets `TelePeriod` to 60: both Warblet posts and MQTT sensor telemetry run once a minute. Tasmota keeps its web page, timers and button.

## What you need

- A plug with a power meter, such as a Sonoff S31, running Tasmota 16.0.0 or newer.
- `Rule3` free for this demo.
- Wi-Fi with internet access and a Warblet account.
- An alert email address in Warblet to receive the emails.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash) and choose **Tasmota**.
2. Open Tasmota's **Tools → Console** and type `Rule3`. It must show `"Rules":""`; move an existing rule to an empty rule set before continuing.
3. Paste the page's line unchanged and press Enter. It begins with `Backlog3`, which keeps the command replies off your MQTT server and out of the log.
4. Check reports on the device page rather than by typing `Rule3`: its answer reveals the code and sends it to your MQTT server. Reports should start within a few seconds, then arrive once a minute.

## What it costs

The connection uses plain HTTP and is not encrypted. Anyone watching traffic between your plug and Warblet can read the readings and setup code, then use the code to post readings as your plug. Nothing on the connection can command the plug.

Leave **Encrypted** off for this device; Warblet refuses these HTTP posts when it is on. While Warblet cannot be reached, each minute's post can hold the plug up for several seconds. A short button press may be missed, and other work waits.

## Keep the code private

The setup code becomes the plug's token. It appears in every HTTP post, in the stored rule and configuration backups, and in Tasmota's logs when you paste the line and each minute the rule runs. `Backlog3` hides command replies, not these log entries.

Keep console output and backups private, including any syslog destination or MQTT logging you enable. Set a web password with `WebPassword <password>` to restrict access to the plug's page. If you rotate the token in Warblet, paste the setup line again with the new token replacing the code.

## What it sends

One reading: **Power**, in watts. The report contains no Wi-Fi name, plug network name or network addresses. A free account keeps readings for up to seven days.

For Voltage, Current and Energy today, see the longer line and its tradeoffs in [decoder.star](decoder.star).

## Email

Setup adds **Tasmota: email when the power passes a limit**, switched on. It emails once above 1000 W, then waits until power returns to 900 W or lower before emailing on another rise. If a reading returns above the limit after a week without it, it can email again.

The routine emails at most once an hour per device; quiet and doorbell alerts share the account’s ten hourly alert emails.

## Go back

Paste this into Tasmota's console, then delete the device in Warblet:

```text
Backlog3 Rule3 0; Rule3 ""; TelePeriod 1
```

This disables and clears the demo's rule and restores Tasmota's five-minute report period. Set your own period again if you used a different one.

## Troubleshooting

| Problem | Check |
|---|---|
| No reports after two minutes | Watch the console for a minute. Look for `RUL: TELE-ENERGY#POWER performs`, but keep that line private: it contains your code. |
| No `RUL:` line | Type `TelePeriod`; it should be 60. Check that the plug has a power meter, then paste the setup line again. |
| `RUL:` appears, but no reading | Check internet access, leave **Encrypted** off, and look for refusals on the Warblet device page. |
| `Connect failed` | Check internet access and DNS. |
| `Host not found` | The address in the command is malformed; copy the setup line again. |
| `Done` | Warblet answered, but this does not confirm acceptance. Check the device page. |
| `401` or `unclaimed` | For `401`, check the token. If the setup code expired or was already used, or the device was deleted, get a new setup code and paste the new line. |

## Make it real

In Warblet's **Routines**, edit the installed routine's `FIELD`, `LIMIT`, `BELOW` and `MARGIN`; [power-limit.star](power-limit.star) explains them. For a washer, `LIMIT = 5` and `BELOW = True` email on the first idle reading, then after a run that reached `LIMIT + MARGIN`, subject to the same hourly email limit. Editing the file here does not change the routine in your account.
