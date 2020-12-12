#include "Framework.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Display
#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 32   // OLED display height, in pixels 
#define OLED_RESET   -1    // define SSD1306 OLED (-1 means none)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int forceAccessPointPin = D5;   // Connect to ground to force access point.

void led_on_request(AsyncWebServerRequest * request)
{
  digitalWrite(LED_BUILTIN, LOW);
  request->send(200, "text/plain", "LED is ON!");
}

void led_off_request(AsyncWebServerRequest * request)
{
  digitalWrite(LED_BUILTIN, HIGH);
  request->send(200, "text/plain", "LED is OFF!");
}

void setup() {
  pinMode(forceAccessPointPin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

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
  AsyncWebServer * webServer = framework_setup(digitalRead(forceAccessPointPin) == LOW);

  // Set up request handlers on the web interface. 
  // See https://github.com/me-no-dev/ESPAsyncWebServer
  if (webServer) {
    webServer->on("/ledon", HTTP_GET, led_on_request);
    webServer->on("/ledoff", HTTP_GET, led_off_request);
  }
}

// Update the status on the OLED display.
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
      statusText = "Not connected."; break;
    case CONNSTAT_CONNECTING:
      statusText = "Connecting..."; break;
    case CONNSTAT_CONNECTED:
      statusText = "Connected."; break;
    case CONNSTAT_LOCALAP:
      statusText = "Local WiFi AP."; break;
  }
  display.println(statusText);

  display.print("SSID: ");
  display.println(connectionStatus.ssid);

  if (connectionStatus.status == CONNSTAT_CONNECTED || connectionStatus.status == CONNSTAT_LOCALAP) {
    display.print("  IP: ");
    display.println(connectionStatus.ourLocalIP);
  }

  if (connectionStatus.status == CONNSTAT_CONNECTED) {
    display.print("Strength: ");
    display.println(connectionStatus.signalStrength);
  }

  display.display();
}

void loop() {
  // put your main code here, to run repeatedly:
  framework_loop();
}
