/*
  ESPixelStick.ino

  Project: ESPixelStick - An ESP8266 and E1.31 based pixel driver
  Copyright (c) 2016 Shelby Merrick
  http://www.forkineye.com

   This program is provided free for you to use in any way that you wish,
   subject to the laws and regulations where you are using it.  Due diligence
   is strongly suggested before using this code.  Please give credit where due.

   The Author makes no warranty of any kind, express or implied, with regard
   to this program or the documentation contained in this document.  The
   Author shall not be liable in any event for incidental or consequential
   damages in connection with, or arising out of, the furnishing, performance
   or use of these programs.

*/



#include "ArduinoJson.h"
#include <Hash.h>
#include <SPI.h>
#include "Framework.h"
#include "EFUpdate.h"

#include <Ticker.h>
#include <ESP8266mDNS.h>



#define HTTP_PORT       80      /* Default web server port */
#define CLIENT_TIMEOUT  15      /* In station/client mode try to connection for 15 seconds */
#define AP_TIMEOUT      60      /* In AP mode, wait 60 seconds for a connection or reboot */
#define REBOOT_DELAY    100     /* Delay for rebooting once reboot flag is set */
#define LOG_PORT        Serial  /* Serial port for console logging */


// Configuration file params
#define CONFIG_MAX_SIZE 4096    /* Sanity limit for config file */


const char VERSION[] = "3.2";
const char BUILD_DATE[] = __DATE__;

// Debugging support
#if defined(DEBUG)
extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

static void _u0_putc(char c) {
  while (((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}
#endif

/////////////////////////////////////////////////////////
//
//  Globals
//
/////////////////////////////////////////////////////////

// Configuration file
const char CONFIG_FILE[] = "/config.json";


config_t            config;         // Current configuration
bool                reboot = false; // Reboot flag
AsyncWebServer      web(HTTP_PORT); // Web Server
AsyncWebSocket      ws("/ws");      // Web Socket Plugin
WiFiEventHandler    wifiConnectHandler;     // WiFi connect handler
WiFiEventHandler    wifiDisconnectHandler;  // WiFi disconnect handler
Ticker              wifiTicker;     // Ticker to handle WiFi


connection_status_t connectionStatus;
bool                updateDisplay = true;
long                lastDisplayUpdate = 0;

// Firmware update.
EFUpdate efupdate;
uint8_t * WSframetemp;
uint8_t * confuploadtemp;



/////////////////////////////////////////////////////////
//
//  Forward Declarations
//
/////////////////////////////////////////////////////////

void loadConfig();
void initWifi();
void initWeb();
void updateConfig();

void serializeConfig(String &jsonString, bool pretty = false, bool creds = false);
void dsNetworkConfig(const JsonObject &json);
void dsDeviceConfig(const JsonObject &json);
void saveConfig();

void connectWifi();
void onWifiConnect(const WiFiEventStationModeGotIP &event);
void onWiFiDisconnect(const WiFiEventStationModeDisconnected &event);
void idleTimeout();

void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void * arg, uint8_t *data, size_t len);
void handle_fw_upload(AsyncWebServerRequest *request, String filename,
                      size_t index, uint8_t *data, size_t len, bool final);
void handle_config_upload(AsyncWebServerRequest *request, String filename,
                          size_t index, uint8_t *data, size_t len, bool final);
void displayStatus();


// Radio config
RF_PRE_INIT() {
  //wifi_set_phy_mode(PHY_MODE_11G);    // Force 802.11g mode
  system_phy_set_powerup_option(31);  // Do full RF calibration on power-up
  system_phy_set_max_tpw(82);         // Set max TX power
}

AsyncWebServer * framework_setup(bool forceAccessPoint) {
  // Configure SDK params
  wifi_set_sleep_type(NONE_SLEEP_T);

  // Initial pin states
  //pinMode(DATA_PIN, OUTPUT);
  //digitalWrite(DATA_PIN, LOW);

  // Setup serial log port
  LOG_PORT.begin(115200);
  delay(10);

#if defined(DEBUG)
  ets_install_putc1((void *) &_u0_putc);
  system_set_os_print(1);
#endif

  LOG_PORT.println("");
  LOG_PORT.print(F("ESP v"));
  for (uint8_t i = 0; i < strlen_P(VERSION); i++)
    LOG_PORT.print((char)(pgm_read_byte(VERSION + i)));
  LOG_PORT.print(F(" ("));
  for (uint8_t i = 0; i < strlen_P(BUILD_DATE); i++)
    LOG_PORT.print((char)(pgm_read_byte(BUILD_DATE + i)));
  LOG_PORT.println(")");
  LOG_PORT.println(ESP.getFullVersion());

  // Enable SPIFFS
  if (!SPIFFS.begin())
  {
    LOG_PORT.println("File system did not initialise correctly");
  }
  else
  {
    LOG_PORT.println("File system initialised");
  }

  FSInfo fs_info;
  if (SPIFFS.info(fs_info))
  {
    LOG_PORT.print("Total bytes in file system: ");
    LOG_PORT.println(fs_info.usedBytes);

    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      LOG_PORT.print(dir.fileName());
      File f = dir.openFile("r");
      LOG_PORT.println(f.size());
    }
  }
  else
  {
    LOG_PORT.println("Failed to read file system details");
  }

  // Load configuration from SPIFFS and set Hostname
  loadConfig();
  if (config.hostname) {
    LOG_PORT.print("Setting hostname: ");
    LOG_PORT.println(config.hostname);
    WiFi.hostname(config.hostname);
  }

  connectionStatus.status = CONNSTAT_NONE;

  // Setup WiFi Handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);

  if (!forceAccessPoint) {
    initWifi();
  }

  // If we fail, go SoftAP or reboot
  if (WiFi.status() != WL_CONNECTED) {
    if (forceAccessPoint || config.ap_fallback) {
      LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, GOING SOFTAP ***"));
      WiFi.mode(WIFI_AP);
      connectionStatus.ssid = config.hostname;
      WiFi.softAP(connectionStatus.ssid.c_str());
      connectionStatus.ourLocalIP = WiFi.softAPIP();
      connectionStatus.ourSubnetMask = IPAddress(255, 255, 255, 0);
      connectionStatus.status = CONNSTAT_LOCALAP;
      updateDisplay = true;
    } else {
      LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, REBOOTING ***"));
      ESP.restart();
    }
  }

  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWiFiDisconnect);

  LOG_PORT.print("IP : ");
  LOG_PORT.println(connectionStatus.ourLocalIP);
  LOG_PORT.print("Subnet mask : ");
  LOG_PORT.println(connectionStatus.ourSubnetMask);

  // Configure and start the web server
  initWeb();

  return &web;
}

