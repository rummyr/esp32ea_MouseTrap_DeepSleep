////////////////////////////////
// 3.0 is an ESP-NOW variant
////////////////////////////////
#define JKO "\\\""
#define JKEVO "\\\":\\\""
#define JVE "\\\""

#include "credentials.h"
#include "settings.h"

//#define S Serial // look in settings.h
#define MAX_WIFI_WAIT_SECS_STATIC  5 /*very rare with a staticIP to go over this */
#define MAX_WIFI_WAIT_SECS_DHCP  10

#if USE_WIFI
  #include <WiFi.h>
#endif
#ifdef OTA_ENABLED 
  #include <ArduinoOTA.h>
#endif

#include <esp_now.h>
#include <espNowImpl.h>

#include <HTTPClient.h>
#include <NameHelper.h>
#include <ArduinoJson.h>

#include "mousetrapMQTT.h"

typedef enum {
  SLEEP_MODE_POLL = 1,
  SLEEP_MODE_EXT0 = 2,
  SLEEP_MODE_EXT1 = 3,
  SLEEP_MODE_FULL_WAKE_POLL = 4,
} sleepMode_t;


extern "C" int rom_phy_get_vdd33();
uint8_t remoteMac[] = {'r', 'P', 'i', 'N', 'o', 'w'}; // {0x30, 0xAE, 0xA4, 0x01, 0x42, 0x95};


/*
Simple Deep Sleep with Timer Wake Up
=====================================
ESP32 offers a deep sleep mode for effective power
saving as power is an important factor for IoT
applications. In this mode CPUs, most of the RAM,
and all the digital peripherals which are clocked
from APB_CLK are powered off. The only parts of
the chip which can still be powered on are:
RTC controller, RTC peripherals ,and RTC memories

This code displays the most basic deep sleep with
a timer to wake it up and how to store data in
RTC memory to use it over reboots

This code is under Public Domain License.

Author:
Pranav Cherukupalli <cherukupallip@gmail.com>
*/

// prototypes and externs
extern RTC_DATA_ATTR int stubWakeCount;
extern RTC_DATA_ATTR uint64_t stubCumulativeWakeTimeMicros;
extern RTC_DATA_ATTR uint32_t stubPollTimeSecs;
extern RTC_DATA_ATTR gpio_num_t inputPin;
extern RTC_DATA_ATTR gpio_num_t outputPin;
extern RTC_DATA_ATTR gpio_num_t ledPin; 
extern RTC_DATA_ATTR boolean debugWakeStub;
extern RTC_DATA_ATTR boolean flushUARTAtEndOfWakeStub;


String  sendSecureThingSpeak(int field1, int field2, int field3, int field4, int, int, int lastWakeDurationMs, uint32_t cumulativeWakeTimeMs);
String  sendThingSpeakMQTT(char const *channelId, char const *apiKey,int bootCount,int inReading,int ADCbatReading,int internalBatReading,
    int connectTimeMillis, int wifiRSSI,int lastWakeDurationMs,uint32_t cumulativeWakeTimeMs) ;
    
String sendSecurePushover(const char * title, const char * msg);
int  readBatteryRaw();
int initWiFi();
static char const *getLocalDomain();
void setupArduinoOTA();
boolean updateConfigFromPi();
// void handleWiFiFail(const char *msg);
void gotoSleepForSeconds(int seconds, const char *reason, sleepMode_t mode);
extern "C" void wakeStubDoPreCalculations(); // in wakestub.c MUST BE CALLED
extern "C" boolean RTC_IRAM_ATTR arePinsConnected(gpio_num_t in, gpio_num_t out); // in wakestub.c
extern "C" boolean RTC_IRAM_ATTR should_stub_wake_fully(); // for wakestub to call
extern "C" boolean RTC_IRAM_ATTR getCurrentState(int mode); 
extern "C" int RTC_IRAM_ATTR RTCIRAM_readGPIO(gpio_num_t gpioNum);
boolean getCurrentStateMAIN(); 

unsigned long startTimeMillis; // initialized in setup

IPAddress gateway = IPAddress(192,168,11,1);
IPAddress dns= IPAddress(192,168,11,1);
IPAddress dns2= IPAddress(8,8,8,8);
IPAddress subnet = IPAddress(255,255,255,0);

// timing points for debugging info
int timingPointA = -1;
int timingPointB = -1;
int timingPointC = -1;
int timingPointD = -1;
int timingPointE = -1;
int timingPointF = -1;
int timingPointG = -1;
int timingPointH = -1;
int timingPointI = -1;
int timingPointJ = -1;
int timingPointK = -1;
int timingPointL = -1;
int timingPointM = -1;
int timingPointN = -1;
int timingPointO = -1;
int timingPointP = -1;
int timingPointQ = -1;
int timingPointR = -1;

// other DEBUG information
unsigned long connectTimeMillis;
int wifiRSSI;


uint64_t uS_TO_S_FACTOR = 1000000LL;  /* Conversion factor for micro seconds to seconds , */


//#define IN_PIN GPIO_NUM_2
#define LED 5

String baseTopicString; // to pin it in memory

// RTC_DATA_ATTR = RTC Slow Memory 
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int lastState;
RTC_DATA_ATTR int lastPinsConnectedState;
RTC_DATA_ATTR int lastWakeDurationMs = 0;
RTC_DATA_ATTR uint32_t cumulativeWakeTimeMs = 0; // 31 bits of milliseconds = roughly 600 hours
RTC_DATA_ATTR uint32_t wakeFromStubAt;


