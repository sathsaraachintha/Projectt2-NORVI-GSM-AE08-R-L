#include <SPI.h>
#include <Wire.h>
#include <Ethernet.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include "SD.h"

// --- Pin Definitions ---
// SD & Ethernet (Shared SPI)
#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SD_CS    15
#define ETH_CS   5

// OLED Display (I2C)
#define I2C_SDA  16
#define I2C_SCL  17
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Cellular Modem
#define GSM_RX    33
#define GSM_TX    32
#define GSM_RESET 21

// Test Inputs/Outputs (Change these to your specific NORVI input pins)
#define TEST_INPUT_PIN 34 // Example digital input
#define BLINK_LED_PIN  2  // Example status LED

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
byte mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}; // Realistic MAC to bypass router blocks

// --- Status Variables ---
String sdStatus  = "Checking...";
String ethStatus = "Checking...";
String simStatus = "Checking...";
bool ledState = false;
unsigned long previousMillis = 0;
const long blinkInterval = 500; // Blink every 500ms

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Initialize Inputs/Outputs
  pinMode(TEST_INPUT_PIN, INPUT);
  pinMode(BLINK_LED_PIN, OUTPUT);
  pinMode(GSM_RESET, OUTPUT);
  digitalWrite(GSM_RESET, HIGH); // Power up modem

  // 2. Initialize OLED Display
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
  } else {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("NORVI System Boot...");
    display.display();
  }

  // 3. Initialize Shared SPI Bus
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // 4. Initialize SD Card
  Serial.print("Mounting SD Card... ");
  if (!SD.begin(SD_CS)) {
    sdStatus = "FAIL / Not Found";
    Serial.println("Failed.");
  } else {
    int cardSize = SD.cardSize() / (1024 * 1024);
    sdStatus = "OK (" + String(cardSize) + "MB)";
    Serial.println("Success.");
  }
  updateScreen();

  // 5. Initialize Ethernet
  Serial.print("Starting Ethernet... ");
  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac) == 0) {
    ethStatus = "FAIL / No DHCP";
    Serial.println("Failed.");
  } else {
    ethStatus = "IP: " + Ethernet.localIP().toString();
    Serial.println(ethStatus);
  }
  updateScreen();

  // 6. Initialize Cellular Modem
  Serial.println("Starting Modem (Wait 10s)...");
  display.setCursor(0, 40);
  display.print("Modem: Booting...");
  display.display();
  
  Serial2.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(10000); // Give the modem time to register on the network
  
  Serial2.println("AT+CPIN?");
  delay(1000);
  String modemReply = "";
  while (Serial2.available()) {
    modemReply += (char)Serial2.read();
  }
  
  if (modemReply.indexOf("READY") != -1) {
    simStatus = "READY (SIM OK)";
    Serial.println("Modem SIM Ready.");
  } else if (modemReply.indexOf("ERROR") != -1) {
    simStatus = "ERROR (Check SIM)";
    Serial.println("Modem SIM Error.");
  } else {
    simStatus = "NO RESPONSE";
    Serial.println("Modem Not Responding.");
  }
  
  updateScreen();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Non-Blocking Blink & Input Read (Happens every 500ms)
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    
    // Toggle the LED
    ledState = !ledState;
    digitalWrite(BLINK_LED_PIN, ledState);
    
    // Refresh the dashboard with the latest input states
    updateScreen(); 
  }

  // 2. Maintain Ethernet DHCP Lease
  Ethernet.maintain();

  // 3. Serial Passthrough for Modem (Allows you to type AT commands manually)
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}

// --- Helper Function to redraw the OLED Dashboard ---
void updateScreen() {
  display.clearDisplay();
  
  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- NORVI STATUS ---");

  // Subsystems
  display.setCursor(0, 15);
  display.print("SD : "); 
  display.println(sdStatus);
  
  display.setCursor(0, 25);
  display.print("ETH: "); 
  display.println(ethStatus);
  
  display.setCursor(0, 35);
  display.print("SIM: "); 
  display.println(simStatus);

  // Live Input & Blink Indicator
  int inputState = digitalRead(TEST_INPUT_PIN);
  
  display.setCursor(0, 50);
  display.print("IN34: ");
  display.print(inputState == HIGH ? "HIGH" : "LOW ");
  
  // Visual blinking cursor on the screen to prove it hasn't frozen
  display.print("      RUN ");
  if (ledState) {
    display.print("*");
  }
  
  display.display();
}