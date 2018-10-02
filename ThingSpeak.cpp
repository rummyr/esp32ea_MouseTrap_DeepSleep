#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <NameHelper.h>
#include "credentials.h"

// Can increase the max packet len from 128 by setting
// #define MQTT_MAX_PACKET_SIZE to another number before this include

#define MQTT_MAX_PACKET_SIZE 256
#include <PubSubClient.h>
#include <WiFiClient.h>
#if MQTT_MAX_PACKET_SIZE < 200
#error pubsubclient needs editing for MQTT_MAX_PACKET_SIZE in \Arduino\sketchbook\libraries\PubSubClient\src
#endif


//#define S Serial

const char *thingSpeakServer = "api.thingspeak.com";
const char *thingSpeakServerMQTT = "mqtt.thingspeak.com";
const int thingSpeakServerMQTTPort = 1883;

const char *thingSpeakFingerprint = "78:60:18:44:81:35:BF:DF:77:84:D4:0A:22:0D:9B:4E:6C:DC:57:2C";
String  sendSecureThingSpeak(
  int bootCount,
  int inReading,
  int ADCbatReading,
  int internalBatReading,
  int connectTimeMillis,
  int wifiRSSI,
  int lastWakeDurationMs,
  uint32_t cumulativeWakeTimeMs) {

  String thingSpeakAPIKey = THINGSPEAK_KEY_ASH; // default
  if (getShortName() == NAME_ASH) {
    thingSpeakAPIKey = THINGSPEAK_KEY_ASH;
  } else if (getShortName() == NAME_ELM) {
    thingSpeakAPIKey = THINGSPEAK_KEY_ELM;
  } else {
    thingSpeakAPIKey = "INVALID";
  }




  const char *host = thingSpeakServer;
  int httpsPort = 443;
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  S.print("connecting to ");
  S.println(host);
  if (!client.connect(host, httpsPort)) {
    S.println("connection failed");
    return "connection failed";
  }

  /** Fingerprint verification is broken @time of writing *.
    if (client.verify(thingSpeakFingerprint, host)) {
    S.println("certificate matches");
    } else {
    S.println("certificate doesn't match");
    }
  */

  String postStr = thingSpeakAPIKey;
  postStr += "&field1=";
  postStr += String(bootCount);
  postStr += "&field2=";
  postStr += String(inReading);
  postStr += "&field3=";
  postStr += String(ADCbatReading);
  postStr += "&field4=";
  postStr += String(internalBatReading);
  postStr += "&field5=";
  postStr += String(connectTimeMillis);
  postStr += "&field6=";
  postStr += String(wifiRSSI);
  postStr += "&field7=";
  postStr += String(lastWakeDurationMs);
  postStr += "&field8=";
  postStr += String(cumulativeWakeTimeMs);
  postStr += "\r\n\r\n";
  client.print("POST /update HTTP/1.1\n");
  client.print("Host: api.thingspeak.com\n");
  client.print("Connection: close\n");
  client.print("X-THINGSPEAKAPIKEY: " + thingSpeakAPIKey + "\n");
  client.print("Content-Type: application/x-www-form-urlencoded\n");
  client.print("Content-Length: ");
  client.print(postStr.length());
  client.print("\n\n");
  client.print(postStr);
  S.println("Sent ");
  //S.print(postStr);
  S.print(" to thingSpeak");
  //print the response

  while (client.available() == 0);

  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("Status:")) {
      S.println(line);
    }
  }
  return "sent ok";
}

void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}
String  sendThingSpeakMQTT(
  char const *channelId,
  char const *apiKey,
  int bootCount,
  int inReading,
  int ADCbatReading,
  int internalBatReading,
  int connectTimeMillis,
  int wifiRSSI,
  int lastWakeDurationMs,
  uint32_t cumulativeWakeTimeMs) {

  WiFiClient espClient;
  PubSubClient client = PubSubClient(thingSpeakServerMQTT, thingSpeakServerMQTTPort, MQTTCallback, espClient);
  // for thingspeak clientId is "random" aka shortName, username is anything, pass is specific to My Thingspeak Acount

  // trying to vary the clientId ("random"?)
  const char *clientId = strdup((String(getShortName()) + millis()).c_str());
  client.connect(
    clientId //id
    , THINGSPEAK_MQTT_USR // user
    , THINGSPEAK_MQTT_KEY // pass
    // willTopic
    // willQos
    // boolean willRetain
    // willMessagae
  );
  Serial.printf("Client is connected as %s:%d\n", clientId, client.connected());

  String thingSpeakAPIKey = apiKey; // default

  if (getShortName() == NAME_ASH) {
    thingSpeakAPIKey = THINGSPEAK_KEY_ASH;
  } else if (getShortName() == NAME_ELM) {
    thingSpeakAPIKey = THINGSPEAK_KEY_ELM;
  } else {
    thingSpeakAPIKey = "INVALID";
  }


  String topic = String("channels/") + channelId + "/publish/" + thingSpeakAPIKey;
  String data1 = String("")
                 + "field1=" + bootCount
                 + "&field2=" + inReading
                 + "&field3=" + ADCbatReading
                 + "&field4=" + internalBatReading;
  String data2 = String("")
                 + "field5=" + connectTimeMillis
                 + "&field6=" + wifiRSSI
                 + "&field7=" + lastWakeDurationMs
                 + "&field8=" + cumulativeWakeTimeMs ;

  String data = data1 + "&" + data2;


  if (1) {
    boolean rv = client.publish(topic.c_str(), data.c_str());
    Serial.printf("Sending topic:%s data1:%s\n", topic.c_str(), data.c_str());
    Serial.printf("Total Len as used inside client:%d (len:%d %d)\n",
                  (5 + 2 + topic.length() + data.length()), topic.length(), data.length());
    Serial.printf("result of publish (all) was %d (connected is %d)\n", rv, client.connected());

  } else  { //2 packets, second seems to be often lost!
    boolean rv = client.publish(topic.c_str(), data1.c_str());
    Serial.printf("Sending topic:%s data1:%s\n", topic.c_str(), data1.c_str());
    Serial.printf("Total Len as used inside client:%d (len:%d %d)\n",
                  (5 + 2 + topic.length() + data1.length()), topic.length(), data1.length());
    Serial.printf("result of publish 1 was %d (connected is %d)\n", rv, client.connected());

    rv = client.publish(topic.c_str(), data2.c_str());
    Serial.printf("Sending topic:%s data2:%s\n", topic.c_str(), data2.c_str());
    Serial.printf("Total Len as used inside client:%d (len:%d %d)\n",
                  (5 + 2 + topic.length() + data2.length()), topic.length(), data2.length());
    Serial.printf("result of publish 2 was %d (connected is %d)\n", rv, client.connected());
  }

  client.loop();
  client.disconnect();
  // dont disconnect the client client.disconnect();


  return "sent ok";
}
