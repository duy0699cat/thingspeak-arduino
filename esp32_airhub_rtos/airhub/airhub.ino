#include <WiFi.h> // This library is for the https connection.
#include <WiFiClientSecure.h> //This library is for the wifi connection.
//#include "SD_MMC.h" // This library is for use SD card.
#include "time.h"
#include "FS.h"
#include "SPIFFS.h"
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

/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED true

///////////////////////////////////pins
#define PIN_MQ135 33
const int DHTPIN = 5;
const int DHTTYPE = DHT11;

//////////////wifi & thingspeak
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
//int keyIndex = 0;            // your network key Index number (needed only for WEP)

bool noInternet = false;
String myStatus = "";
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

////////////////var to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200; //gmt +7
const int   daylightOffset_sec = 0;
unsigned long timeUpdatePeriod = 300000; // 300000 for 5min test or 864000000 for 10days (milisec)
unsigned long lastUpdateTime;
struct tm timeinfo;

//////////////air variables
float h, t, ppm, correctedPPM;


///////////////////////////////////
MQ135 mq135_sensor = MQ135(PIN_MQ135);
WiFiClient  client;
DHT dht(DHTPIN, DHTTYPE);

////////////data struct to store offline //deprecated
/*struct airData {
  float h_off, t_off, correctedPPM_off;
  char date[12]; // HH-mm-DD-MM-YYYY
  };
*/

int File_counter = 0;

/////////////////////////////////////////////////////////////////
// define tasks
void SendtoThingSpeakTask(void *pvParameters);
void SensorsTask(void *pvParameters);
void WifiConnectTask(void *pvParameters);// have while()
void UpdateTimeTask(void *pvParameters);
void offlineTask(void *pvParameters);
/////////////spiffs funtions

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}


void createDir(fs::FS &fs, const char * path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char * path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}
void readFile(fs::FS &fs, String path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, String path, String message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}
/*
  void appendFile(fs::FS &fs, String path, String message) {
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("- message appended");
  } else {
    Serial.println("- append failed");
  }
  file.close();
  }
*/
/*void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("- file renamed");
  } else {
    Serial.println("- rename failed");
  }
  }
*/
void deleteFile(fs::FS &fs, String path) {
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

void testFileIO(fs::FS &fs, const char * path) {
  Serial.printf("Testing file I/O with %s\r\n", path);

  static uint8_t buf[512];
  size_t len = 0;
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }

  size_t i;
  Serial.print("- writing" );
  uint32_t start = millis();
  for (i = 0; i < 2048; i++) {
    if ((i & 0x001F) == 0x001F) {
      Serial.print(".");
    }
    file.write(buf, 512);
  }
  Serial.println("");
  uint32_t end = millis() - start;
  Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
  file.close();

  file = fs.open(path);
  start = millis();
  end = start;
  i = 0;
  if (file && !file.isDirectory()) {
    len = file.size();
    size_t flen = len;
    start = millis();
    Serial.print("- reading" );
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      if ((i++ & 0x001F) == 0x001F) {
        Serial.print(".");
      }
      len -= toRead;
    }
    Serial.println("");
    end = millis() - start;
    Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
    file.close();
  } else {
    Serial.println("- failed to open file for reading");
  }
}

