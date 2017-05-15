#include <sha256.h>
#include <ESP8266WiFi.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <EEPROM.h>

#define PIN1 3 // RX
#define PIN2 2 // GPIO2

#define PIN3 1 // TX
#define PIN4 0 // meh

#define SHA256HMAC_SIZE 32
String hmacKey = "something random";
bool shouldSaveConfig = false;
int hmacWindow = 30;

long buttons[2] = {0, 0};

WiFiUDP ntpUDP;
WiFiServer server(8023);
WiFiManager wifiManager;
NTPClient timeClient(ntpUDP);

void saveKey() {
  int len = hmacKey.length();
  EEPROM.write(0, len);
  for (int i = 0; i < len; i ++) {
    EEPROM.write(i + 1, hmacKey[i]);
  }
  EEPROM.commit();
}

void loadKey() {
  int len = EEPROM.read(0);
  if (len > 129) {
    return;
  }
  hmacKey = "";

  for (int i = 0; i < len; i++) {
    hmacKey += char(EEPROM.read(i + 1));
  }
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void setup() {
  EEPROM.begin(129);
  delay(500);
  loadKey();
  pinMode(PIN1, OUTPUT);
  pinMode(PIN2, OUTPUT);
  pinMode(PIN3, OUTPUT);
  pinMode(PIN4, OUTPUT);

  digitalWrite(PIN1, LOW);
  digitalWrite(PIN2, LOW);
  digitalWrite(PIN3, LOW);
  digitalWrite(PIN4, LOW);

  WiFiManagerParameter custom_hmac_key("hmac", "hmac key", hmacKey.c_str(), 128);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_hmac_key);

  if (!wifiManager.autoConnect("gates")) {
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  hmacKey = String(custom_hmac_key.getValue());

  if (shouldSaveConfig) {
    saveKey();
  }

  server.begin();
  server.setNoDelay(true);

  timeClient.begin();
}

void loop() {
  timeClient.update();
  WiFiClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        // int switch
        // int seconds
        // hmac

        uint8_t *hash;
        Sha256.initHmac((uint8_t*)hmacKey.c_str(), hmacKey.length()); // key, and length of key in bytes

        byte btn = client.read();
        Sha256.write(btn);
        if (btn < 0 || (btn > 2 && btn != 99)) {
          client.write(0x04);
          client.stop();
          continue;
        }

        unsigned long seconds = 0;
        for (byte i = 0; i < 4; i++ ) {
          byte b = client.read();
          Sha256.write(b);
          seconds += b << (8 * (3 - i));
        }

        byte code[SHA256HMAC_SIZE];
        client.readBytes(code, SHA256HMAC_SIZE);

        unsigned long epoch = timeClient.getEpochTime();
        if (!((seconds < epoch + hmacWindow) && (seconds > epoch - hmacWindow))) {
          client.write(0x03);
          client.stop();
          continue;
        }

        hash = Sha256.resultHmac();
        bool pass = true;
        for (int i = 0; i < SHA256HMAC_SIZE; i++) {
          pass = pass && (hash[i] == code[i]);
          if (!pass) {
            break;
          }
        }
        if (!pass) {
          client.write(0x02);
          client.stop();
          continue;
        }

        if (btn == 99) {
          wifiManager.resetSettings();
          client.write(99);
          client.stop();
          delay(3000);
          ESP.reset();
          delay(5000);
          continue;
        }

        // TODO don't block
        if (btn == 1) {
          digitalWrite(PIN1, HIGH);
          delay(150);
          digitalWrite(PIN1, LOW);
        } else if (btn == 2) {
          digitalWrite(PIN2, HIGH);
          delay(150);
          digitalWrite(PIN2, LOW);
        }

        client.write(0x01);
        client.stop();
        break;
      }
    }
  }

}