// need to work out how to persist this for ESP_NOW
// in that mode it could use wifi and read on "boot", then use the persisted version
// this would reduce WiFi usage and still allows for configuration (at boot time)
struct Config {
  boolean configFromPi;
  boolean sendThingSpeak;
  char *thingspeakKey;
  char *thingspeakChannel;
  
  boolean sendPushover;
  
  sleepMode_t sleepMode; // TODO should be an enum!
  int sleepTimeMins;
  int pollIntervalSecs; // if using wakestub, poll this often, will still wake after (approx) sleepTimeMins
  int lingerSeconds; // stay awake after sending message for this long, used when I want to OTA
  
  uint16_t espNowRetries; 
  
  gpio_num_t inPin;
  int inPinMode;
  gpio_num_t outPin;

  boolean debugWakeStub;
  boolean flushUARTAtEndOfWakeStub;
};
RTC_DATA_ATTR Config config = { 
    .configFromPi = false,
    .sendThingSpeak = SEND_THINGSPEAK, 
    .thingspeakKey= strdup("Key-Not-Set"),
    .thingspeakChannel= strdup("Channel-Not-Set"),
    .sendPushover= SEND_PUSHOVER,
    .sleepMode = SLEEP_MODE_EXT0,
    .sleepTimeMins = 10, //short time initially while testing // 2*60,
    .pollIntervalSecs = 5, // really short!
    .lingerSeconds = 0, // time to wait before going into deep sleep (gives OTA a chance)
    .espNowRetries = 5,
    .inPin = GPIO_NUM_2, // default to GPIO 2 for mousetraps, can be overriden by Pi Settings
                        // for the postbox it is GPIO_NUM_36
                        // "output pin" is GPIO_NUM_32 - though not used for the postbox
    .inPinMode = INPUT_PULLUP,
    .outPin = GPIO_NUM_MAX, // MAX is "disabled", 15 & 13 are typically "terminals" on the mouse traps

    .debugWakeStub = false,
    .flushUARTAtEndOfWakeStub = false,
    };


#define WAKEUP_REASON_RTC_IO 1
#define WAKEUP_REASON_RTC_CNTL 2
#define WAKEUP_REASON_TIMER 3
#define WAKEUP_REASON_TOUCH 4
#define WAKEUP_REASON_ULP 5



/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
esp_sleep_wakeup_cause_t print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case 1  : S.println("Wakeup caused by external signal using RTC_IO"); break;
    case 2  : S.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case 3  : S.println("Wakeup caused by timer"); break;
    case 4  : S.println("Wakeup caused by touchpad"); break;
    case 5  : S.println("Wakeup caused by ULP program"); break;
    default : S.printf("Wakeup was not caused by deep sleep .. reason: %d\n", wakeup_reason); break;
  }
  return wakeup_reason;
}

char wakeupUnknown[20]; // wakeupCauseInt
char const *wakeupReasonAsCharPtr(int wakeup_reason) {
  switch(wakeup_reason)
  {
    case 1  : return ("signal RTC_IO");
    case 2  : return("signal RTC_CNTL");
    case 3  : return("timer");
    case 4  : return("touchpad");
    case 5  : return("ULP program");
    default : {sprintf(wakeupUnknown, "Unknown:%02d", wakeup_reason); return(wakeupUnknown);}
  }
}