// the setup function runs once when you press reset or power the board
void setup() {

  // initialize serial communication at 115200 bits per second:
  Serial.begin(115200);

  lastUpdateTime = millis();

  dht.begin();
  ////////////files to store data offline
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  /*  //Serial.println("Create log file.");
    listDir(SPIFFS, "/", 0);

    writeFile(SPIFFS, "/hello.txt", "Hello ");
    writeFile(SPIFFS, "/log.txt", temp);
    for (int i = 0; i < 100; i++) {
      appendFile(SPIFFS, "/log.txt", temp);

    }
    readFile(SPIFFS, "/log.txt");*/
  // network things
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  WiFi.begin(ssid, pass);
  delay(500);
  // Now set up tasks to run independently.
  xTaskCreatePinnedToCore(
    WifiConnectTask
    ,  "WifiConnectTask"   // A name just for humans
    ,  8192  // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  2  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
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

  xTaskCreatePinnedToCore(
    offlineTask
    ,  "offlineTask"
    ,  4096  // Stack size
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
  Serial.println();
  Serial.print("FreeHeap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print(",MinFreeHeap: ");
  Serial.print(ESP.getMinFreeHeap());
  Serial.print(",Size: ");
  Serial.print(ESP.getHeapSize());
  Serial.print(",MaxAlloc: ");
  Serial.print(ESP.getMaxAllocHeap());

  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
    if (WiFi.status() != WL_CONNECTED) {
      bool noInternet = true;
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(SECRET_SSID);

      while (WiFi.status() != WL_CONNECTED)
      {
        WiFi.begin(ssid, pass);
        delay(3000);
      }  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
      noInternet = false;
      send_logs();
      deleteLog();
      // and get the time
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      lastUpdateTime = millis();

    }
    else {
      bool noInternet = false;
      Serial.println("Wifi Connected.");
      vTaskDelay(5000);
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

////////////////////////////
void offlineTask(void *pvParameters)  // This is a task.
{
  (void) pvParameters;
  String taskMessage = "offlineTask running on core ";
  taskMessage = taskMessage + xPortGetCoreID();
  for (;;) // A Task shall never return or exit.
  { Serial.println(taskMessage);
if(!noInternet){
    Serial.println("no internet, start store data");
    String temp = String(File_counter);
    String temp2 = "/log" + temp + ".txt";
    String t_data = String(t) + " " + String(h) + " " + String(correctedPPM) + " " + String(timeinfo.tm_hour) + " " + String(timeinfo.tm_min) + " " + String(timeinfo.tm_mday) + " " + String(timeinfo.tm_mon + 1) + " " + String(1900 + timeinfo.tm_year) ;
    deleteFile(SPIFFS, temp2); //for overwrite
    writeFile(SPIFFS, temp2, t_data);
    readFile(SPIFFS, temp2);
    File_counter++;
    if (File_counter >= 100) File_counter = 0;
    vTaskDelay(60000); //write period, should be 60s
    }
    else {
      //donothing
      vTaskDelay(5000); //check again in 5s
      }
    }
  }
}

//////////////////////////////////////////////////////////


/*---------------------- Functions ---------------------*/
void printLocalTime() { // This is a function.

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}
void deleteLog() {
  for (int i = 0; i < 100; i++) {
    String temp = String(i);
    String temp2 = "/log" + temp + ".txt";
    deleteFile(SPIFFS, temp2);
  }
  File_counter = 0;
}
void send_logs() {
  char list_files[1010] = ""; // is where we put the file names for verification and sending it to the server.
  char name[50]; // is create the initial request to the server
  char name2[50]; // is to read the answer from the server
  int i, j, n;
  int nb_files;
  bool end = false;
  // To send all the files in the root we need to do two Files variable:
  File dir;
  File file;
  Serial.println("Begin send files...");
  file = dir.openNextFile();
  while (!end) {
    file = dir.openNextFile(); //We take the files one by one:
    if (!file) { //if file is null, we finished the read directory
      //We don’t break for fish the check and send the last files.
      end = true;
      name[0] = '\0';
    }
    //we send one by one the files
    for (n = 0, i = 0, j = 0; i < 100 && list_files != '\0'; i++) {
      //While we don’t have all name of file put character by character name files in “name2”

        //if we finish read the name we call send_csv_files_to_server
        Serial.println("Send" + String(n) + "/" + nb_files + " : " + String(name2));
        name2[j] = '\0';
        //send_csv_files_to_server(name2);
        n++;
        j = 0;
      }
    list_files[0] = '\0';
    sprintf(list_files, "%s%s;", list_files, name);
    if (end) break;
  }
file.close();
dir.close();
Serial.println("End send files");
}
