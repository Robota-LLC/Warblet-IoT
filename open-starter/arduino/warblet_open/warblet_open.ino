// Warblet Open starter for Arduino: one file, no library.
// Every 10 seconds the board posts one line of text, "<count> <what it last
// heard>", like "3 hello". Type in the Message box on its Warblet device page
// and the next line says it back. Open level: no token, no TLS, no clock.
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_UNOR4_WIFI)
#include <WiFiS3.h>
#elif defined(ARDUINO_SAMD_NANO_33_IOT)
#include <WiFiNINA.h>
#else
#include <WiFi.h>                       // ESP32, and others whose library is WiFi.h
#endif

const char WIFI_NAME[] = "your-wifi-name";
const char WIFI_PASSWORD[] = "your-wifi-password";
// Make the id yours: letters, digits and - _ . : (up to 128). Anyone who knows
// it can post as this board, so add something hard to guess.
const char BOARD_ID[] = "<your-board-id>";
const char HOST[] = "http.warbletiot.com";   // Warblet's plain-HTTP door

char buf[1600];                         // one request, or one answer
char heard[1025] = "hello";             // the last thing typed in the Message box
long count = 0;

// One HTTP request. Returns the status (202, 200, 204, 429...) or -1 when the
// network failed; the answer is left in buf. Each wait gives up after 15 s.
int http(const char *method, const char *path, const char *body) {
  WiFiClient client;
  int n = snprintf(buf, sizeof buf, "%s %s HTTP/1.0\r\nHost: %s\r\nContent-Length: %u\r\n\r\n%s",
                   method, path, HOST, (unsigned)strlen(body), body);
  if (n <= 0 || n >= (int)sizeof buf || !client.connect(HOST, 80)) {
    client.stop();
    return -1;
  }
  client.write((const uint8_t *)buf, n);
  int got = 0;
  unsigned long start = millis();
  while ((client.connected() || client.available()) && millis() - start < 15000 &&
         got < (int)sizeof buf - 1) {
    if (client.available()) buf[got++] = client.read();
    else delay(1);                      // let the board's other tasks run
  }
  buf[got] = '\0';
  client.stop();
  const char *space = strchr(buf, ' ');
  return (strncmp(buf, "HTTP/", 5) == 0 && space != NULL) ? atoi(space + 1) : -1;
}

void setup() {
  Serial.begin(115200);
  for (; BOARD_ID[0] == '<'; delay(5000))  // still the placeholder: send nothing
    Serial.println("Set BOARD_ID at the top of the sketch, then upload it again.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {    // at the start, and after every drop
    Serial.println("joining Wi-Fi...");
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  }
  char path[160];
  snprintf(path, sizeof path, "/ingest/%s/down", BOARD_ID);
  if (http("POST", path, "") == 200) {     // someone typed in the Message box
    const char *text = strstr(buf, "\r\n\r\n");
    if (text != NULL) snprintf(heard, sizeof heard, "%s", text + 4);
  }
  char line[1040];
  snprintf(line, sizeof line, "%ld %s", ++count, heard);
  snprintf(path, sizeof path, "/ingest/%s", BOARD_ID);
  int status = http("POST", path, line);
  Serial.print(line);
  Serial.print(" -> ");
  Serial.println(status);                 // 202: Warblet has it
  delay(10000);
}