void setup() {
  boolean thingSpeakSuccess = false;
  boolean pushoverSuccess = false;
  unsigned long mqttTimeMillis =0;
  unsigned long getConfigFromPiMillis = 0;
  unsigned long thingspeakTimeMillis = 0;


  timingPointA = startTimeMillis= millis();
  Serial.begin(115200);
  Serial.printf("configuring inPin %d to mode %d\n", config.inPin, config.inPinMode);
  if (config.flushUARTAtEndOfWakeStub) {
    Serial.flush();
  }
  pinMode(config.inPin, config.inPinMode); 
  inputPin = config.inPin;
  outputPin = config.outPin;
  
  pinMode(LED, OUTPUT);

  

  //whatever happens, whether or not we connect to Wifi
  // Increment boot number and print it every reboot
  ++bootCount;
  esp_sleep_wakeup_cause_t wakeup_reason = print_wakeup_reason();
  // Check GPIO status pin 23 is connected to Ground when mousetrap is "ready"
  int inReading = getCurrentStateMAIN(config.sleepMode);
  int rawBatReading = readBatteryRaw();
  int internalBatReading = getInternalVdd33Adjusted(rawBatReading);
  int ADCbatReading = analogRead(A6);
  String topicBase = String("/ESP/") + getShortName();


  timingPointB = millis();

  const char *sourceFile = strrchr(__FILE__, '\\');
  if (sourceFile == 0L) {
    sourceFile = strrchr(__FILE__, '/');
  }
  if (sourceFile == 0L) {
    sourceFile = "unknown";
  } else {
    sourceFile++; // skip over the '/'
  }
  
  S.printf("Full Boot number: %d\n",bootCount);
  S.printf("Stub Boot number: %d\n", stubWakeCount);
  S.printf("File Timestamp is %s\n", __TIMESTAMP__);
  S.printf("Full Path is %s\n", __FILE__);
  S.printf("File Name is %s\n", sourceFile == 0L ? "null" : sourceFile);
  S.printf("Mousetrap state is %s (%d), last state was %d\n", inReading ? "Trap has Sprung": "Trap is Set", inReading, lastState);
  S.printf("scaled battery reading (before wifi) is %d, unScaled %d\n",internalBatReading, rawBatReading);
  S.printf("ADC battery Reading (io34/A6) is %d, internal raw Reading: %d\n", ADCbatReading, rawBatReading);
  S.printf("topicBase is %s\n", topicBase.c_str());
  S.printf("wakeStub cumulative time micros %llu\n", stubCumulativeWakeTimeMicros);
  printConfig();



// always try to initialize ESPNow
  WiFi.mode(WIFI_STA); // required because otherwise it aint setup!
  WiFi.setAutoConnect(false);
  if (!initESPNOWSenderTo(remoteMac)) {
    gotoSleepForSeconds(15*60,"ESPNow failed to initialize, might try again in 15 minutes", SLEEP_MODE_FULL_WAKE_POLL);    
  }
  ESPNOWSetRetryLimit(config.espNowRetries);
  setESPNOWDefaultBaseMQTTTopic((String("/ESP/") + getShortName()).c_str());
  // char const *baseTopic = getESPNOWDefaultBaseMQTTTopic();
  // tbhis will work because WiFi is not connected, so won't be to a random channel
  sendESPNOWMQTT("boot", "booted");


// optionally connect to WiFi .. 
// e.g. to retreive settings if not already retreived


  timingPointF = millis();


// turn the LED on for a second to show we're awake or blink while connecting to WiFi
digitalWrite(LED, LOW);
// if we haven't got any settings, then ALWAYS connect to WiFi and pull from RPi
  if (!config.configFromPi) {
    sendESPNOWMQTT("boot", "getting Settings from Pi");
    initWiFi();
    // check if WiFi is connected, otherwise, goto sleep and try again in 15 minutes!
    if (WiFi.status() != WL_CONNECTED) {
     gotoSleepForSeconds(15*60, "Wifi failed to connect, trying again in 15 minutes", SLEEP_MODE_FULL_WAKE_POLL);
    }
  }
  digitalWrite(LED, HIGH); // turn LED offf


//Get here we should have connected to WiFi and/or ESPNOW (or have gone back into deep sleep
// due to no wifi connection)


  timingPointG = millis();

  // since we have WiFi we might as well get our configuration from node-red
  // after this all happens via ESPNow!
  if (WiFi.status() == WL_CONNECTED) {
    getConfigFromPiMillis = millis();
    updateConfigFromPi();
    getConfigFromPiMillis = millis() - getConfigFromPiMillis;
    ESPNOWSetRetryLimit(config.espNowRetries); // in case it has changed!
    if (config.configFromPi) {
        Serial.println("After updating config from Pi");
        printConfig();
    }
    else {
      Serial.println("WARNING: Updating config from Pi failed!");
      printConfig();
    }
  }


  // if WiFi is connected , and the channel is wrong, and we *DID* manage to get the settings!
// always do the short sleep thingy if settings *ARE* retrieved
// if wifi is not connected, OR settings aren't retreived we will proceed with the built in settings
// aka ??? minutes
  if (WiFi.status() == WL_CONNECTED && config.configFromPi) {
//  if (WiFi.status() == WL_CONNECTED && WiFi.channel() != ESPNOW_CHANNEL && config.configFromPi ) {
    Serial.printf("doing a FAST sleep to reinitialize ESP NOW, settingsRetrieved:%d, channel:%d\n", config.configFromPi , WiFi.channel());

    Serial.println("deep sleeping for 1 second, expect a wakeup");
    delay(100);
    gotoSleepForSeconds(1,"FastSleep for 1 second on boot to reInitialize WiFi/ESPNow", SLEEP_MODE_FULL_WAKE_POLL);
    Serial.println("Shouldn't reach Here!");

    
    // disconnect WiFi and deinitialize espNow
    stopESPNow();
    WiFi.setAutoConnect(false);
    WiFi.mode(WIFI_OFF);

    if (config.flushUARTAtEndOfWakeStub) {
      Serial.flush();
      delay(1000);
    }
    
    esp_sleep_enable_timer_wakeup(1 * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
    // init espnow again
    if (!initESPNOWSenderTo(remoteMac)) {
      gotoSleepForSeconds(15*60, "ESPNow failed to re-initialize after disconnecting WiFi, trying again in 15 minutes", SLEEP_MODE_FULL_WAKE_POLL);    
    };
  }
  Serial.println("Ready to start to report status");
  

  if (config.sendPushover) {
    String myName = (String("MouseTrap ") + getShortName());
      if (((bootCount==1) || (lastState != inReading))) { // push message on startup and/or change
        sendESPNOW("Pushover", (myName + " " + (bootCount==1 ? "Boot: " : "") + (inReading ? "Trap is Sprung": "Trap is Set")).c_str());
      }
      if (internalBatReading < 2900) {
        sendESPNOW("Pushover", (myName + "Possibly Low Voltage <2900").c_str());
      }
  } // pushover enabled




    Serial.printf("About to MQTT Send via ESPNOW\n");
    sendESPNOWMQTT("status", String(inReading ? "Trap is sprung": "Trap is set").c_str());
    sendESPNOWMQTT("trapStateDigital", inReading);
    sendESPNOWMQTT("Battery", internalBatReading);
    sendESPNOWMQTT("BatteryADCRaw", ADCbatReading);
    sendESPNOWMQTT("WiFiRSSI", wifiRSSI);
    sendESPNOWMQTT("connectTimeMillis", connectTimeMillis);
    sendESPNOWMQTT("lastWakeDurationMs", lastWakeDurationMs);
    sendESPNOWMQTT("bootCount", bootCount);
    sendESPNOWMQTT("cumulativeWakeTimeMs", cumulativeWakeTimeMs);
    sendESPNOWMQTT("stubCumulativeWakeTimeMicros", stubCumulativeWakeTimeMicros);
    sendESPNOWMQTT("debug/config/sendToThingSpeak", config.sendThingSpeak? "true": "false");
    sendESPNOWMQTT("debug/config/sendToPushover", config.sendPushover? "true": "false");
    sendESPNOWMQTT("debug/config/thingspeakKey", config.thingspeakKey);
    sendESPNOWMQTT("debug/config/thingspeakChannel", config.thingspeakChannel);
    sendESPNOWMQTT("debug/config/sleepTimeMins", config.sleepTimeMins);
    sendESPNOWMQTT("debug/config/lingerSeconds", config.lingerSeconds);
    sendESPNOWMQTT("debug/fileTimestamp", __TIMESTAMP__);
    sendESPNOWMQTT("debug/sourceFile", sourceFile);
    sendESPNOWMQTT("debug/wakeupCause", wakeupReasonAsCharPtr(wakeup_reason));
    sendESPNOWMQTT("debug/timings/millisNow", millis());
    sendESPNOWMQTT("debug/timings/connectTimeMillis", connectTimeMillis);

     mqttTimeMillis = millis() - mqttTimeMillis;

    timingPointJ = millis();
  
// send a message to thingspeak, every time we wake (whatever reason)
  sendESPNOW("ThingSpeak",
    (char const *const) (String("{") 
    +  JKO + "bootCount" + JKEVO + bootCount + JVE
    + "," + JKO + "inReading" + JKEVO + inReading + JVE
    + "," + JKO + "ADCbatReading" + JKEVO + ADCbatReading + JVE
    + "," + JKO + "internalBatReading" + JKEVO + internalBatReading + JVE
    + "," + JKO + "connectTimeMillis" + JKEVO + connectTimeMillis + JVE
    + "," + JKO + "wifiRSSI" + JKEVO + wifiRSSI + JVE
    + "," + JKO + "lastWakeDurationMs" + JKEVO + lastWakeDurationMs + JVE
    + "," + JKO + "cumulativeWakeTimeMs" + JKEVO + cumulativeWakeTimeMs + JVE
    + "}"
    ).c_str());
    
  lastState = inReading;




  sendESPNOWMQTT("debug/timings/mqttTimeMillis", mqttTimeMillis);
  sendESPNOWMQTT("debug/timings/connectTimeMillis", connectTimeMillis);
  sendESPNOWMQTT("debug/timings/getConfigFromPiMillis", getConfigFromPiMillis);
  sendESPNOWMQTT("debug/timings/timeSoFarMillis", millis() - startTimeMillis);
  sendESPNOWMQTT("debug/timings/thingspeakTimeMillis", thingspeakTimeMillis);

  sendESPNOWMQTT("debug/timings/timingPoints", (String("") 
  + timingPointA + ","
  + timingPointB + ","
  + timingPointC + ","
  + timingPointD + ","
  + timingPointE + ","
  + timingPointF + ","
  + timingPointG + ","
  + timingPointH + ","
  + timingPointI + ","
  + timingPointJ + ","
  + timingPointK + ","
  + timingPointL + ","
  + timingPointM + ","
  + timingPointN + ","
  // rest are below here!
  /*
  + timingPointO + ","
  + timingPointP + ","
  + timingPointQ + ","
  + timingPointR + ","
  + timingPointS + ","
  + timingPointT + ","
  + timingPointU + ","
  + timingPointV + ","
  + timingPointW + ","
  + timingPointX + ","
  + timingPointY + ","
  + timingPointZ + ","
  */).c_str()
  );
    

  sendESPNOWMQTT("debug/espNow/sentMessageCount", ESPNOWGetSentMessageCount());
  sendESPNOWMQTT("debug/espNow/retriesCount", ESPNOWGetTotalRetriesCount());
  sendESPNOWMQTT("debug/espNow/totalSendingTimeMillis", ESPNOWGetTotalSendingTimeMs());
  sendESPNOWMQTT("debug/espNow/failedMessageCount", ESPNOWGetTotalFailedSends());



  timingPointO = millis();

    
    if (config.lingerSeconds >0 && config.lingerSeconds < 120) {
#ifdef __ARDUINO_OTA_H
    if (WiFi.status() == WL_CONNECTED) {
      setupArduinoOTA();
      ArduinoOTA.handle();
    }
#endif

  sendESPNOWMQTT("debug/Linger", "about to Linger");

      for (int i=0;i<config.lingerSeconds*1000;i+=100) {
#ifdef __ARDUINO_OTA_H
        if (WiFi.status() == WL_CONNECTED) {
          ArduinoOTA.handle();
        }
#endif
          delay(100);
      }
  }

  sendESPNOWMQTT("debug/SigningOff", "Bye");


  timingPointP = millis();

  // does disconnect seems to slow to the next connect ? WiFi.disconnect(true /*wifiOff*/);
  // WiFi.disconnect(true /*wifiOff*/);
  // WIFI_OFF takes ~400ms!   WiFi.mode(WIFI_OFF);
  // do we need to END ArduinoOTA?
  // delay(250); // does this need to be 250ms?

  timingPointQ = millis();


  timingPointR = millis();


  S.printf("Been awake for %ld millis\n", (millis()- startTimeMillis) );
  S.printf("missing timing points:\n\tN:%d\n\tO:%d\n\tP:%d\n\t(WiFi_OFF)\n\tQ:%d\n\tR:%d\n",
    timingPointN,timingPointO,timingPointP,timingPointQ,timingPointR);
  gotoSleepForSeconds(config.sleepTimeMins*60, "End of Normal Boot Cycle", config.sleepMode);

// shouldn't reach here!

  /*
  First we configure the wake up source
  We set our ESP32 to wake up every 5 seconds
  */
  stopESPNow();
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup(config.sleepTimeMins*60 * uS_TO_S_FACTOR);
  S.println("Setup ESP32 to sleep for every " + String(config.sleepTimeMins) +  " Seconds");
  S.println("Setup ESP32 to sleep for (round tripped)" + String((long)((config.sleepTimeMins*uS_TO_S_FACTOR)/uS_TO_S_FACTOR)) + " seconds");

  // also enable on pin High or Low (whichever it ISNT)
  if (inReading == 0) {
    S.printf("Enabling wakeup on pin%d HIGH - waiting to trigger when it goes to 1, current value is stored as %d (now is %d)", config.inPin, inReading,  digitalRead(config.inPin));
    S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 1)); //1 = High, 0 = Low
  } else {
    S.printf("Enabling wakeup on pin%d LOW - waiting to trigger when it goes 0, current value is stored as %d (now is %d)", config.inPin,inReading,  digitalRead(config.inPin));
    S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 0)); //1 = High, 0 = Low
  }

    
  S.printf("Going to sleep now for %d minutes", config.sleepTimeMins);
  if (config.flushUARTAtEndOfWakeStub) {
     Serial.flush();
      delay(1000);
  }

  //Go to sleep now
  unsigned long endTimeMillis = millis();
  if (endTimeMillis > startTimeMillis) { // safe reading
    lastWakeDurationMs = endTimeMillis - startTimeMillis;
  } else {
    lastWakeDurationMs = 0;
  }
  cumulativeWakeTimeMs += lastWakeDurationMs;
  esp_deep_sleep_start();
  S.println("This will never be printed");
  delay(1000);
}