/////////////////////////////////////////////////////////
//
//  WiFi Section
//
/////////////////////////////////////////////////////////

void initWifi() {
  // Switch to station mode and disconnect just in case
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (config.ssid.length() == 0)
    return;
    
  connectWifi();
  uint32_t timeout = millis();
  while (WiFi.status() != WL_CONNECTED) {
    LOG_PORT.print(".");
    delay(500);
    if (millis() - timeout > (1000 * config.sta_timeout) ) {
      LOG_PORT.println("");
      LOG_PORT.println(F("*** Failed to connect ***"));
      connectionStatus.status = CONNSTAT_NONE;
      updateDisplay = true;
      displayStatus();
      break;
    }
  }
}

void connectWifi() {
  delay(secureRandom(100, 500));

  LOG_PORT.println("");
  LOG_PORT.print(F("Connecting to "));
  LOG_PORT.print(config.ssid);
  LOG_PORT.print(F(" as "));
  LOG_PORT.println(config.hostname);
  connectionStatus.status = CONNSTAT_CONNECTING;
  connectionStatus.ssid = config.ssid;
  updateDisplay = true;
  displayStatus();

  WiFi.begin(config.ssid.c_str(), config.passphrase.c_str());
  if (config.dhcp) {
    LOG_PORT.print(F("Connecting with DHCP"));
  } else {
    // We don't use DNS, so just set it to our gateway
    WiFi.config(IPAddress(config.ip[0], config.ip[1], config.ip[2], config.ip[3]),
                IPAddress(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3]),
                IPAddress(config.netmask[0], config.netmask[1], config.netmask[2], config.netmask[3]),
                IPAddress(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3])
               );
    LOG_PORT.print(F("Connecting with Static IP"));
  }
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
  LOG_PORT.println("");
  LOG_PORT.print(F("Connected with IP: "));
  LOG_PORT.println(WiFi.localIP());

  connectionStatus.ourLocalIP = WiFi.localIP();
  connectionStatus.ourSubnetMask = WiFi.subnetMask();
  connectionStatus.status = CONNSTAT_CONNECTED;
  updateDisplay = true;

  // Setup mDNS / DNS-SD
  //TODO: Reboot or restart mdns when config.id is changed?
  String chipId = String(ESP.getChipId(), HEX);
  MDNS.setInstanceName(String(config.id + " (" + chipId + ")").c_str());
  if (MDNS.begin(config.hostname.c_str())) {
    MDNS.addService("http", "tcp", HTTP_PORT);
  } else {
    LOG_PORT.println(F("*** Error setting up mDNS responder ***"));
  }
}

