#include <Ethernet.h>
#include <EthernetUdp.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
unsigned int localPort = 8888;

// FIX 1: Using the global NTP pool (much more reliable than NIST)
const char timeServer[] = "pool.ntp.org"; 

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
EthernetUDP Udp;

void setup() {
  Serial.begin(115200); 
  delay(2000); 

  Ethernet.init(5);  // CS pin for NORVI
  Serial.println("Starting Ethernet...");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    while (true); // Stop if failed
  } 
  
  Serial.print("Ethernet connected! IP Address: ");
  Serial.println(Ethernet.localIP());
  
  Udp.begin(localPort);
}

void loop() {
  Ethernet.maintain();

  Serial.println("Requesting time...");
  sendNTPpacket(timeServer);
  
  // FIX 2: Wait up to 3 seconds for a reply, checking every 100 milliseconds
  int packetSize = 0;
  int timeoutCounter = 0;
  while (timeoutCounter < 30) {
    packetSize = Udp.parsePacket();
    if (packetSize) {
      break; // Packet received! Exit the waiting loop
    }
    delay(100);
    timeoutCounter++;
  }
  
  if (packetSize) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;
    
    // Add 19800 seconds (5 hours 30 mins) to convert UTC to Sri Lanka time!
    epoch = epoch + 19800;
    
    Serial.print("The Local Time is ");
    Serial.print((epoch  % 86400L) / 3600);
    Serial.print(':');
    if (((epoch % 3600) / 60) < 10) { Serial.print('0'); }
    Serial.print((epoch  % 3600) / 60);
    Serial.print(':');
    if ((epoch % 60) < 10) { Serial.print('0'); }
    Serial.println(epoch % 60);
    Serial.println("-------------------------");
  } else {
    Serial.println("No reply from time server. Trying again soon...");
  }

  delay(10000); // Wait 10 seconds before next request
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