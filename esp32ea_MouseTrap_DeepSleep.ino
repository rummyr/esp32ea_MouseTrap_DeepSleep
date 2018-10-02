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

// prototypes
String  sendSecureThingSpeak(int field1, int field2, int field3, int field4, int, int, int lastWakeDurationMs, uint32_t cumulativeWakeTimeMs);
String  sendThingSpeakMQTT(char const *channelId, char const *apiKey,int bootCount,int inReading,int ADCbatReading,int internalBatReading,
    int connectTimeMillis, int wifiRSSI,int lastWakeDurationMs,uint32_t cumulativeWakeTimeMs) ;
    
String sendSecurePushover(const char * title, const char * msg);
int  readBatteryRaw();
int initWiFi();
void setupArduinoOTA();
void updateConfigFromPi();
// void handleWiFiFail(const char *msg);
void gotoSleepForSeconds(int seconds, const char *reason);




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
RTC_DATA_ATTR int lastWakeDurationMs = 0;
RTC_DATA_ATTR uint32_t cumulativeWakeTimeMs = 0; // 31 bits of milliseconds = roughly 600 hours
RTC_DATA_ATTR boolean settingsRetrieved = false;


// need to work out how to persist this for ESP_NOW
// in that mode it could use wifi and read on "boot", then use the persisted version
// this would reduce WiFi usage and still allows for configuration (at boot time)
struct Config {
  boolean sendThingSpeak;
  boolean sendPushover;
  char *thingspeakKey;
  char *thingspeakChannel;
  int sleepTimeMins;
  int lingerSeconds; // stay awake after sending message for this long, used when I want to OTA
  uint16_t espNowRetries; 
  gpio_num_t inPin;
  int inPinMode;
};
RTC_DATA_ATTR Config config = { 
    .sendThingSpeak = SEND_THINGSPEAK, 
    .sendPushover= SEND_PUSHOVER,
    .thingspeakKey= strdup("Key-Not-Set"),
    .thingspeakChannel= strdup("Channel-Not-Set"),
    .sleepTimeMins = 10, //short time initially while testing // 2*60,
    .lingerSeconds = 0,
    .espNowRetries = 5,
    .inPin = GPIO_NUM_2, // default to GPIO 2 for mousetraps, can be overriden by Pi Settings
                        // for the postbox it is GPIO_NUM_36
                        // "output pin" is GPIO_NUM_32 - though not used for the postbox
    .inPinMode = INPUT_PULLUP
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
  unsigned long wifiTimeMillis =0;
  unsigned long getConfigFromPiMillis = 0;
  unsigned long thingspeakTimeMillis = 0;


  timingPointA = startTimeMillis= millis();
  Serial.begin(115200);
  pinMode(config.inPin, config.inPinMode); 
  pinMode(LED, OUTPUT);

  

  //whatever happens, whether or not we connect to Wifi
  // Increment boot number and print it every reboot
  ++bootCount;
  esp_sleep_wakeup_cause_t wakeup_reason = print_wakeup_reason();
  // Check GPIO status pin 23 is connected to Ground when mousetrap is "ready"
  int inReading = digitalRead(config.inPin);
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
  
  S.println("Boot number: " + String(bootCount));
  S.printf("File Timestamp is %s\n", __TIMESTAMP__);
  S.printf("Full Path is %s\n", __FILE__);
  S.printf("File Name is %s\n", sourceFile == 0L ? "null" : sourceFile);
  S.printf("Mousetrap state is %s (%d), last state was %d\n", inReading ? "Trap has Sprung": "Trap is Set", inReading, lastState);
  S.printf("scaled battery reading (before wifi) is %d, unScaled %d\n",internalBatReading, rawBatReading);
  S.printf("raw battery Reading (io34/A6) is %d, internal Reading: %d\n", ADCbatReading, internalBatReading);
  S.printf("topicBase is %s\n", topicBase.c_str());





// always try to initialize ESPNow
  WiFi.mode(WIFI_STA); // required because otherwise it aint setup!
  WiFi.setAutoConnect(false);
  if (!initESPNOWSenderTo(remoteMac)) {
    gotoSleepForSeconds(15*60,"ESPNow failed to initialize, might try again in 15 minutes");    
  }
  ESPNOWSetRetryLimit(config.espNowRetries);
  setESPNOWDefaultBaseMQTTTopic((String("/ESP/") + getShortName()).c_str());
  char const *baseTopic = getESPNOWDefaultBaseMQTTTopic();
  // tbhis will work because WiFi is not connected, so won't be to a random channel
  sendESPNOWMQTT("boot", "booted");


// optionally connect to WiFi .. 
// e.g. to retreive settings if not already retreived


  timingPointF = millis();


// turn the LED on for a second to show we're awake or blink while connecting to WiFi
digitalWrite(LED, LOW);
// if we haven't got any settings, then ALWAYS connect to WiFi and pull from RPi
  if (!settingsRetrieved ) {
    sendESPNOWMQTT("boot", "getting Settings from Pi");
    initWiFi();
    // check if WiFi is connected, otherwise, goto sleep and try again in 15 minutes!
    if (WiFi.status() != WL_CONNECTED) {
     gotoSleepForSeconds(15*60, "Wifi failed to connect, trying again in 15 minutes");
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
  }


  // if WiFi is connected , and the channel is wrong, and we *DID* manage to get the settings!
// always do the short sleep thingy if settings *ARE* retrieved
// if wifi is not connected, OR settings aren't retreived we will proceed with the built in settings
// aka ??? minutes
  if (WiFi.status() == WL_CONNECTED && settingsRetrieved ) {
//  if (WiFi.status() == WL_CONNECTED && WiFi.channel() != ESPNOW_CHANNEL && settingsRetrieved ) {
    Serial.printf("doing a FAST sleep to reinitialize ESP NOW, settingsRetrieved:%d, channel:%d\n", settingsRetrieved , WiFi.channel());

    Serial.println("deep sleeping for 1 second, expect a wakeup");
    delay(100);
    gotoSleepForSeconds(1,"FastSleep for 1 second on boot to reInitialize WiFi/ESPNow");
    Serial.println("Shouldn't reach Here!");

    
    // disconnect WiFi and deinitialize espNow
    stopESPNow();
    WiFi.setAutoConnect(false);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(1 * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
    // init espnow again
    if (!initESPNOWSenderTo(remoteMac)) {
      gotoSleepForSeconds(15*60, "ESPNow failed to re-initialize after disconnecting WiFi, trying again in 15 minutes");    
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
  gotoSleepForSeconds(config.sleepTimeMins*60, "End of Normal Boot Cycle");

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

// #include <driver/adc.h>
#include <esp_adc_cal.h>
void readCalibratedMilliVoltsOnA6() {
      #define V_REF 1100  // ADC reference voltage
    adc_atten_t attenuation =  ADC_ATTEN_0db; //ADC_ATTEN_11db;
    // Configure ADC
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_CHANNEL_6, attenuation);

    // Calculate ADC characteristics i.e. gain and offset factors
    esp_adc_cal_characteristics_t characteristics;
    esp_adc_cal_get_characteristics(V_REF, attenuation, ADC_WIDTH_12Bit, &characteristics);

    // Read ADC and obtain result in mV
    uint32_t voltage = adc1_to_voltage(ADC1_CHANNEL_6, &characteristics);
    printf("%d mV\n",voltage);


        esp_err_t status;
    status = adc2_vref_to_gpio(GPIO_NUM_25);
    if (status == ESP_OK){
        printf("v_ref routed to GPIO\n");
    }else{
        printf("failed to route v_ref\n");
    }
}


int  readBattery() {
  int internalBatReading;
  if (WiFi.status() == 255) {
      btStart();
      internalBatReading = rom_phy_get_vdd33();
      btStop();
  }
  else {
      internalBatReading = rom_phy_get_vdd33();
  }
  S.printf("Raw internal reading:%d\n", internalBatReading);
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
  S.printf("Raw internal reading:%d\n", internalBatReading);
  return internalBatReading;
}



void updateConfigFromPi() {
  // get from raspberry pi
  HTTPClient client;
  String url = String("http://192.168.11.110:1880/ESP/config?name=") + getShortName();
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
       int sleepTimeMins = root["sleepTimeMins"];
       int lingerSeconds = root["lingerSeconds"];
       uint16_t espNowRetries = root["espNowRetries"];
       gpio_num_t inPin = config.inPin;
       int inPinMode = config.inPinMode;
       if (root.containsKey("inPin")) {
          inPin = (gpio_num_t) ((int)root["inPin"]);
          S.printf("Setting \"inPin\" to %d\n", inPin);
       } else {
          S.printf("inPin left at default of %d\n", inPin);
       }
       if (root.containsKey("inPinMode")) {
          String inPinModeS = root["inPinMode"];
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

       
       S.printf("thingspeak:%d pushover:%d invalid:%d thingspeakKey:%s\n",thingspeak, pushover, invalid, thingspeakKey.c_str() );
       config.sendThingSpeak = thingspeak;
       config.sendPushover = pushover;
       config.sleepTimeMins = sleepTimeMins;
       if (config.thingspeakKey != NULL) {
         free(config.thingspeakKey);
       }
       config.thingspeakKey = strdup(thingspeakKey.c_str());
       config.thingspeakChannel = strdup(thingspeakChannel.c_str());
       config.lingerSeconds = lingerSeconds;
       config.espNowRetries = espNowRetries;
       config.inPin = inPin;
       settingsRetrieved = true;
  } 
}


int initWiFi() {
    // get WIFI going...
  
  //WiFi.setHostname(getName());
  timingPointC = millis();

  Serial.printf("Wifi Status is currently:%d", WiFi.status());

  if (WiFi.status() == WL_CONNECTED) {
    // we're already connected, just return
    return true;
  }

  
  uint32_t staticIP  = getIPAddress32bit();

  connectTimeMillis = millis();
  if (staticIP != 0){
    IPAddress ip = IPAddress(staticIP);
    Serial.print("Using a static IP address to connect\n");
    Serial.println(ip);
    WiFi.config(ip, gateway,subnet,dns, dns2); 
  }

  WiFi.begin(WIFI_SSID, WIFI_PWD);
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
    gotoSleepForSeconds(15*60, msg);
}


/** puts into Deep Sleep with appropriate wakeups.
shuts down as much as is possible.
*/
void gotoSleepForSeconds(int seconds, char const *reason) {
      // send a final ESPNow message
      sendESPNOWMQTT("debug/ShuttingDown", reason);
      
      Serial.println(reason);
      
      WiFi.setAutoConnect(false);
      WiFi.mode(WIFI_OFF);
      stopESPNow();
      esp_sleep_enable_timer_wakeup(seconds * uS_TO_S_FACTOR);
      S.println("Setup ESP32 to sleep for " + String(seconds) +  " Seconds");
      S.println("Setup ESP32 to sleep for (round tripped)" + String((long)((seconds*uS_TO_S_FACTOR)/uS_TO_S_FACTOR)) + " seconds");

      // also enable on pin High or Low (whichever it ISNT)
      int inReading = digitalRead(config.inPin);

      if (inReading == 0) {
        S.printf("Enabling wakeup on pin%d HIGH - waiting to trigger when it goes to 1, current value is stored as %d (now is %d)", config.inPin, inReading,  digitalRead(config.inPin));
        S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 1)); //1 = High, 0 = Low
      } else {
        S.printf("Enabling wakeup on pin%d LOW - waiting to trigger when it goes 0, current value is stored as %d (now is %d)", config.inPin,inReading,  digitalRead(config.inPin));
        S.println(esp_sleep_enable_ext0_wakeup(config.inPin, 0)); //1 = High, 0 = Low
      }

        unsigned long endTimeMillis = millis();
        if (endTimeMillis > startTimeMillis) { // safe reading
          lastWakeDurationMs = endTimeMillis - startTimeMillis;
        } else {
          lastWakeDurationMs = 0;
        }

      //Go to sleep now
      S.println("Going to sleep now");

      
      cumulativeWakeTimeMs += lastWakeDurationMs;
      esp_deep_sleep_start();

      
      S.println("Should never reach here!");    
  
}