void loop() {
  //This is not going to be called
  S.println("Loop should never be called!");
}


void setupArduinoOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("setupOTA doing nothing, not connected");
    return;
  }

#ifdef __ARDUINO_OTA_H
  S.println("setupOTA");
  const char *name = getName();
  if (name != NULL) {
    ArduinoOTA.setHostname(name);
  }
  ArduinoOTA.setPort(8266);

    // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    S.println("Start updating");
  });
  ArduinoOTA.onEnd([]() {
    S.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    S.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    S.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) S.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) S.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) S.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) S.println("Receive Failed");
    else if (error == OTA_END_ERROR) S.println("End Failed");
  });
  ArduinoOTA.begin();
#else
  Serial.println("ArduinoOTA NOT compiled in");
  S.println("ArduinoOTA NOT compiled in");
#endif  

}



int  readBatteryWithAdjustment() {
  int internalBatReading;
  if (WiFi.status() == 255) {
      btStart();
      internalBatReading = rom_phy_get_vdd33();
      btStop();
  }
  else {
      internalBatReading = rom_phy_get_vdd33();
  }
  S.printf("Raw internal battery reading:%d\n", internalBatReading);
  return getInternalVdd33Adjusted(internalBatReading);
}


int  readBatteryRaw() {
  int internalBatReading;
  if (WiFi.status() == 255) {
      btStart();
      internalBatReading = rom_phy_get_vdd33();
      btStop();
  }
  else {
      internalBatReading = rom_phy_get_vdd33();
  }
  S.printf("readBatteryRaw() reading:%d\n", internalBatReading);
  return internalBatReading;
}



