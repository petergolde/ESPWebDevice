/*
* ESPixelStick.h
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

#ifndef ESPIXELSTICK_H_
#define ESPIXELSTICK_H_

const char VERSION[] = "3.2";
const char BUILD_DATE[] = __DATE__;

// Mode configuration moved to Mode.h to ease things with Travis
#include "Mode.h"
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncE131.h>
#include <ArduinoJson.h>


#define HTTP_PORT       80      /* Default web server port */
#define MQTT_PORT       1883    /* Default MQTT port */
#define DATA_PIN        2       /* Pixel output - GPIO2 (D4 on NodeMCU) */
#define UNIVERSE_MAX    512     /* Max channels in a DMX Universe */
#define PIXEL_LIMIT     1360    /* Total pixel limit - 40.85ms for 8 universes */
#define RENARD_LIMIT    2048    /* Channel limit for serial outputs */
#define E131_TIMEOUT    1000    /* Force refresh every second an E1.31 packet is not seen */
#define CLIENT_TIMEOUT  15      /* In station/client mode try to connection for 15 seconds */
#define AP_TIMEOUT      60      /* In AP mode, wait 60 seconds for a connection or reboot */
#define REBOOT_DELAY    100     /* Delay for rebooting once reboot flag is set */
#define LOG_PORT        Serial  /* Serial port for console logging */

// E1.33 / RDMnet stuff - to be moved to library
#define RDMNET_DNSSD_SRV_TYPE   "draft-e133.tcp"
#define RDMNET_DEFAULT_SCOPE    "default"
#define RDMNET_DEFAULT_DOMAIN   "local"
#define RDMNET_DNSSD_TXTVERS    1
#define RDMNET_DNSSD_E133VERS   1

// Configuration file params
#define CONFIG_MAX_SIZE 4096    /* Sanity limit for config file */

// Pixel Types
class DevCap {
 public:
    bool MPIXEL : 1;
    bool MSERIAL : 1;
    uint8_t toInt() {
        return (MSERIAL << 1 | MPIXEL);
    }
};

// Data Source to use
enum class DataSource : uint8_t {
    E131,
    MQTT,
    WEB,
    IDLEWEB,
    ZCPP,
    DDP
};

// Configuration structure
typedef struct {
    /* Device */
    String      id;             /* Device ID */
    DevCap      devmode;        /* Used for reporting device mode, not stored */
    DataSource  ds;             /* Used to track current data source, not stored */


    /* Network */
    String      ssid;
    String      passphrase;
    String      hostname;
    uint8_t     ip[4];
    uint8_t     netmask[4];
    uint8_t     gateway[4];
    bool        dhcp;           /* Use DHCP? */
    bool        ap_fallback;    /* Fallback to AP if fail to associate? */
    uint32_t    sta_timeout;    /* Timeout when connection as client (station) */
    uint32_t    ap_timeout;     /* How long to wait in AP mode with no connection before rebooting */




} config_t;

// Forward Declarations
void serializeConfig(String &jsonString, bool pretty = false, bool creds = false);
void dsNetworkConfig(const JsonObject &json);
void dsDeviceConfig(const JsonObject &json);
void dsEffectConfig(const JsonObject &json);
void saveConfig();
void dsGammaConfig(const JsonObject &json);

void connectWifi();
void onWifiConnect(const WiFiEventStationModeGotIP &event);
void onWiFiDisconnect(const WiFiEventStationModeDisconnected &event);
void connectToMqtt();
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* p_payload,
        AsyncMqttClientMessageProperties properties, size_t len,size_t index, size_t total);
void publishState();
void idleTimeout();


#endif  // ESPIXELSTICK_H_
