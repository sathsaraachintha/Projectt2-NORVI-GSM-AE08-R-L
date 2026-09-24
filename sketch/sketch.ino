#include <SPI.h>
#include <Wire.h>
#include <Ethernet.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include "SD.h"
#include "RTClib.h"  // <-- New RTC Library

// --- Pin Definitions ---
// SD & Ethernet (Shared SPI)
#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SD_CS    15
#define ETH_CS   5

// OLED Display & RTC (Shared I2C)
#define I2C_SDA  16
#define I2C_SCL  17
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Cellular Modem
#define GSM_RX    33
#define GSM_TX    32
#define GSM_RESET 21

// Test Inputs/Outputs
#define TEST_INPUT_PIN 34 // Change to your NORVI input pin
#define BLINK_LED_PIN  2  // Status LED

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;           // <-- RTC Object
byte mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}; 

// --- Status Variables ---
String sdStatus  = "Checking...";
String ethStatus = "Checking...";
String simStatus = "Checking...";
bool rtcFound = false;
bool ledState = false;
unsigned long previousMillis = 0;
const long blinkInterval = 500; // Screen and LED update twice a second

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Initialize Inputs/Outputs
  pinMode(TEST_INPUT_PIN, INPUT);
  pinMode(BLINK_LED_PIN, OUTPUT);
  pinMode(GSM_RESET, OUTPUT);
  digitalWrite(GSM_RESET, HIGH); 

  // 2. Initialize Shared I2C (OLED & RTC)
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Start OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
  } else {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("System Booting...");
    display.display();
  }

  // Start RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  } else {
    rtcFound = true;
    // If the RTC lost power/battery, set it to the time the code was compiled
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting time...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  // 3. Initialize Shared SPI Bus
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // 4. Initialize SD Card
  Serial.print("Mounting SD... ");
  if (!SD.begin(SD_CS)) {
    sdStatus = "FAIL";
    Serial.println("Failed.");
  } else {
    int cardSize = SD.cardSize() / (1024 * 1024);
    sdStatus = String(cardSize) + "MB OK";
    Serial.println("Success.");
  }
  updateScreen();

  // 5. Initialize Ethernet
  Serial.print("Starting ETH... ");
  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac) == 0) {
    ethStatus = "No DHCP";
    Serial.println("Failed.");
  } else {
    ethStatus = Ethernet.localIP().toString();
    Serial.println(ethStatus);
  }
  updateScreen();

  // 6. Initialize Modem
  Serial.println("Starting Modem (Wait 10s)...");
  display.setCursor(0, 40);
  display.print("SIM: Booting...");
  display.display();
  
  Serial2.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(10000); 
  
  Serial2.println("AT+CPIN?");
  delay(1000);
  String modemReply = "";
  while (Serial2.available()) {
    modemReply += (char)Serial2.read();
  }
  
  if (modemReply.indexOf("READY") != -1) {
    simStatus = "READY";
  } else {
    simStatus = "ERROR/NO SIM";
  }
  updateScreen();
}

void loop() {
  unsigned long currentMillis = millis();

  // Update screen, time, inputs, and LED every 500ms
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(BLINK_LED_PIN, ledState);
    updateScreen(); 
  }

  Ethernet.maintain();

  // Modem Passthrough
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}

// --- Helper Function to redraw the Dashboard ---
void updateScreen() {
  display.clearDisplay();
  
  // 1. Top Row: Live RTC Time
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (rtcFound) {
    DateTime now = rtc.now();
    char timeStr[20];
    // Formats time as "TIME: HH:MM:SS" with leading zeros
    sprintf(timeStr, "TIME: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
    display.print(timeStr);
  } else {
    display.print("TIME: RTC ERROR");
  }

  // 2. Middle Rows: Hardware Status
  display.setCursor(0, 15);
  display.print("SD : "); 
  display.println(sdStatus);
  
  display.setCursor(0, 26);
  display.print("ETH: "); 
  display.println(ethStatus);
  
  display.setCursor(0, 37);
  display.print("SIM: "); 
  display.println(simStatus);

  // 3. Bottom Row: Inputs & Heartbeat
  int inputState = digitalRead(TEST_INPUT_PIN);
  display.setCursor(0, 52);
  display.print("IN34: ");
  display.print(inputState == HIGH ? "HIGH" : "LOW ");
  
  // Blinking asterisk shows the ESP32 is running without freezing
  display.print("      RUN ");
  if (ledState) {
    display.print("*");
  }
  
  display.display();
}