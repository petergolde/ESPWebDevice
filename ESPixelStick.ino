/*
* ESPixelStick.ino
*
* Project: ESPixelStick - An ESP8266 and E1.31 based pixel driver
* Copyright (c) 2016 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*/

/*****************************************/
/*        BEGIN - Configuration          */
/*****************************************/

/* Fallback configuration if config.json is empty or fails */
const char ssid[] = "ENTER_SSID_HERE";
const char passphrase[] = "ENTER_PASSPHRASE_HERE";

/*****************************************/
/*         END - Configuration           */
/*****************************************/


#include "ArduinoJson.h"
#include <Hash.h>
#include <SPI.h>
#include "ESPixelStick.h"
#include "EFUpdate.h"
#include "wshandler.h"


extern "C" {
#include <user_interface.h>
}

// Debugging support
#if defined(DEBUG)
extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

static void _u0_putc(char c){
  while(((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}
#endif

/////////////////////////////////////////////////////////
//
//  Globals
//
/////////////////////////////////////////////////////////

// MQTT State
const char MQTT_SET_COMMAND_TOPIC[] = "/set";

// MQTT Payloads by default (on/off)
const char LIGHT_ON[] = "ON";
const char LIGHT_OFF[] = "OFF";

// Configuration file
const char CONFIG_FILE[] = "/config.json";


config_t            config;         // Current configuration
uint32_t            *seqError;      // Sequence error tracking for each universe
uint32_t            *seqZCPPError;  // Sequence error tracking for each universe
uint16_t            lastZCPPConfig; // last config we saw
uint8_t             seqZCPPTracker; // sequence number of zcpp frames
uint16_t            uniLast = 1;    // Last Universe to listen for
bool                reboot = false; // Reboot flag
AsyncWebServer      web(HTTP_PORT); // Web Server
AsyncWebSocket      ws("/ws");      // Web Socket Plugin
uint8_t             *seqTracker;    // Current sequence numbers for each Universe */
uint32_t            lastUpdate;     // Update timeout tracker
WiFiEventHandler    wifiConnectHandler;     // WiFi connect handler
WiFiEventHandler    wifiDisconnectHandler;  // WiFi disconnect handler
Ticker              wifiTicker;     // Ticker to handle WiFi
Ticker              idleTicker;     // Ticker for effect on idle
IPAddress           ourLocalIP;
IPAddress           ourSubnetMask;


/////////////////////////////////////////////////////////
//
//  Forward Declarations
//
/////////////////////////////////////////////////////////

void loadConfig();
void initWifi();
void initWeb();
void updateConfig();

// Radio config
RF_PRE_INIT() {
    //wifi_set_phy_mode(PHY_MODE_11G);    // Force 802.11g mode
    system_phy_set_powerup_option(31);  // Do full RF calibration on power-up
    system_phy_set_max_tpw(82);         // Set max TX power
}

void setup() {
    // Configure SDK params
    wifi_set_sleep_type(NONE_SLEEP_T);

    // Initial pin states
    pinMode(DATA_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);

    // Setup serial log port
    LOG_PORT.begin(115200);
    delay(10);

#if defined(DEBUG)
    ets_install_putc1((void *) &_u0_putc);
    system_set_os_print(1);
#endif

    // Set default data source to E131
    config.ds = DataSource::E131;

    LOG_PORT.println("");
    LOG_PORT.print(F("ESPixelStick v"));
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
    if (config.hostname)
        WiFi.hostname(config.hostname);

/*
#if defined (ESPS_MODE_PIXEL)
    pixels.setPin(DATA_PIN);
    updateConfig();

    // Do one effects cycle as early as possible
    if (config.ds == DataSource::WEB) {
        effects.run();
    }
    // set the effect idle timer
    idleTicker.attach(config.effect_idletimeout, idleTimeout);

    pixels.show();
#else
    updateConfig();
    // Do one effects cycle as early as possible
    if (config.ds == DataSource::WEB) {
        effects.run();
    }
    // set the effect idle timer
    idleTicker.attach(config.effect_idletimeout, idleTimeout);

    serial.show();
#endif
*/

    // Setup WiFi Handlers
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);

    // Fallback to default SSID and passphrase if we fail to connect
    initWifi();
    if (WiFi.status() != WL_CONNECTED) {
        LOG_PORT.println(F("*** Timeout - Reverting to default SSID ***"));
        config.ssid = ssid;
        config.passphrase = passphrase;
        initWifi();
    }

    // If we fail again, go SoftAP or reboot
    if (WiFi.status() != WL_CONNECTED) {
        if (config.ap_fallback) {
            LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, GOING SOFTAP ***"));
            WiFi.mode(WIFI_AP);
            String ssid = "ESPixelStick " + String(config.hostname);
            WiFi.softAP(ssid.c_str());
            ourLocalIP = WiFi.softAPIP();
            ourSubnetMask = IPAddress(255,255,255,0);
        } else {
            LOG_PORT.println(F("*** FAILED TO ASSOCIATE WITH AP, REBOOTING ***"));
            ESP.restart();
        }
    }

    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWiFiDisconnect);

    LOG_PORT.print("IP : ");
    LOG_PORT.println(ourLocalIP);
    LOG_PORT.print("Subnet mask : ");
    LOG_PORT.println(ourSubnetMask);

    // Configure and start the web server
    initWeb();

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

    connectWifi();
    uint32_t timeout = millis();
    while (WiFi.status() != WL_CONNECTED) {
        LOG_PORT.print(".");
        delay(500);
        if (millis() - timeout > (1000 * config.sta_timeout) ){
            LOG_PORT.println("");
            LOG_PORT.println(F("*** Failed to connect ***"));
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

    ourLocalIP = WiFi.localIP();
    ourSubnetMask = WiFi.subnetMask();


    // Setup mDNS / DNS-SD
    //TODO: Reboot or restart mdns when config.id is changed?
    String chipId = String(ESP.getChipId(), HEX);
    MDNS.setInstanceName(String(config.id + " (" + chipId + ")").c_str());
    if (MDNS.begin(config.hostname.c_str())) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        /*
        MDNS.addService("zcpp", "udp", ZCPP_PORT);
        MDNS.addService("ddp", "udp", DDP_PORT);
        MDNS.addService("e131", "udp", E131_DEFAULT_PORT);
        MDNS.addServiceTxt("e131", "udp", "TxtVers", String(RDMNET_DNSSD_TXTVERS));
        MDNS.addServiceTxt("e131", "udp", "ConfScope", RDMNET_DEFAULT_SCOPE);
        MDNS.addServiceTxt("e131", "udp", "E133Vers", String(RDMNET_DNSSD_E133VERS));
        MDNS.addServiceTxt("e131", "udp", "CID", chipId);
        MDNS.addServiceTxt("e131", "udp", "Model", "ESPixelStick");
        MDNS.addServiceTxt("e131", "udp", "Manuf", "Forkineye");
        */
    } else {
        LOG_PORT.println(F("*** Error setting up mDNS responder ***"));
    }
}

void onWiFiDisconnect(const WiFiEventStationModeDisconnected &event) {
    LOG_PORT.println(F("*** WiFi Disconnected ***"));

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

    // Add header for SVG plot support?
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    // Setup WebSockets
    ws.onEvent(wsEvent);
    web.addHandler(&ws);

    // Heap status handler
    web.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });

    // JSON Config Handler
    web.on("/conf", HTTP_GET, [](AsyncWebServerRequest *request) {
        String jsonString;
        serializeConfig(jsonString, true);
        request->send(200, "text/json", jsonString);
    });

    // Firmware upload handler - only in station mode
    web.on("/updatefw", HTTP_POST, [](AsyncWebServerRequest *request) {
        ws.textAll("X6");
    }, handle_fw_upload).setFilter(ON_STA_FILTER);

    // Static Handler
    web.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

    // Raw config file Handler - but only on station
//  web.serveStatic("/config.json", SPIFFS, "/config.json").setFilter(ON_STA_FILTER);

    web.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Page not found");
    });

    DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), "*");

    // Config file upload handler - only in station mode
    web.on("/config", HTTP_POST, [](AsyncWebServerRequest *request) {
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

        // Fallback to embedded ssid and passphrase if null in config
        config.ssid = networkJson["ssid"].as<String>();
        if (!config.ssid.length())
            config.ssid = ssid;

        config.passphrase = networkJson["passphrase"].as<String>();
        if (!config.passphrase.length())
            config.passphrase = passphrase;

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
        config.hostname = "esps-" + String(ESP.getChipId(), HEX);
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
        config.ssid = ssid;
        config.passphrase = passphrase;
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
    device["mode"] = config.devmode.toInt();

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

void idleTimeout() {
   //idleTicker.attach(config.effect_idletimeout, idleTimeout);
}



/////////////////////////////////////////////////////////
//
//  Main Loop
//
/////////////////////////////////////////////////////////
void loop() {
    // Reboot handler
    if (reboot) {
        delay(REBOOT_DELAY);
        ESP.restart();
    }


// workaround crash - consume incoming bytes on serial port
    if (LOG_PORT.available()) {
        while (LOG_PORT.read() >= 0);
    }
}
