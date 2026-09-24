// --- Libraries ---
#define TINY_GSM_MODEM_SIM7600 
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <ModbusMaster.h>

// --- Pins ---
#define GSM_RX    33
#define GSM_TX    32
#define GSM_RESET 21
#define MODBUS_RX 25
#define MODBUS_TX 26
#define MODBUS_FC 22  

// --- Credentials ---
const char apn[] = "hutch3g"; 
const char server[] = "api.datacake.co";
const char token[] = "f17d10b57a12c9bd1849d50ec360d4ffba4a1157";
const char deviceID[] = "021db3eb-a86b-4d9f-8c2a-6d00e8cf7f8f";

// --- Objects ---
TinyGsm modem(Serial2);
TinyGsmClient gsmClient(modem);
HttpClient http(gsmClient, server, 80); // Port 80 for Non-Secure HTTP
ModbusMaster node; 

// --- Modbus Callbacks ---
void preTransmission() { digitalWrite(MODBUS_FC, HIGH); }
void postTransmission() { digitalWrite(MODBUS_FC, LOW); }

void setup() {
  Serial.begin(115200);
  
  // Setup Pins
  pinMode(GSM_RESET, OUTPUT);
  digitalWrite(GSM_RESET, HIGH); 
  pinMode(MODBUS_FC, OUTPUT);
  digitalWrite(MODBUS_FC, LOW);

  // Start Modbus Serial
  Serial1.begin(9600, SERIAL_8N1, MODBUS_RX, MODBUS_TX);
  node.begin(1, Serial1); 
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // Start Modem
  Serial.println("Starting modem... Please wait 10 seconds.");
  Serial2.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(10000); 

  // Connect to Network
  Serial.println("Connecting to network...");
  modem.init();
  modem.waitForNetwork(15000);
  modem.gprsConnect(apn, "", "");
  
  if (modem.isGprsConnected()) {
    Serial.println("GPRS Connected Successfully!");
  } else {
    Serial.println("GPRS Connection Failed!");
  }
}

void loop() {
  // 1. Read the Sensor
  uint8_t result = node.readInputRegisters(0x0001, 2);
  
  if (result == node.ku8MBSuccess) {
    float temperature = node.getResponseBuffer(0) / 100.0f;
    float humidity = node.getResponseBuffer(1) / 100.0f;
    
    // 2. Print to Serial Monitor
    Serial.println("-----------------------------------");
    Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
    Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");

    // 3. Send to Datacake if GPRS is connected
    if (modem.isGprsConnected()) {
      sendToDatacake(temperature, humidity);
    } else {
      Serial.println("Modem disconnected. Trying to reconnect...");
      modem.gprsConnect(apn, "", "");
    }
  } else {
    Serial.println("Failed to read from Modbus sensor!");
  }

  // Wait 60 seconds before reading and sending again
  delay(60000); 
}

// --- Upload Function ---
void sendToDatacake(float t, float h) {
  Serial.println("Uploading to Datacake...");
  
  // Format for Direct API
  String payload = "[{\"field\":\"TEMPERATURE\",\"value\":" + String(t) + "},";
  payload += "{\"field\":\"HUMIDITY\",\"value\":" + String(h) + "}]";
  
  // Direct API Path with Trailing Slash (Fixes 308 Redirect Error)
  String path = String("/v1/devices/") + deviceID + "/telemetry/";

  // Build the HTTP POST Request (Without duplicate Host header to fix 400 Error)
  http.beginRequest();
  http.post(path);
  http.sendHeader("Authorization", String("Token ") + token);
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", payload.length());
  http.beginBody();
  http.print(payload);
  http.endRequest();

  // Read Response
  int statusCode = http.responseStatusCode();
  String responseBody = http.responseBody(); 
  
  if (statusCode >= 200 && statusCode < 300) {
    Serial.printf("Success! Code: %d\n", statusCode);
  } else {
    Serial.printf("Error! Code: %d\n", statusCode);
    Serial.println("Datacake says: " + responseBody); 
  }
}