#define S Serial
//#define S DBGUDPStream
#if (S == DBGUDPStream)
  #include "dbg.h"
#endif

#define USE_WIFI 0
#define USE_ESPNOW 1
#define OTA_ENABLED 1


// note the next line doesn't stop ESPNowMQTT bridging
#define STATUS_OVER_MQTT 0

#define SEND_THINGSPEAK 1
#define SEND_PUSHOVER 1

#define ESP_NOW_RETRY_LIMIT 1
