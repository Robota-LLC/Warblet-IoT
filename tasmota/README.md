# Tasmota

## What happens

Your Tasmota plug sends its power readings to [Warblet](https://warbletiot.com) once a minute over plain HTTP. Warblet keeps them for up to seven days, shows them on your dashboard, and emails you when the power passes a limit you choose. It is one way: Warblet sends nothing back to the plug, so there is no switch on the dashboard.

You paste one line into Tasmota's console. It adds one rule and sets Tasmota's report period to one minute. Your MQTT settings are not touched, so your own MQTT server and Home Assistant keep hearing the plug as before, and Tasmota keeps its own web page, timers and button.

## What you need

- A plug with a power meter, running Tasmota 16.0.0 or newer.
- Rule set 3 free: type `Rule3` in Tasmota's console, and it should show `"Rules":""`.
- Wi-Fi with internet access.
- A Warblet account, and an alert email address.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash) and choose **Tasmota**.
2. In Tasmota's console (Tools, then Console), type `Rule3`. It must show `"Rules":""`, because the line puts its own rule there. If a rule of yours is there, move it to an empty rule set first.
3. Paste the line the page shows into the console and press Enter. The first report arrives a few seconds later.

## What it costs

The connection is not encrypted. The plug posts over plain HTTP, because Tasmota's builds for this plug's chip cannot make HTTPS requests. Anyone who can watch the traffic between your plug and Warblet, on your Wi-Fi or further along the way, can read its readings and its setup code, and with the code they can post readings as your plug. Nothing on the connection can command the plug: it never acts on anything Warblet sends back. Leave **Encrypted** off for this device, because the plug cannot use it.

## Keep the code private

The setup code is your plug's key to Warblet. It is written in the rule, and Tasmota's console shows it each minute when the rule runs. Set a web password (`WebPassword`) so nobody else on your network can open the plug's page.

## What it sends

Once a minute, one report: Power (W), Voltage (V), Current (A) and Energy today (kWh), from the plug's power meter. Nothing else leaves the plug for Warblet: not your Wi-Fi network's name, and not the plug's network name or addresses. [sample-messages.txt](sample-messages.txt) shows some reports.

While it builds the report, the rule keeps three readings in `Var14`, `Var15` and `Var16`. The one-minute report period also applies to the reports Tasmota sends to your own MQTT server.

## Email

The routine **Tasmota: email when the power passes a limit** is added to your account, switched on. It emails once when the power goes over 1000 W, and again only after it has come back to 900 W and goes over again, or goes over again after a week without that reading.

## Go back

Paste this into Tasmota's console, then delete the device in Warblet. It empties rule set 3 and sets the report period back to Tasmota's five minutes (set your own again if you had another):

```
Backlog Rule3 0; Rule3 ""; TelePeriod 1
```

## Make it real

Change the reading and the limit in [power-limit.star](power-limit.star): with `LIMIT = 5` and `BELOW = True` it tells you when a washer has finished.
