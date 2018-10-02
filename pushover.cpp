#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
//#include "dbg.h"
#include "credentials.h"


const char *pushoverAppKey=PUSHOVER_APP_KEY; // esp applications
const char *pushoverUserKey = PUSHOVER_USER_KEY;
const char *pushoverServer = "api.pushover.net"; // https
const String pushoverURL = "/1/messages.json";
const char *pushoverFingerprint =  "1E:D5:B7:68:BB:25:AD:A3:E0:96:78:A4:68:48:08:4F:07:E4:8D:AB";
const int timeout = 15*1000; // 15 sec timeout waiting for the response?


String sendSecurePushover(const char * title, const char * msg) {



  S.printf("Sending title:%s msg:%sto pushover",title,msg);


    
  const char *host = pushoverServer;
  int httpsPort = 443;
    // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  S.print("connecting to ");
  S.println(host);
  if (!client.connect(host, httpsPort)) {
    S.println("connection failed");
    return "connection failed";
  } 


//ESP32 has no verify!
//#if (ARCH==EPS32)
//    if (client.verify(pushoverFingerprint, host)) {
//    S.println("certificate matches");
//  } else {
//    S.println("certificate doesn't match");
//  }       
//#endif


  String postStr = "";
      postStr += "token=" ;
      postStr += pushoverAppKey;
      postStr += "&user="; postStr += pushoverUserKey;
      postStr += "&title="; postStr += title;
      postStr += "&message="; postStr += msg; postStr += "\n";
      // optional postStr += "&device="; postStr +=;
      // optional postStr += "&url="; postStr +=;
      // optional postStr += "&url_title="; postStr +=;
      // optional postStr += "&priority="; postStr +=;
      // optional postStr += "&retry="; postStr +=;
      // optional postStr += "&expire="; postStr +=;
      // optional postStr += "&sound="; postStr +=;
      // we automatically add this? postStr += "\r\n\r\n";
      client.print("POST " + pushoverURL + " HTTP/1.1\n");
      client.print("Host: "); client.print(host); client.print("\n");
      client.print("Connection: close\n");
      client.print("Content-Type: application/x-www-form-urlencoded\n");
      client.print("Content-Length: ");
      client.print(postStr.length());
      client.print("\n\n");
      client.print(postStr);
      client.print("\r\n\r\n");
      S.println("Sent "); S.print(postStr); S.print(" to pushover");
    //print the response


  // perform timeout code..
  int timeout_at = millis() + timeout;
  S.println("Now is " + String(millis()) + " timeout at " + String(timeout_at));
  while (!client.available() && (timeout_at - millis() > 0)) {
    delay(100); // busy waiting
    S.print("+");
  }

  S.println("Client avaliable is " + String(client.available()) + "time delta is " + String(timeout_at - millis()));
  if (!client.available())   {
    S.println("Pushover response not available");
    client.stop();
    return "no response";
  }

  S.println("Pushover response available:");

  while (client.available()) {
    String line = client.readStringUntil('\n');
    S.println(line);
  } 
   S.println("Pushover response ends");
  client.stop();
  return "sent ok";
}
