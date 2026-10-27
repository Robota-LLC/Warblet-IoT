# Tasmota

## What happens

Your Tasmota plug sends its power readings to [Warblet](https://warbletiot.com) every minute over MQTT, and a switch on your dashboard sends it `ON` or `OFF`. Warblet keeps the messages for up to seven days and emails you when the power passes a limit you choose.

You paste one line into Tasmota's console. It changes Tasmota's MQTT settings and adds one rule. Nothing is installed from Warblet's page, and Tasmota keeps its own web page, timers and button.

## What you need

- A plug with a power meter, running Tasmota 16.0.0 or newer.
- Rule set 3 free, and a look at rule sets 1 and 2 (step 3 below).
- The board id the setup page suggests. This demo takes ids of at most 32 characters: Tasmota keeps the id in three of its settings, and a longer one may not fit, so the plug never connects.
- Wi-Fi with internet access.
- A Warblet account, and an alert email address.

## Set it up

1. Open [Set up a device](https://warbletiot.com/flash) and choose **Tasmota**. Keep the board id it suggests.
2. Write down your MQTT settings (Configuration, then Configure MQTT), so you can go back to them.
3. In Tasmota's console (Tools, then Console), type `Rule1`, `Rule2` and `Rule3`. `Rule3` must show `"Rules":""`, because the line puts its own rule there. If `Rule1` or `Rule2` hold rules of your own, read [Commands](#commands) before you go on.
4. Paste the line the page shows into the console and press Enter. Tasmota saves the settings and restarts.

## What changes

Tasmota has one MQTT connection. This line points it at Warblet, so the MQTT server your plug talks to now, and anything that listens there, stops hearing the plug.

The connection is not encrypted. It uses port 1883, because Tasmota's builds for this plug's chip have no TLS. Anyone who can watch the traffic between your plug and Warblet, on your Wi-Fi or further along the way, can read its readings, its commands and its setup code. With the code they can send readings as your plug, or take its connection so it misses your commands. By tampering with the connection they can also send it commands (see [Commands](#commands)). Use it for things that can safely be switched by mistake, and leave **Encrypted** off for this device, because the plug cannot use it.

## Keep the code private

The setup code is your plug's key to Warblet. Tasmota's console shows the line you pasted until the plug restarts, and Tasmota keeps the code in its settings. Set a web password (`WebPassword`) so nobody else on your network can open the plug's page.

## What it sends

- Power, Voltage, Current, Power factor, Energy today, Energy yesterday and Energy total, every minute.
- Relay (`ON` or `OFF`), Wi-Fi signal, Uptime, Free memory and MQTT connects, every minute.
- The relay's state alone, after each command and each press of the plug's button.

Tasmota's STATE message also carries your Wi-Fi network's name, your router's address and the plug's own address on your network. Warblet stores each message as it arrives, so those stay in the plug's message history; the decoder never makes them readings. [sample-messages.txt](sample-messages.txt) shows each kind of message.

## Commands

The switch on the dashboard sends `ON` or `OFF`. The rule turns the relay on or off, and Tasmota reports the relay's new state, which the switch then shows. A command's status in Warblet says it was sent, not that the relay moved.

Through Warblet a command switches the relay on or off. If you keep rules of your own in `Rule1` or `Rule2`, a command can also set those off, so on this plug don't write rules that act on `%value%`, and keep only rules that are safe to set off at any time. Tasmota puts a command's text into the event the rule reads without checking it, so a crafted command can look like another event. Commands come from anyone who can send commands on your Warblet account, and from anyone tampering with the unencrypted connection.

## Email

The routine **Tasmota: email when the power passes a limit** is added to your account, switched on. It emails once when the power goes over 1000 W, and again only after it has come back to 900 W and goes over again, or goes over again after a week without that reading.

## Go back

Paste this into Tasmota's console, then put your own MQTT settings back and delete the device in Warblet:

```
Backlog Rule3 0; Rule3 ""; MqttHost 0; MqttPassword 0; MqttUser 1; MqttClient 1; FullTopic 1; Prefix2 1; Prefix3 1; TelePeriod 1
```

## Make it real

Change the reading and the limit in [power-limit.star](power-limit.star): with `LIMIT = 5` and `BELOW = True` it tells you when a washer has finished.
