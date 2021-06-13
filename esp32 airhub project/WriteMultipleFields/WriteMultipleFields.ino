#include <WiFi.h>
#include "DHT.h"
#include "MQ135.h"
#include "secrets.h"
#include "ThingSpeak.h" // always include thingspeak header file after other header files and custom macros

#define PIN_MQ135 33
const int DHTPIN = 5;      
const int DHTTYPE = DHT11;  
char ssid[] = SECRET_SSID;   // your network SSID (name) 
char pass[] = SECRET_PASS;   // your network password
//int keyIndex = 0;            // your network key Index number (needed only for WEP)
int counter = 0;

MQ135 mq135_sensor = MQ135(PIN_MQ135);
WiFiClient  client;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

// Initialize our values
String myStatus = "";
DHT dht(DHTPIN, DHTTYPE);
void setup() {
Serial.begin(115200);  //Initialize serial
dht.begin();       
  
WiFi.mode(WIFI_STA);   
ThingSpeak.begin(client);  // Initialize ThingSpeak
}

void loop() {
delay(1000);
  // Connect or reconnect to WiFi
if(WiFi.status() != WL_CONNECTED){
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(SECRET_SSID);
  while(WiFi.status() != WL_CONNECTED){
    WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
    Serial.print(".");
    delay(5000);     
  } 
  Serial.println("\nConnected.");
}
  
float h = dht.readHumidity();    
float t = dht.readTemperature(); 

float ppm = mq135_sensor.getPPM();
float correctedPPM = 0;
if (isnan(h) || isnan(t) ) {
  Serial.println("Failed to read from DHT sensor!");
  return;
}
if (isnan(ppm)) {
  Serial.println("Failed to read from MQ sensor!");
  return;
}

correctedPPM = mq135_sensor.getCorrectedPPM(t, h);

////////////////////////////
Serial.print("Nhiet do: ");
Serial.print(t);               
Serial.print(", Do am: ");
Serial.print(h);
Serial.print(", PPM: ");
Serial.print(ppm);
Serial.print(", Corrected PPM: ");
Serial.print(correctedPPM);
Serial.println();
// set the fields with the values
ThingSpeak.setField(1, t);
ThingSpeak.setField(2, h);
ThingSpeak.setField(3, correctedPPM);

  // figure out the status message
  /*if(number1 > number2){
    myStatus = String("field1 is greater than field2"); 
  }
  else if(number1 < number2){
    myStatus = String("field1 is less than field2");
  }
  else{
    myStatus = String("field1 equals field2");
  }
  
  // set the status
  ThingSpeak.setStatus(myStatus);
  */
  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if(x == 200){
    Serial.println("Channel update successful.");
  }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
  
  delay(19000); // Wait 20 seconds to update the channel again
}
