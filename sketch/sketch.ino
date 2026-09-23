#include <Ethernet.h>
#include <EthernetUdp.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
unsigned int localPort = 8888;
const char timeServer[] = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
EthernetUDP Udp;

void setup() {
  Serial.begin(115200); // Changed to 115200 for faster ESP32 printing
  delay(2000); // Wait for Serial monitor to open

  Ethernet.init(5);  // CS pin for NORVI Ethernet
  Serial.println("Starting Ethernet...");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.");
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    while (true); // Stop here if Ethernet fails
  } 
  
  Serial.print("Ethernet connected! IP Address: ");
  Serial.println(Ethernet.localIP());
  
  Udp.begin(localPort);
}

void loop() {
  // 1. Keep the DHCP lease alive
  Ethernet.maintain();

  // 2. Request the time
  Serial.println("Requesting time...");
  sendNTPpacket(timeServer);
  
  // 3. Wait up to 1 second for a reply
  delay(1000);
  
  // 4. Process the reply if received
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;
    
    Serial.print("The UTC time is ");
    Serial.print((epoch  % 86400L) / 3600);
    Serial.print(':');
    if (((epoch % 3600) / 60) < 10) {
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60);
    Serial.print(':');
    if ((epoch % 60) < 10) {
      Serial.print('0');
    }
    Serial.println(epoch % 60);
    Serial.println("-------------------------");
  } else {
    Serial.println("No reply from time server.");
  }

  // 5. Wait 10 seconds before asking for the time again
  delay(10000); 
}

void sendNTPpacket(const char * address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}