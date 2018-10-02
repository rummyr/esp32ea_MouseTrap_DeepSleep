#include "mousetrapMQTT.h"
#include "credentials.h"

WiFiClient espClient;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

PubSubClient *mqtt_pubSubClient_init(WiFiClient _espClient, char const *name){
  espClient = _espClient;
  return mqtt_pubSubClient_init(name);
  
}
PubSubClient *mqtt_pubSubClient_init( char const *name) {
  Serial.println("creating client"); //delay(1000);
  PubSubClient *client = new PubSubClient("192.168.11.110", 1883,callback,espClient);
  Serial.printf("Pi connecting as %s\n", name); //delay(1000);
  int c = client->connect(name,MQTT_USR, MQTT_PWD);
  Serial.printf("Pi connected called, state is %d\n", client->state()); //delay(1000);
  //Serial.printf("Pi connected called, state is %d\n", client->state()); //delay(1000);
  if (!c) {
    Serial.printf("Failed to connect %d!",c);
    return NULL;
  } else {
    Serial.println("Connected");
    return client;
  }
}

// https://uk.mathworks.com/help/thingspeak/publishtoachannelfieldfeed.html
void publishThingSpeakField(PubSubClient &client, String &channelId, String &apiKey, int fieldNum, const char*value) {
  // channels/<channelID>/publish/fields/field<fieldnumber>/<apikey>
  String topic = "/channels/" + channelId + "/publish/fields/field" + fieldNum + "/" + apiKey;
  client.publish(topic.c_str(), value);
}

void publishThingSpeakField(PubSubClient &client, String &channelId, String &apiKey, int fieldNum, int value) {
   publishThingSpeakField(client, channelId, apiKey, fieldNum, String(value).c_str());
}
void publishThingSpeakField(PubSubClient &client, String &channelId, String &apiKey, int fieldNum, uint32_t value) {
     publishThingSpeakField(client, channelId, apiKey, fieldNum, String(value).c_str());
}


void publishThingSpeakData(int count, ...) {
  va_list ap;
  va_start(ap, count); //Requires the last fixed parameter (to get the address)
  int j;
  for(j=0; j<count; j++)
    va_arg(ap, double); //Requires the type to cast to. Increments ap to the next argument.
  va_end(ap);
}
/*JsonObject getMyESPConfig() {
  String configTopic = "/ESP/" + getShortName() + "/Config";
  client.subscribe(configTopic);
  // JsonObject& root = jsonBuffer.parseObject((char *)payload);
  return null;
}
*/