void onWiFiDisconnect(const WiFiEventStationModeDisconnected &event) {
  LOG_PORT.println(F("*** WiFi Disconnected ***"));

  connectionStatus.status = CONNSTAT_NONE;
  updateDisplay = true;

  wifiTicker.once(2, connectWifi);
}



/////////////////////////////////////////////////////////
//
//  Web Section
//
/////////////////////////////////////////////////////////

// Configure and start the web server
void initWeb() {
  // Handle OTA update from asynchronous callbacks
  Update.runAsync(true);

  // Setup WebSockets
  ws.onEvent(wsEvent);
  web.addHandler(&ws);

  // Heap status handler
  web.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  // JSON Config Handler
  web.on("/conf", HTTP_GET, [](AsyncWebServerRequest * request) {
    String jsonString;
    serializeConfig(jsonString, true);
    request->send(200, "text/json", jsonString);
  });

  // Firmware upload handler - only in station mode
  web.on("/updatefw", HTTP_POST, [](AsyncWebServerRequest * request) {
    ws.textAll("X6");
  }, handle_fw_upload).setFilter(ON_STA_FILTER);

  // Static Handler
  web.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

  // Raw config file Handler - but only on station
  //  web.serveStatic("/config.json", SPIFFS, "/config.json").setFilter(ON_STA_FILTER);

  web.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404, "text/plain", "Page not found");
  });

  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), "*");

  // Config file upload handler - only in station mode
  web.on("/config", HTTP_POST, [](AsyncWebServerRequest * request) {
    ws.textAll("X6");
  }, handle_config_upload).setFilter(ON_STA_FILTER);

  web.begin();

  LOG_PORT.print(F("- Web Server started on port "));
  LOG_PORT.println(HTTP_PORT);
}

/////////////////////////////////////////////////////////
//
//  JSON / Configuration Section
//
/////////////////////////////////////////////////////////

// Configuration Validations
void validateConfig() {

}

void updateConfig() {
  // Validate first
  validateConfig();
}

// De-Serialize Network config
void dsNetworkConfig(const JsonObject &json) {
  if (json.containsKey("network")) {
    JsonObject networkJson = json["network"];

    config.ssid = networkJson["ssid"].as<String>();
    config.passphrase = networkJson["passphrase"].as<String>();
    if (!config.passphrase.length())

      // Network
      for (int i = 0; i < 4; i++) {
        config.ip[i] = networkJson["ip"][i];
        config.netmask[i] = networkJson["netmask"][i];
        config.gateway[i] = networkJson["gateway"][i];
      }
    config.dhcp = networkJson["dhcp"];
    config.sta_timeout = networkJson["sta_timeout"] | CLIENT_TIMEOUT;
    if (config.sta_timeout < 5) {
      config.sta_timeout = 5;
    }

    config.ap_fallback = networkJson["ap_fallback"];
    config.ap_timeout = networkJson["ap_timeout"] | AP_TIMEOUT;
    if (config.ap_timeout < 15) {
      config.ap_timeout = 15;
    }

    // Generate default hostname if needed
    config.hostname = networkJson["hostname"].as<String>();
  }
  else {
    LOG_PORT.println("No network settings found.");
  }

  if (!config.hostname.length()) {
    config.hostname = "esp-" + String(ESP.getChipId(), HEX);
  }
}


// De-serialize Device Config
void dsDeviceConfig(const JsonObject &json) {
  // Device
  if (json.containsKey("device")) {
    config.id = json["device"]["id"].as<String>();
  }
  else
  {
    LOG_PORT.println("No device settings found.");
  }
}

// Load configugration JSON file
void loadConfig() {
  // Zeroize Config struct
  memset(&config, 0, sizeof(config));

  // Load CONFIG_FILE json. Create and init with defaults if not found
  File file = SPIFFS.open(CONFIG_FILE, "r");
  if (!file) {
    LOG_PORT.println(F("- No configuration file found."));
    config.ssid = "";
    config.passphrase = "";
    config.hostname = "esps-" + String(ESP.getChipId(), HEX);
    config.ap_fallback = true;
    config.id = "No Config Found";
    saveConfig();
  } else {
    // Parse CONFIG_FILE json
    size_t size = file.size();
    if (size > CONFIG_MAX_SIZE) {
      LOG_PORT.println(F("*** Configuration File too large ***"));
      return;
    }

    std::unique_ptr<char[]> buf(new char[size]);
    file.readBytes(buf.get(), size);

    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, buf.get());
    if (error) {
      LOG_PORT.println(F("*** Configuration File Format Error ***"));
      return;
    }

    dsNetworkConfig(json.as<JsonObject>());
    dsDeviceConfig(json.as<JsonObject>());


    LOG_PORT.println(F("- Configuration loaded."));
  }

  // Validate it
  validateConfig();
}

