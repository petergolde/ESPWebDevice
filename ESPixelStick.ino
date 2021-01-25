#include "Framework.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Display
#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 32   // OLED display height, in pixels 
#define OLED_RESET   -1    // define SSD1306 OLED (-1 means none)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int forceAccessPointPin = D8;   // Connect to ground to force access point.

#define RELAY_PIN D6
#define PIR_TRIGGER_PIN D4
// State.
String deviceName = "Default";
int    millisOn = 2000;
int    millisOff = 2000;
bool   blinking = true;

// Was AP request at start.
bool   startupRequestAP = false;

void led_on_request(AsyncWebServerRequest * request)
{
  digitalWrite(RELAY_PIN, LOW);
  blinking = false;
  request->send(200, "text/plain", "Relay is ON!");
}

void led_off_request(AsyncWebServerRequest * request)
{
  digitalWrite(RELAY_PIN, HIGH);
  blinking = false;
  request->send(200, "text/plain", "Relay is OFF!");
}

void led_blink_request(AsyncWebServerRequest * request)
{
  blinking = true;
  request->send(200, "text/plain", "Relay is blinking!");
}

void setup() {
  pinMode(forceAccessPointPin, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(PIR_TRIGGER_PIN, INPUT_PULLUP);

  // Initialise OLED display.
  Wire.begin(4, 0);           // set I2C pins [SDA = GPIO4 (D2), SCL = GPIO0 (D3)], default clock is 100kHz
  Wire.setClock(400000L);     // set I2C clock to 400kHz
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)

  // Show initial message on the screen
  display.clearDisplay();
  display.setTextSize(2);      // Big Text
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font  display.display();
  display.println("Starting");
  display.display();

  // Check pin to force access point.
  startupRequestAP = (digitalRead(forceAccessPointPin) == LOW);
  AsyncWebServer * webServer = framework_setup(startupRequestAP);

  // Set up request handlers on the web interface.
  // See https://github.com/me-no-dev/ESPAsyncWebServer
  if (webServer) {
    webServer->on("/on", HTTP_GET, led_on_request);
    webServer->on("/off", HTTP_GET, led_off_request);
    webServer->on("/blink", HTTP_GET, led_blink_request);
  }
}

// Update the status on the OLED display.
// Called from the framework whenever the network status updates
// or every 2 seconds. Always called in a place it is safe.
void updateStatus(const connection_status_t & connectionStatus)
{
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  const char * statusText;
  switch (connectionStatus.status) {
    case CONNSTAT_NONE:
    default:
      statusText = "Disconnected"; break;
    case CONNSTAT_CONNECTING:
      statusText = "Connecting"; break;
    case CONNSTAT_CONNECTED:
      statusText = "Connected"; break;
    case CONNSTAT_LOCALAP:
      statusText = "Local AP"; break;
  }
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Draw 'inverse' text
  display.print(deviceName);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); // Draw regular text
  display.print(" ");
  display.println(statusText);

  if (connectionStatus.status != CONNSTAT_NONE) {
    display.print("SSID: ");
    display.println(connectionStatus.ssid);
  }

  if (connectionStatus.status == CONNSTAT_CONNECTED || connectionStatus.status == CONNSTAT_LOCALAP) {
    display.print("IP: ");
    display.println(connectionStatus.ourLocalIP);
  }

  if (connectionStatus.status == CONNSTAT_CONNECTED) {
    display.setCursor(114, 8);
    display.println(connectionStatus.signalStrength);
  }

  display.display();
}

void saveState(const JsonObject & json)
{
  JsonObject device = json.createNestedObject("device");
  device["id"] = deviceName.c_str();
  device["millisOn"] = millisOn;
  device["millisOff"] = millisOff;
}

void loadState(const JsonObject & json)
{
  if (json.containsKey("device")) {
    deviceName = json["device"]["id"].as<String>();
    millisOn = json["device"]["millisOn"].as<int>();
    millisOff = json["device"]["millisOff"].as<int>();
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  framework_loop();

  if (digitalRead(PIR_TRIGGER_PIN) == LOW) {
    digitalWrite(RELAY_PIN, LOW);
    delay(millisOn);
    digitalWrite(RELAY_PIN, HIGH);
  }
  
  // If the AP switch is closed, but wasn't closed at startup, restart to
  // enter AP mode.
  if (!startupRequestAP && digitalRead(forceAccessPointPin) == LOW) {
    ESP.restart();
  }
/*
  // If the AP switch is open, but was closed at startup, restart to exit
  // AP mode
  if (startupRequestAP && digitalRead(forceAccessPointPin) == HIGH) {
    ESP.restart();
  }*/
}
