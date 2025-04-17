#include "WifiCam.hpp"
#include <WiFi.h>
 
static const char* WIFI_SSID = "LAWLITE";
static const char* WIFI_PASS = "11111112";

esp32cam::Resolution initialResolution;

WebServer server(80);
#define INPUT_PIN 14  // Use a valid GPIO
void
setup() {
  pinMode(INPUT_PIN, INPUT); // Or INPUT_PULLUP if needed
  Serial.begin(115200);
  Serial.println();
  esp32cam::setLogger(Serial);
  delay(1000);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("WiFi failure %d\n", WiFi.status());
    delay(5000);
    ESP.restart();
  }
  Serial.println("WiFi connected");
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);  // turn the LED on (HIGH is the voltage level)
  delay(10);                      // wait for a second
  digitalWrite(4, LOW);
  delay(10);
  digitalWrite(4, HIGH);  // turn the LED on (HIGH is the voltage level)
  delay(10);                      // wait for a second
  digitalWrite(4, LOW);
  {
    using namespace esp32cam;

    initialResolution = Resolution::find(1024, 768);

    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(initialResolution);
    cfg.setJpeg(80);

    bool ok = Camera.begin(cfg);
    if (!ok) {
      Serial.println("camera initialize failure");
      delay(5000);
      ESP.restart();
    }
    Serial.println("camera initialize success");
  }

  Serial.println("camera starting");
  Serial.print("http://");
  Serial.println(WiFi.localIP());

  addRequestHandlers();
  server.begin();
}

void
loop() {
  
  server.handleClient();
}
