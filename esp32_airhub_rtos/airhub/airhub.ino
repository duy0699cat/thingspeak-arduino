#include <WiFi.h>
#include "time.h"
#include <EEPROM.h>
#include "DHT.h"
#include "MQ135.h"
#include "secrets.h"
#include "ThingSpeak.h" // always include thingspeak header file after other header files and custom macros

///////////////////////////////////
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

///////////////////////////////////pins
#define PIN_MQ135 33
const int DHTPIN = 5;
const int DHTTYPE = DHT11;

//////////////wifi & thingspeak
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
//int keyIndex = 0;            // your network key Index number (needed only for WEP)
int counter = 0;
String myStatus = "";
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

////////////////var to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200; //gmt +7
const int   daylightOffset_sec = 0;
unsigned long timeUpdatePeriod = 300000; // 300000 for 5min test or 864000000 for 10days (milisec)
unsigned long lastUpdateTime;


//////////////air variables
float h, t, ppm, correctedPPM;


///////////////////////////////////
MQ135 mq135_sensor = MQ135(PIN_MQ135);
WiFiClient  client;
DHT dht(DHTPIN, DHTTYPE);


/////////////////////////////////////////////////////////////////
// define tasks
void SendtoThingSpeakTask(void *pvParameters);
void SensorsTask(void *pvParameters);
void WifiConnectTask(void *pvParameters);// have while()
void UpdateTimeTask(void *pvParameters);


// the setup function runs once when you press reset or power the board
void setup() {

  // initialize serial communication at 115200 bits per second:
  Serial.begin(115200);

  lastUpdateTime = millis();

  dht.begin();

  // network things
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  WiFi.begin(ssid, pass);
  // Now set up tasks to run independently.
  xTaskCreatePinnedToCore(
    WifiConnectTask
    ,  "WifiConnectTask"   // A name just for humans
    ,  8192  // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  0  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  NULL
    ,  0);

  xTaskCreatePinnedToCore(
    SendtoThingSpeakTask
    ,  "SendtoThingSpeakTask"
    ,  8192  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL
    ,  0);

  xTaskCreatePinnedToCore(
    SensorsTask
    ,  "SensorsTask"
    ,  1024  // Stack size
    ,  NULL
    ,  2  // Priority
    ,  NULL
    ,  1);

  xTaskCreatePinnedToCore(
    UpdateTimeTask
    ,  "UpdateTimeTask"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL
    ,  1);
  // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.
}

void loop()
{
  // Empty. Things are done in Tasks.
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

///////////////////////////////////////////////////////////////////
void SendtoThingSpeakTask(void *pvParameters)  // This is a task.
{
  (void) pvParameters;
  String taskMessage = "SendtoThingSpeakTask running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
    // set the fields with the values
    ThingSpeak.setField(1, t);
    ThingSpeak.setField(2, h);
    ThingSpeak.setField(3, correctedPPM);

    // figure out the status message
    /*if(number1 > number2){
      myStatus = String("field1 is greater than field2");
      ThingSpeak.setStatus(myStatus);
    */
    // write to the ThingSpeak channel
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200) {
      Serial.println("Channel update successful.");
      vTaskDelay(15000);  // original val. 10, one tick delay (15ms) in between reads for stability
      //10000 = 15s?
    }
    else {
      Serial.println("Problem updating channel. HTTP error code " + String(x));
      vTaskDelay(3000); //retry 4.5s
    }
  }
}
//////////////////////////////
void SensorsTask(void *pvParameters)  // This is a task.
{
  (void) pvParameters;
  String taskMessage = "SensorsTask running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
    h = dht.readHumidity();
    t = dht.readTemperature();
    ppm = mq135_sensor.getPPM();
    
vTaskDelay(100);
    if (isnan(h) || isnan(t) ) Serial.println("Failed to read from DHT sensor!");

    else if (ppm == 0 ) Serial.println("Failed to read from MQ sensor!");

    else {
      correctedPPM = mq135_sensor.getCorrectedPPM(t, h);
      ////////////////////////////////////
      Serial.print("Sensors values: Nhiet do: ");
      Serial.print(t);
      Serial.print(", Do am: ");
      Serial.print(h);
      Serial.print(", PPM: ");
      Serial.print(ppm);
      Serial.print(", Corrected PPM: ");
      Serial.print(correctedPPM);
      Serial.println();

    }

    vTaskDelay(1000);
  }
}

/////////////////////////////////////////////////
void WifiConnectTask(void *pvParameters)  // This is a task.
{ // Connect or reconnect to WiFi
  (void) pvParameters;
  String taskMessage = "WifiConnectTask running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(SECRET_SSID);

      while (WiFi.status() != WL_CONNECTED)
      {
        WiFi.begin(ssid, pass);
        delay(3000);
      }  // Connect to WPA/WPA2 network. Change this line if using open or WEP network

      // and get the time
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      lastUpdateTime = millis();

    }
    else {
      Serial.println("Wifi Connected.");
      vTaskDelay(4000);
    }//recheck period
  }
}
//////////////////////////////////////////////////////////
void UpdateTimeTask(void *pvParameters)  // This is a task.
{
  (void) pvParameters;
  String taskMessage = "UpdateTimeTask running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
    printLocalTime();
    if ( millis() - lastUpdateTime >= timeUpdatePeriod)  //test whether the period has elapsed
    { Serial.println(" Performing time update ");
      if (WiFi.status() == WL_CONNECTED) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); //update time
        lastUpdateTime = millis();
        Serial.println("update completed");
      }
      else Serial.println("no wifi to update time");
    }
    Serial.print((timeUpdatePeriod - millis() + lastUpdateTime) / 1000);
    Serial.print(" seconds till next time update ");
    Serial.println();

    vTaskDelay(2000); //recheck period
  }
}

//////////////////////////////////////////////////////////


/*---------------------- Functions ---------------------*/
void printLocalTime() { // This is a function.
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}
