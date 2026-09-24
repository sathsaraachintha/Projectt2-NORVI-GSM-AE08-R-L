// NORVI GSM-AE08-R-L -> Modbus temp/humidity -> Datacake HTTP Payload Decoder
// HTTPS is done by the modem itself (AT+HTTP*), no TinyGsmClientSecure needed.

#define TINY_GSM_MODEM_SIM7600
#include <TinyGsmClient.h>
#include <ModbusMaster.h>

// --- Pins ---
#define GSM_RX    33
#define GSM_TX    32
#define GSM_RESET 21
#define MODBUS_RX 25
#define MODBUS_TX 26
#define MODBUS_FC 22

// --- Settings ---
const char apn[] = "hutch3g";
const char datacakeUrl[]  = "https://api.datacake.co/integrations/api/f527a000-c64b-4ea2-b69b-34818e69866a/";
const char deviceSerial[] = "YOUR_DEVICE_SERIAL";   // <-- Serial Number of the device in Datacake
const unsigned long SEND_INTERVAL_MS = 60000;

TinyGsm modem(Serial2);
ModbusMaster node;

void preTransmission()  { digitalWrite(MODBUS_FC, HIGH); }
void postTransmission() { digitalWrite(MODBUS_FC, LOW); }

// --- Raw AT helper: send cmd, wait until `expect` appears (or timeout), return everything received ---
String at(const String &cmd, const char *expect, uint32_t timeoutMs) {
  while (Serial2.available()) Serial2.read();   // flush old data
  if (cmd.length()) Serial2.println(cmd);
  String resp = "";
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial2.available()) resp += (char)Serial2.read();
    if (resp.indexOf(expect) >= 0) {
      delay(50);
      while (Serial2.available()) resp += (char)Serial2.read();
      break;
    }
    if (resp.indexOf("ERROR") >= 0) break;
    delay(5);
  }
  return resp;
}

bool connectNetwork() {
  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork(60000L)) { Serial.println("Network registration failed"); return false; }
  Serial.println("Connecting GPRS...");
  if (!modem.gprsConnect(apn, "", "")) { Serial.println("GPRS connection failed"); return false; }
  Serial.println("GPRS connected");
  return true;
}

void sendToDatacake(float t, float h) {
  Serial.println("Uploading to Datacake...");

  String payload = "{\"device\":\"" + String(deviceSerial) + "\",";
  payload += "\"temperature\":" + String(t, 2) + ",";
  payload += "\"humidity\":" + String(h, 2) + "}";

  at("AT+HTTPTERM", "OK", 2000);                         // clean up any old session
  String r = at("AT+HTTPINIT", "OK", 5000);
  if (r.indexOf("OK") < 0) { Serial.println("HTTPINIT failed: " + r); return; }

  at("AT+HTTPPARA=\"URL\",\"" + String(datacakeUrl) + "\"", "OK", 5000);
  at("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK", 3000);

  r = at("AT+HTTPDATA=" + String(payload.length()) + ",10000", "DOWNLOAD", 5000);
  if (r.indexOf("DOWNLOAD") < 0) {
    Serial.println("HTTPDATA failed: " + r);
    at("AT+HTTPTERM", "OK", 2000);
    return;
  }
  Serial2.print(payload);
  at("", "OK", 5000);                                    // wait for OK after data

  r = at("AT+HTTPACTION=1", "+HTTPACTION:", 60000);      // 1 = POST
  Serial.println("Modem: " + r);

  int idx = r.indexOf("+HTTPACTION:");
  int status = -1, len = 0;
  if (idx >= 0) {
    int c1 = r.indexOf(',', idx);
    int c2 = r.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > 0) {
      status = r.substring(c1 + 1, c2).toInt();
      len = r.substring(c2 + 1).toInt();
    }
  }

  if (status >= 200 && status < 300) {
    Serial.printf("Success! Code: %d\n", status);
  } else {
    Serial.printf("Error! Code: %d\n", status);
    if (len > 0) Serial.println("Response: " + at("AT+HTTPREAD=0," + String(len), "OK", 5000));
  }

  at("AT+HTTPTERM", "OK", 2000);
}

void setup() {
  Serial.begin(115200);

  pinMode(GSM_RESET, OUTPUT);
  digitalWrite(GSM_RESET, HIGH);
  pinMode(MODBUS_FC, OUTPUT);
  digitalWrite(MODBUS_FC, LOW);

  Serial1.begin(9600, SERIAL_8N1, MODBUS_RX, MODBUS_TX);
  node.begin(1, Serial1);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Starting modem... please wait 10 seconds.");
  Serial2.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(10000);

  if (!modem.init()) { Serial.println("modem.init() failed, restarting..."); modem.restart(); }
  Serial.println("Modem: " + modem.getModemInfo());

  connectNetwork();
}

void loop() {
  uint8_t result = node.readInputRegisters(0x0001, 2);

  if (result == node.ku8MBSuccess) {
    float temperature = node.getResponseBuffer(0) / 100.0f;
    float humidity    = node.getResponseBuffer(1) / 100.0f;

    Serial.println("-----------------------------------");
    Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
    Serial.print("Humidity: ");    Serial.print(humidity);    Serial.println(" %");

    if (!modem.isGprsConnected()) {
      Serial.println("Modem disconnected. Reconnecting...");
      connectNetwork();
    }
    if (modem.isGprsConnected()) sendToDatacake(temperature, humidity);
  } else {
    Serial.printf("Failed to read Modbus sensor! Error code: 0x%02X\n", result);
  }

  delay(SEND_INTERVAL_MS);
}