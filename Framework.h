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

#ifndef FRAMEWORK_H_
#define FRAMEWORK_H_

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Configuration structure
typedef struct {
    /* Device */
    String      id;             /* Device ID */

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

// Status for display on the OLED display.
enum ConnectionStatus { CONNSTAT_CONNECTING, CONNSTAT_CONNECTED, CONNSTAT_LOCALAP, CONNSTAT_NONE};

typedef struct {
  enum ConnectionStatus status; 
  String                ssid;
  IPAddress             ourLocalIP;
  IPAddress             ourSubnetMask;
  int                   signalStrength;
} connection_status_t;

extern void updateStatus(const connection_status_t & connectionStatus);
extern AsyncWebServer * framework_setup(bool forceAccessPoint);
extern void framework_loop();


#endif  // FRAMEWORK_H_
