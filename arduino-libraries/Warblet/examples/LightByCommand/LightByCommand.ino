// Light by command: switch the board's LED from your Warblet dashboard.
//
// Add a toggle to the spec's dashboard with on text "led:on" and off text
// "led:off", and a button with text "led:blink" (the same commands the
// public Warblet demos use). The board asks for commands every 10 seconds.
// Paste decoder.star (in this folder) into the spec.
// Set up the board over USB with the Warblet setup page, as in SensorReading.
//
// The board also reports its LED setting after each command: that says the
// sketch received the command, not that the light came on. Look at the LED.

#include <Warblet.h>

// The board's own LED. Some boards name none (a plain ESP32 module, for
// one); pin 2 is the LED on many ESP32 boards, so it is used there. Change
// it to your LED's pin if yours is elsewhere.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// The level that lights the LED. Most boards light it on HIGH; the XIAO
// ESP32-S3 lights it on LOW. On another board whose LED lights on LOW, set LOW.
#if defined(ARDUINO_XIAO_ESP32S3)
const int LED_ON = LOW;
#else
const int LED_ON = HIGH;
#endif

Warblet board;
bool saidHello = false;
bool ledLit = false;                    // what the sketch set: true is lit

void setLed(bool lit) {
  ledLit = lit;
  digitalWrite(LED_BUILTIN, lit ? LED_ON : !LED_ON);
}

void command(String text) {
  if (text == "led:on") {
    setLed(true);
  } else if (text == "led:off") {
    setLed(false);
  } else if (text == "led:blink") {
    bool was = ledLit;
    for (int i = 0; i < 3; i++) {       // three blinks, then as it was
      setLed(!was);
      board.wait(200);
      setLed(was);
      board.wait(200);
    }
  } else {
    Serial.println("unknown command: " + text);
    return;
  }
  board.sendNumber(ledLit ? 1 : 0, "led");  // 1 when the sketch lit it
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);                        // start with the LED out
  board.begin();
  board.onCommand(command);             // run command() when one arrives
}

void loop() {
  // The first message adds the board to your account, so say hello once.
  if (!saidHello && board.connected()) {
    saidHello = board.sendNumber(0, "led");
  }
  board.wait(100);                      // checks for commands, then calls command()
}