void printConfig() {
  Serial.printf("Config:\n");
  Serial.printf("\tconfigFromPi:%s\n",                config.configFromPi ? "true":"false");
  Serial.printf("\tsendThingSpeak:%s\n",              config.sendThingSpeak ? "true":"false");
  Serial.printf("\tthingspeakKey:%s\n",               config.thingspeakKey);
  Serial.printf("\tthingspeakChannel::%s\n",          config.thingspeakChannel);
  Serial.printf("\tsendPushover:%s\n",                config.sendPushover ? "true":"false");
  Serial.printf("\tsleepMode:%s\n",                   config.sleepMode == SLEEP_MODE_POLL ? "POLL" : 
                                                      config.sleepMode == SLEEP_MODE_EXT0 ? "EXT0" : 
                                                      config.sleepMode == SLEEP_MODE_EXT1 ? "EXT1" : 
                                                      config.sleepMode == SLEEP_MODE_FULL_WAKE_POLL ? "POLL FULL WAKE" :
                                                      "UNKNOWN"
                                                      );
  Serial.printf("\tsleepTimeMins:%d\n",               config.sleepTimeMins);
  Serial.printf("\tpollIntervalSecs:%d\n",            config.pollIntervalSecs);
  Serial.printf("\tlingerSeconds:%d\n",               config.lingerSeconds);
  Serial.printf("\tespNowRetries:%d\n",               config.espNowRetries); 
  Serial.printf("\tinPin:%d\n",                       config.inPin);
  Serial.printf("\toutPin:%d\n",                      config.outPin);
  Serial.printf("\tinPinMode:%s\n",                   config.inPinMode == INPUT ? "INPUT" : config.inPinMode == INPUT_PULLUP ? "INPUT_PULLUP" : config.inPinMode == INPUT_PULLDOWN ? "INPUT_PULLDOWN" : "UNKNOWN");
  Serial.printf("\tdebugWakeStub:%s\n",               config.debugWakeStub ? "true":"false");
  Serial.printf("\tflushUARTAtEndOfWakeStub:%s\n",    config.flushUARTAtEndOfWakeStub ? "true":"false");
}
/*
 * read some config from Pi
 * mode unset => ext0 + input_pullup              (sleep mode is ext0 + timer)
 *      EXT0  => default
 *      STUB  => use wakestub to poll              (sleep mode is just timer)
 *      POLL  => no wakestub, just check on wakeup (sleep mode is timer only)
 * sleepTimeMins -> time between FULL Wakeups to report status
 * pollIntervalSecs -> time between stub wakeups (if enabled by mode=STUB)
 * inPinMode -> INPUT, INPUT_PULLUP, INPUT_PULLDOWN
 */
