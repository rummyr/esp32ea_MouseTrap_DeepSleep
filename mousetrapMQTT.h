#include <PubSubClient.h>
#include <WiFiClient.h>
PubSubClient *mqtt_pubSubClient_init(WiFiClient espClient, char const *name);
PubSubClient *mqtt_pubSubClient_init(char const *name);
