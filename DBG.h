#ifndef DBG_H
#define DBG_H
//#warning DBG.h is being included

#include <WiFi.h>
#include <WiFiUdp.h>

// prototypes for the debug classr
class DBG : public Stream {
  public:
    static void begin(int) {
      Serial.println("DBG::begin called");
      udp.begin(8266);
      sendToIP =  IPAddress(192,168,11,200);
      beginPacket();
    }
    static void beginPacket() {
        udp.beginPacket(sendToIP, sendToPort);  
    }

//    static void print(const String &);
//    static void print(const char* );
//    static void println(const char*);
//    static void println(const String &);
//    static void print(double);
//    static void print(unsigned int);
//    static void print(int);
//    static void println();
//    static void printf(char*a, const int&);
//    static void printf(char*a, const char*);

    size_t write(uint8_t c) {
      size_t rv = udp.write(c);
      Serial.printf("%c",c);
      if (c=='\n') {
        flush();
      }
      return rv;
    }

    void flush() {
      // debug on the debug! Serial.printf("DBG.flush to %s:%d\n", sendToIP.toString().c_str(), sendToPort);
      udp.endPacket();
      beginPacket();
    }

    int peek() {
       return udp.peek();
    }

    int read() {
      return udp.read();
    }

    int available() {
      return udp.available();
    }

// HACK, leave public  private:
    static WiFiUDP udp;

    static  IPAddress sendToIP;
    static const int sendToPort = 6666; // netconsole port?




};


extern DBG DBGUDPStream;
#endif