boolean updateConfigFromPi() {
  // get from raspberry pi
  HTTPClient client;
  String piName = "192.168.11.110";

//  Serial.println("Trying to find localDomain suffix\n");
//  const char *localDomain = getLocalDomain();
//  if (*localDomain != 0) {
//    piName = String("raspberrypi.") + localDomain;
//  }
  String url = String("http://" + piName + ":1880/ESP/config?name=") + getShortName();
  S.printf("Attempting to get config from %s\n", url.c_str());
  client.begin(url.c_str());
  int statusCode = client.GET();
  S.printf("Status code for getting config is %d\n", statusCode);
  String payload;
  if (statusCode == 200) {
    payload = client.getString();
    Serial.println(payload);
  }
  client.end();


  if (statusCode == 200) {
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(payload);
       boolean thingspeak = root["thingspeak"];
       boolean pushover = root["pushover"];
       boolean invalid = root["invalid"];
       String thingspeakKey = root["thingspeakKey"];
       String thingspeakChannel = root["thingspeakChannel"];
       int sleepTimeMins = config.sleepTimeMins;
       sleepMode_t mode = config.sleepMode;
       gpio_num_t inPin = config.inPin;
       int inPinMode = config.inPinMode;
       gpio_num_t outPin = config.outPin;
       boolean debugWakeStub = config.debugWakeStub;
       boolean flushUARTAtEndOfWakeStub = config.flushUARTAtEndOfWakeStub;



      
       if (root.containsKey("mode")) {
          String modeStr = root["mode"];
          if (modeStr == "EXT0") {
            mode = SLEEP_MODE_EXT0;
          } else if (modeStr == "POLL") {
            mode = SLEEP_MODE_POLL;
          } else {
            mode = SLEEP_MODE_EXT0;
          }
       }
       if (root.containsKey("sleepTimeMins")) {
          sleepTimeMins = root["sleepTimeMins"];
       }
       
       int pollIntervalSecs = config.pollIntervalSecs;
       if (root.containsKey("pollIntervalSecs")) {
          pollIntervalSecs = root["pollIntervalSecs"];
       }
       
       int lingerSeconds = root["lingerSeconds"];
       uint16_t espNowRetries = root["espNowRetries"];
       
       if (root.containsKey("inPin")) {
          inPin = (gpio_num_t) ((int)root["inPin"]);
          S.printf("Setting \"inPin\" to %d\n", inPin);
       } else {
          S.printf("inPin left at default of %d\n", inPin);
       }
       
       if (root.containsKey("inPinMode")) {
          String inPinModeS = root["inPinMode"];
          S.printf("inPinMode read as %s\n", inPinModeS.c_str());
          if (inPinModeS == "INPUT_PULLUP") {
            inPinMode = INPUT_PULLUP;
          } else if (inPinModeS == "INPUT_PULLDOWN") {
            inPinMode = INPUT_PULLDOWN;
          } else if (inPinModeS == "INPUT") {
            inPinMode = INPUT;
          } else {
            S.printf("Invalid in pin mode %s, expect INPUT | INPUT_PULLUP | INPUT_PULLDOWN\n", inPinModeS.c_str());
          }
          S.printf("Setting \"inPinMode\" to %d\n", inPinMode);
       } else {
          S.printf("inPinMode left at default of %d\n", inPinMode);
       }
       if (root.containsKey("outPin")) {
          outPin = (gpio_num_t) ((int)root["outPin"]);
          S.printf("Setting \"outPin\" to %d\n", outPin);
       } else {
          S.printf("outPin left at default of %d\n", outPin);
       }       

       if (root.containsKey("debugWakeStub")) {
        debugWakeStub = root["debugWakeStub"];
       }
       if (root.containsKey("flushUARTAtEndOfWakeStub")) {
        flushUARTAtEndOfWakeStub = root["flushUARTAtEndOfWakeStub"];
       }

       
       // update the config
       S.printf("thingspeak:%d pushover:%d invalid:%d thingspeakKey:%s\n",thingspeak, pushover, invalid, thingspeakKey.c_str() );
       config.sendThingSpeak = thingspeak;
       config.sendPushover = pushover;
       config.sleepTimeMins = sleepTimeMins;
       config.pollIntervalSecs = pollIntervalSecs;
       if (config.thingspeakKey != NULL) {
         free(config.thingspeakKey);
       }
       config.thingspeakKey = strdup(thingspeakKey.c_str());
       config.thingspeakChannel = strdup(thingspeakChannel.c_str());
       config.lingerSeconds = lingerSeconds;
       config.espNowRetries = espNowRetries;
       config.inPin = inPin;
       config.inPinMode = inPinMode;
       config.outPin = outPin;
       config.sleepMode= mode;
       config.debugWakeStub = debugWakeStub;
       config.flushUARTAtEndOfWakeStub = flushUARTAtEndOfWakeStub;

       config.configFromPi = true;
  }  else {
    
  }
  return config.configFromPi;
}


