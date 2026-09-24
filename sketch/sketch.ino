#include <SPI.h>
#include <Wire.h>
#include <Ethernet.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include "SD.h"
#include "RTClib.h"
#include <ModbusMaster.h>

// --- TinyGSM Definitions ---
// Use the SIM7600 driver for SIMCom SIM7500/SIM7600 modules
#define TINY_GSM_MODEM_SIM7600 
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

// --- Pin Definitions (NORVI GSM-AE08-R-L) ---
#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SD_CS    15
#define ETH_CS   5
#define I2C_SDA  16
#define I2C_SCL  17
#define GSM_RX    33
#define GSM_TX    32
#define GSM_RESET 21
#define MODBUS_RX 25
#define MODBUS_TX 26
#define MODBUS_FC 22  
#define RELAY_PIN 12  
#define TEST_INPUT_PIN 34 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// --- Network Credentials ---
const char apn[]  = "hutch3g"; 
const char user[] = "";
const char pass[] = "";

// Datacake Credentials
const char server[] = "api.datacake.co";
const char token[]  = "f17d10b57a12c9bd1849d50ec360d4ffba4a1157";
const char deviceID[] = "021db3eb-a86b-4d9f-8c2a-6d00e8cf7f8f";

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
ModbusMaster node; 
byte mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}; 

// TinyGSM & HTTP Objects
TinyGsm modem(Serial2);
TinyGsmClient gsmClient(modem);
HttpClient http(gsmClient, server, 80);

// --- System Variables ---
String ethStatus = "Wait";
String simStatus = "Connecting...";
bool rtcFound = false;
float temperature = 0.0;
float humidity = 0.0;
bool relayState = false;

unsigned long previousBlink = 0;
unsigned long previousModbus = 0;
unsigned long previousDatacake = 0;

void preTransmission() { digitalWrite(MODBUS_FC, HIGH); }
void postTransmission() { digitalWrite(MODBUS_FC, LOW); }

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  pinMode(GSM_RESET, OUTPUT);
  digitalWrite(GSM_RESET, HIGH); 
  pinMode(MODBUS_FC, OUTPUT);
  digitalWrite(MODBUS_FC, LOW);

  // Initialize OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  // Initialize RTC
  if (rtc.begin()) {
    rtcFound = true;
    if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize SPI & SD
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SD.begin(SD_CS); 
  
  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac) != 0) ethStatus = "IP OK";

  // Initialize RS485
  Serial1.begin(9600, SERIAL_8N1, MODBUS_RX, MODBUS_TX);
  node.begin(1, Serial1); 
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // Initialize Modem via TinyGSM
  Serial.println("Initializing modem... (Waiting 10 seconds for boot)");
  Serial2.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(10000); // Give the SIM7500 10 seconds to fully wake up

  if (!modem.init()) { 
    Serial.println("Modem init failed!");
    simStatus = "MODEM FAIL";
  } else {
    Serial.println("Connecting to cellular network...");
    if (!modem.waitForNetwork(15000)) {
      simStatus = "NO SIGNAL";
    } else {
      Serial.println("Connecting to APN: " + String(apn));
      if (!modem.gprsConnect(apn, user, pass)) {
        simStatus = "APN FAIL";
      } else {
        simStatus = "ONLINE";
      }
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Refresh OLED every 500ms
  if (currentMillis - previousBlink >= 500) {
    previousBlink = currentMillis;
    updateScreen(); 
  }

  // 2. Read XY-MD02 via Modbus every 2 seconds
  if (currentMillis - previousModbus >= 2000) {
    previousModbus = currentMillis;
    readXYMD02();
    
    // CONTROL LOGIC
    if (temperature > 30.0) {
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
    } else {
      relayState = false;
      digitalWrite(RELAY_PIN, LOW);
    }
  }

  // 3. Send Data to Datacake via TinyGSM every 60 seconds
  if (currentMillis - previousDatacake >= 60000) {
    previousDatacake = currentMillis;
    if (temperature != 0.0 && modem.isGprsConnected()) {
      sendToDatacake(temperature, humidity);
    } else if (!modem.isGprsConnected()) {
      Serial.println("GPRS Disconnected! Attempting reconnect...");
      modem.gprsConnect(apn, user, pass);
    }
  }

  Ethernet.maintain();
}

// --- Read Modbus Sensor ---
void readXYMD02() {
  uint8_t result = node.readInputRegisters(0x0001, 2);
  if (result == node.ku8MBSuccess) {
    temperature = node.getResponseBuffer(0) / 10.0f;
    humidity = node.getResponseBuffer(1) / 10.0f;
  }
}

// --- Upload to Datacake using TinyGSM & ArduinoHttpClient ---
void sendToDatacake(float t, float h) {
  Serial.println("Uploading to Datacake...");
  
  String payload = "[{\"field\":\"TEMPERATURE\",\"value\":" + String(t) + "},";
  payload += "{\"field\":\"HUMIDITY\",\"value\":" + String(h) + "}]";
  
  // Print exactly what we are sending to check for typos!
  Serial.println("Sending Payload: " + payload);
  
  String path = String("/v1/devices/") + deviceID + "/telemetry";

  // Standard HTTP POST using the ArduinoHttpClient
  http.beginRequest();
  http.post(path);
  http.sendHeader("Host", server);
  http.sendHeader("Authorization", String("Token ") + String(token));
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", payload.length());
  http.beginBody();
  http.print(payload);
  http.endRequest();

  // Check the response from Datacake
  int statusCode = http.responseStatusCode();
  String responseBody = http.responseBody(); // Grab the server's explanation
  
  if (statusCode >= 200 && statusCode < 300) {
    Serial.printf("Success! Server replied with code: %d\n", statusCode);
  } else {
    Serial.printf("Failed to connect. Error code: %d\n", statusCode);
    Serial.println("Datacake says: " + responseBody); // Print why it failed!
  }
}

// --- Refresh OLED Dashboard ---
void updateScreen() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (rtcFound) {
    DateTime now = rtc.now();
    char timeStr[20];
    sprintf(timeStr, "TIME: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
    display.print(timeStr);
  }

  display.setCursor(0, 15);
  display.print("TEMP : "); 
  display.print(temperature, 1);
  display.println(" C");
  
  display.setCursor(0, 27);
  display.print("HUM  : "); 
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(0, 42);
  display.print("RELAY: ");
  if (relayState) {
    display.setTextColor(BLACK, WHITE); 
    display.print(" ON (ALARM) ");
    display.setTextColor(WHITE, BLACK);
  } else {
    display.print(" OFF ");
  }

  display.setCursor(0, 55);
  display.print("SIM: ");
  display.print(simStatus);
  
  display.display();
}
