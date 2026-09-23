#define INPUT1 34 

void setup() { 
   Serial.begin(9600); 
   Serial.println("Device Starting"); 
   pinMode(INPUT1, INPUT); 
} 

void loop() { 
   Serial.print(digitalRead(INPUT1));
   Serial.println(""); 
   delay(500); 
}