int initWiFi() {
    // get WIFI going...
  
  timingPointC = millis();

  Serial.printf("Wifi Status is currently:%d\n", WiFi.status());

  if (WiFi.status() == WL_CONNECTED) {
    // we're already connected, just return
    return true;
  }

  
  uint32_t staticIP  = getIPAddress32bit();

  connectTimeMillis = millis();
  if (staticIP != 0){
    IPAddress ip = IPAddress(staticIP);
    Serial.print("Using a static IP address (");
    Serial.print(ip);
    Serial.println(") to connect\n");
    WiFi.config(ip, gateway,subnet,dns, dns2); 
  }

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  Serial.printf("Setting hostname to %s\n", getShortName());
  WiFi.setHostname(getShortName());
  WiFi.setAutoReconnect(0);

  timingPointD = millis();

  // blink the LED while we're conecting
  // put it back on once were connected
  int waitTime = 0;
  int maxWaitTime = MAX_WIFI_WAIT_SECS_DHCP * 1000;
  if (staticIP != 0){
      maxWaitTime = MAX_WIFI_WAIT_SECS_STATIC * 1000;
  }
  
  while(waitTime < maxWaitTime && WiFi.status() != WL_CONNECTED) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected!");
      break;
    }
    else {
      Serial.print (".");
    }
    delay(100);
    waitTime += 100;
    digitalWrite(LED, !digitalRead(LED));
  }
  connectTimeMillis = millis() - connectTimeMillis;
  digitalWrite(LED, LOW); // ensure the LED is on once connected



  Serial.printf("Done wait, wifi state is %d, connected in approx %d ms\n", (int)WiFi.status(), (waitTime));
  wifiRSSI = WiFi.RSSI();
  timingPointE = millis();
  return WiFi.status() == WL_CONNECTED;
}

void handleWiFiFail(char const *msg) {
    gotoSleepForSeconds(15*60, msg, SLEEP_MODE_FULL_WAKE_POLL);
}


/** puts into Deep Sleep with appropriate wakeups.
shuts down as much as is possible.
*/
void gotoSleepForSeconds(int seconds, char const *reason, sleepMode_t mode) {
      // send a final ESPNow message
      sendESPNOWMQTT("debug/ShuttingDown", reason);
      
      Serial.println(reason);

      
      WiFi.setAutoConnect(false);
      WiFi.mode(WIFI_OFF);
      stopESPNow();
      int inReading = digitalRead(config.inPin);

      
      wakeStubDoPreCalculations();
      debugWakeStub = config.debugWakeStub;
      flushUARTAtEndOfWakeStub = config.flushUARTAtEndOfWakeStub;


      
      stubPollTimeSecs = config.pollIntervalSecs;
      switch (mode) {
        default:
        case SLEEP_MODE_FULL_WAKE_POLL :
        case SLEEP_MODE_EXT0: {
          esp_sleep_enable_timer_wakeup(seconds * uS_TO_S_FACTOR);
          S.println("Sleep: FULL|EXT0 Setup ESP32 to sleep for " + String(seconds) +  " Seconds");
          S.println("Sleep: FULL|EXT0 Setup ESP32 to sleep for (round tripped)" + String((long)((seconds*uS_TO_S_FACTOR)/uS_TO_S_FACTOR)) + " seconds");
  
  
          // also enable on pin High or Low (whichever it ISNT)
          if (inReading == 0) {
            S.printf("Sleep: FULL|EXT0 Enabling wakeup on pin%d HIGH - waiting to trigger when it goes to 1, current value is stored as %d (now is %d)", config.inPin, inReading,  digitalRead(config.inPin));
            S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 1)); //1 = High, 0 = Low
          } else {
            S.printf("Sleep: FULL|EXT0 Enabling wakeup on pin%d LOW - waiting to trigger when it goes 0, current value is stored as %d (now is %d)", config.inPin,inReading,  digitalRead(config.inPin));
            S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 0)); //1 = High, 0 = Low
          }
          break;
        }
        case SLEEP_MODE_POLL: {
          // ensure some IO is set to as quiet as possible 
          S.printf("Sleep:POLL putting pins into INPUT mode\n");
          if (config.outPin < GPIO_NUM_MAX && config.outPin >= 0) {
            pinMode(config.outPin, INPUT);
          }
          pinMode(config.inPin, INPUT);
          pinMode(GPIO_NUM_13, INPUT);
          pinMode(GPIO_NUM_15, INPUT);
          pinMode(GPIO_NUM_2, INPUT);
          S.printf("Sleep:POLL stub wakeup in %d secs\n", config.pollIntervalSecs);
          esp_sleep_enable_timer_wakeup(config.pollIntervalSecs * uS_TO_S_FACTOR);
          wakeFromStubAt = stubWakeCount + config.sleepTimeMins*60 / config.pollIntervalSecs;
          S.printf("Sleep: Enabling POLLING wakeup every %u Secs, full wakeup at %u polls\n", config.pollIntervalSecs ,wakeFromStubAt);
          break;
        }
        case SLEEP_MODE_EXT1: {
          S.printf("SLEEP MODE EXT1 NOT YET IMPLEMENTED\n");
          break;
        }
      } // end switch on mode

      unsigned long endTimeMillis = millis();
      if (endTimeMillis > startTimeMillis) { // safe reading
        lastWakeDurationMs = endTimeMillis - startTimeMillis;
      } else {
        lastWakeDurationMs = 0;
      }

      //Go to sleep now
      S.println("Going to sleep now\n\n\n\n");

//      if (config.flushUARTAtEndOfWakeStub) {
        Serial.flush();
//      }
      cumulativeWakeTimeMs += lastWakeDurationMs;
      esp_deep_sleep_start();

      
      S.println("Should never reach here!");    
  
}