// Serialize the current config into a JSON string
void serializeConfig(String &jsonString, bool pretty, bool creds) {
  // Create buffer and root object
  DynamicJsonDocument json(1024);

  // Device
  JsonObject device = json.createNestedObject("device");
  device["id"] = config.id.c_str();

  // Network
  JsonObject network = json.createNestedObject("network");
  network["ssid"] = config.ssid.c_str();
  if (creds)
    network["passphrase"] = config.passphrase.c_str();
  network["hostname"] = config.hostname.c_str();
  JsonArray ip = network.createNestedArray("ip");
  JsonArray netmask = network.createNestedArray("netmask");
  JsonArray gateway = network.createNestedArray("gateway");
  for (int i = 0; i < 4; i++) {
    ip.add(config.ip[i]);
    netmask.add(config.netmask[i]);
    gateway.add(config.gateway[i]);
  }
  network["dhcp"] = config.dhcp;
  network["sta_timeout"] = config.sta_timeout;

  network["ap_fallback"] = config.ap_fallback;
  network["ap_timeout"] = config.ap_timeout;

  if (pretty)
    serializeJsonPretty(json, jsonString);
  else
    serializeJson(json, jsonString);
}


// Save configuration JSON file
void saveConfig() {
  // Update Config
  updateConfig();

  // Serialize Config
  String jsonString;
  serializeConfig(jsonString, true, true);

  // Save Config
  File file = SPIFFS.open(CONFIG_FILE, "w");
  if (!file) {
    LOG_PORT.println(F("*** Error creating configuration file ***"));
    return;
  } else {
    file.println(jsonString);
    LOG_PORT.println(F("* Configuration saved."));
  }
}


void displayStatus()
{
    if (connectionStatus.status == CONNSTAT_CONNECTED) {
      int rssi = WiFi.RSSI();
      connectionStatus.signalStrength = 2 * (rssi + 100);
      if (rssi <= -100)
        connectionStatus.signalStrength = 0;
      else if (rssi >= -50)
        connectionStatus.signalStrength = 100;
    }
    else {
      connectionStatus.signalStrength = 0;
    }

    updateStatus(connectionStatus);
}

/////////////////////////////////////////////////////////
// Web socket handlers
/////////////////////////////////////////////////////////
/*
  Packet Commands
    G1 - Get Config
    G2 - Get Config Status

    S1 - Set Network Config
    S2 - Set Device Config

    XJ - Get RSSI,heap,uptime, e131 stats in json

    X6 - Reboot
*/


// Handle request that start with 'X'
void procX(uint8_t *data, AsyncWebSocketClient *client) {
  switch (data[1]) {
    case 'J': {

        DynamicJsonDocument json(1024);

        // system statistics
        JsonObject system = json.createNestedObject("system");
        system["rssi"] = (String)WiFi.RSSI();
        system["freeheap"] = (String)ESP.getFreeHeap();
        system["uptime"] = (String)millis();

        String response;
        serializeJson(json, response);
        client->text("XJ" + response);
        break;
      }

    case '6':  // Init 6 baby, reboot!
      reboot = true;
  }
}

// Handle requests that start with 'G'
void procG(uint8_t *data, AsyncWebSocketClient *client) {
  switch (data[1]) {
    case '1': {
        String response;
        serializeConfig(response, false, true);
        client->text("G1" + response);
        break;
      }

    case '2': {
        // Create buffer and root object
        DynamicJsonDocument json(1024);

        json["ssid"] = (String)WiFi.SSID();
        json["hostname"] = (String)WiFi.hostname();
        json["ip"] = WiFi.localIP().toString();
        json["mac"] = WiFi.macAddress();
        json["version"] = (String)VERSION;
        json["built"] = (String)BUILD_DATE;
        json["flashchipid"] = String(ESP.getFlashChipId(), HEX);
        json["usedflashsize"] = (String)ESP.getFlashChipSize();
        json["realflashsize"] = (String)ESP.getFlashChipRealSize();
        json["freeheap"] = (String)ESP.getFreeHeap();

        String response;
        serializeJson(json, response);
        client->text("G2" + response);
        break;
      }

  }
}

// Handle requests that start with 'S'
void procS(uint8_t *data, AsyncWebSocketClient *client) {

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, reinterpret_cast<char*>(data + 2));

  if (error) {
    LOG_PORT.println(F("*** procS(): Parse Error ***"));
    LOG_PORT.println(reinterpret_cast<char*>(data));
    return;
  }

  bool reboot = false;
  switch (data[1]) {
    case '1':   // Set Network Config
      dsNetworkConfig(json.as<JsonObject>());
      saveConfig();
      client->text("S1");
      break;
    case '2':   // Set Device Config

      dsDeviceConfig(json.as<JsonObject>());
      saveConfig();

      if (reboot)
        client->text("S1");
      else
        client->text("S2");
      break;
  }
}


void handle_fw_upload(AsyncWebServerRequest *request, String filename,
                      size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    WiFiUDP::stopAll();
    LOG_PORT.print(F("* Upload Started: "));
    LOG_PORT.println(filename.c_str());
    efupdate.begin();
  }

  if (!efupdate.process(data, len)) {
    LOG_PORT.print(F("*** UPDATE ERROR: "));
    LOG_PORT.println(String(efupdate.getError()));
  }

  if (efupdate.hasError())
    request->send(200, "text/plain", "Update Error: " +
                  String(efupdate.getError()));

  if (final) {
    LOG_PORT.println(F("* Upload Finished."));
    efupdate.end();
    SPIFFS.begin();
    saveConfig();
    reboot = true;
  }
}

void handle_config_upload(AsyncWebServerRequest *request, String filename,
                          size_t index, uint8_t *data, size_t len, bool final) {
  static File file;
  if (!index) {
    WiFiUDP::stopAll();
    LOG_PORT.print(F("* Config Upload Started: "));
    LOG_PORT.println(filename.c_str());

    if (confuploadtemp) {
      free (confuploadtemp);
      confuploadtemp = nullptr;
    }
    confuploadtemp = (uint8_t*) malloc(CONFIG_MAX_SIZE);
  }

  LOG_PORT.printf("index %d len %d\n", index, len);
  memcpy(confuploadtemp + index, data, len);
  confuploadtemp[index + len] = 0;

  if (final) {
    int filesize = index + len;
    LOG_PORT.print(F("* Config Upload Finished:"));
    LOG_PORT.printf(" %d bytes", filesize);

    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, reinterpret_cast<char*>(confuploadtemp));

    if (error) {
      LOG_PORT.println(F("*** Parse Error ***"));
      LOG_PORT.println(reinterpret_cast<char*>(confuploadtemp));
      request->send(500, "text/plain", "Config Update Error." );
    } else {
      dsNetworkConfig(json.as<JsonObject>());
      dsDeviceConfig(json.as<JsonObject>());
      saveConfig();
      request->send(200, "text/plain", "Config Update Finished: " );
      //          reboot = true;
    }

    if (confuploadtemp) {
      free (confuploadtemp);
      confuploadtemp = nullptr;
    }
  }
}

void wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void * arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_DATA: {
        AwsFrameInfo *info = static_cast<AwsFrameInfo*>(arg);
        if (info->opcode == WS_TEXT) {
          switch (data[0]) {
            case 'X':
              procX(data, client);
              break;
            case 'G':
              procG(data, client);
              break;
            case 'S':
              procS(data, client);
              break;
          }
        } else {
          LOG_PORT.println(F("-- binary message --"));
        }
        break;
      }
    case WS_EVT_CONNECT:
      LOG_PORT.print(F("* WS Connect - "));
      LOG_PORT.println(client->id());
      break;
    case WS_EVT_DISCONNECT:
      LOG_PORT.print(F("* WS Disconnect - "));
      LOG_PORT.println(client->id());
      break;
    case WS_EVT_PONG:
      LOG_PORT.println(F("* WS PONG *"));
      break;
    case WS_EVT_ERROR:
      LOG_PORT.println(F("** WS ERROR **"));
      break;
  }
}



/////////////////////////////////////////////////////////
//
//  Main Loop
//
/////////////////////////////////////////////////////////
void framework_loop() {
  // Reboot handler
  if (reboot) {
    delay(REBOOT_DELAY);
    ESP.restart();
  }

  if (updateDisplay || millis() > lastDisplayUpdate + 2000) {
    displayStatus();
    updateDisplay = false;
    lastDisplayUpdate = millis();
  }

  // workaround crash - consume incoming bytes on serial port
  if (LOG_PORT.available()) {
    while (LOG_PORT.read() >= 0);
  }
}