#include "soc/soc.h"
#include "soc/uart_reg.h"
static RTC_IRAM_ATTR void flushUART() {
      // Wait for UART to end transmitting.
      while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) { // soc\uart_reg.h
            ;
        }
}// end if waiting for UART to flush


RTC_IRAM_ATTR boolean getCurrentState(int sleepMode) {
/*   
 do { 
     static RTC_RODATA_ATTR const char fmt[] = "\tin IRAM getCurrentState\n"; 
     ets_printf(fmt); 
     flushUART();
     } while (0);
 */    
   if (config.outPin < GPIO_NUM_0 || config.outPin >= GPIO_NUM_MAX) {
    // output disabled, just read the in pin
       return GPIO_INPUT_GET(config.inPin);
   } else {
      return arePinsConnected(config.inPin, config.outPin);
   }
/** old code
  switch (sleepMode) {
    default:
    case SLEEP_MODE_EXT0:
    case SLEEP_MODE_EXT1:
    case SLEEP_MODE_FULL_WAKE_UP:
       return GPIO_INPUT_GET(config.inPin);
       ;
    case SLEEP_MODE_POLL:
      return arePinsConnected(config.inPin, config.outPin);
      ;
  }
*/  
}

// returns TRUE for sprung
boolean getCurrentStateMAIN(int sleepMode) {
   Serial.printf("getCurrentStateMAIN() inPin %d, outPin %d\n", config.inPin, config.outPin);
   if (config.flushUARTAtEndOfWakeStub) {
     Serial.flush();
   }

   if (config.outPin < GPIO_NUM_0 || config.outPin >= GPIO_NUM_MAX) {
    // output disabled, just read the in pin
       return digitalRead(config.inPin);
   } else {
      return !arePinsConnectedMAIN(config.inPin, config.outPin);
   }

/* old code
  switch (sleepMode) {
    default:
    case SLEEP_MODE_EXT0:
    case SLEEP_MODE_EXT1:
      Serial.printf("getCurrentStateMAIN() digitalRead pin %d\n", config.inPin);
      Serial.flush();
      return digitalRead(config.inPin);
    case SLEEP_MODE_POLL:
      Serial.printf("getCurrentStateMAIN() calling arePinsConected\n");
      Serial.flush();
      // cant call IRAM code for some reason
      return arePinsConnectedMAIN(config.inPin, config.outPin);
  }
*/  
}

boolean arePinsConnectedMAIN(gpio_num_t in, gpio_num_t out) {
  boolean input_and_output_connected = true;
  int inputState;

  pinMode(out, OUTPUT);
  digitalWrite(out, 0);
  inputState = digitalRead(in);
  Serial.printf("\twith output LOW input is %d\n", inputState);

  if (inputState != 0) {
    Serial.printf("\tinput and output pins are NOT connected\n");
    return false;
  }

  digitalWrite(out, 1);
  inputState = digitalRead(in);
  Serial.printf("\tWith output high, input is %d\n", inputState);

  if (inputState != 1) {
    Serial.printf("\tinput and output pins are NOT connected\n");
    return false;
  }
    
  Serial.printf("\tinput and output pins are connected\n");

  return true;
}

/** STUB function, be careful what you call! */
boolean RTC_IRAM_ATTR  should_stub_wake_fully() {
  if (wakeFromStubAt <= stubWakeCount) {
    return true;
  }
  // check to see if input and output are connected
  boolean currentState = getCurrentState(config.sleepMode);
  if (lastPinsConnectedState == currentState) {
    return false;
  }
  lastPinsConnectedState = currentState;
  return true;
  
}

///////////////////////////////////////////////////////////////////////////////
//              getLocalDomain                                               //
//  returns .lan, .home, .local or an empty char *!                          //
///////////////////////////////////////////////////////////////////////////////
static char const *getLocalDomain() {
  char const *testDomainNames[] = {".home",  ".lan", ".local"};
    if (WiFi.status() != WL_CONNECTED) {
      return "";
    }
    // could have potentially used wifi_station_get_hostname() in user_interface.h
#ifdef ESP8266    
    const char   *hostname = strdup(WiFi.hostname().c_str());
#else
  const char   *hostname = strdup(WiFi.getHostname());
#endif
    Serial.printf("getLocalDomain my Hostname is %s\n",hostname);


    // find sizeof string to allocate for the lookup
    int maxDomainNameLength = 0;
    for (int i=0;i<sizeof(testDomainNames) / sizeof(testDomainNames[0]); i++) {
      if (maxDomainNameLength  < strlen(testDomainNames[i])) {
        maxDomainNameLength = strlen(testDomainNames[i]);
      }
    }

//Serial.printf("allocSize is %d\n", 1 + maxDomainNameLength + strlen(hostname));
    char *tstFQDN = (char  *)calloc(2 + maxDomainNameLength + strlen(hostname), sizeof(char));
    IPAddress throwAway;
    char const *domainName = NULL;

    for (int i=0;i<sizeof(testDomainNames) / sizeof(testDomainNames[0]); i++) {
      sprintf(tstFQDN,"%s%s", hostname, testDomainNames[i]);
      Serial.printf("looking up %s\n", tstFQDN);
      int retCode = WiFi.hostByName(tstFQDN, throwAway);
      if (retCode == 1) {
        Serial.printf("look up of %s successful\n", tstFQDN);
        domainName = testDomainNames[i];
        break;
      }
      Serial.printf("look up of %s failed\n", tstFQDN);
    }
//Serial.printf("testing is complete\n");
    free(tstFQDN);
#ifdef ESP8266    
    free(hostname);
#endif
    if (domainName == NULL) {
      domainName = "";
    }
    return domainName;    
}

