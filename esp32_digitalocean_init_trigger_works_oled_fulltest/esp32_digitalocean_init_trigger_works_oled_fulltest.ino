#include "WiFi.h"
#include "ArduinoJson.h"
#include "HTTPClient.h"
#include "driver/adc.h"
#include <esp_wifi.h>

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>

//ADC_MODE(ADC_VCC); //to use getVcc

// WiFi network details
#define WIFI_SSID "Pixel_9736"
#define WIFI_PASSWORD "eb5289d9d8e1"

//#define WIFI_SSID "PLATFORM-GUEST"
//#define WIFI_PASSWORD "PitchStage#1"

//#define WIFI_SSID "Shayan's iPhone"
//#define WIFI_PASSWORD "shayannn"

#define WIFI_TIMEOUT 10000 // 10 seconds in milliseconds
#define facility_name "testfacility"
#define nickname "kat"
#define temp_password "Bearer &%}npGH9yEB@HN>y"
#define id_strmax 25

#define network_icon 0x0119
#define upload_icon 0x008f
#define checkmark_icon 0x0073
#define star_icon 0x0102


// Store readings in RTC memory

RTC_DATA_ATTR bool pump_initialized = false;
RTC_DATA_ATTR char PUMP_ID[25];
RTC_DATA_ATTR int MAX_OFFLINE_READINGS = 1;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR unsigned char offlineReadingCount = 0;
RTC_DATA_ATTR unsigned int readings_temp[100];

// server to which to post json document
String url_1 = "http://www.i4h22-hygienie.xyz/api/pump/initialize";
String url_2 = "http://www.i4h22-hygienie.xyz/api/trigger/post-trigger";
//String url_1 = "http://ptsv2.com/t/a98qt-1650930832/post";
//String url_2 = "http://ptsv2.com/t/a98qt-1650930832/post";

#define Threshold 70 /* Greater the value, more the sensitivity */
touch_pad_t touchPin;

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

const float MAX_BATTERY_VOLTAGE = 4.2; // Max LiPoly voltage of a 3.7 battery is 4.2
const float MIN_BATTERY_VOLTAGE = 3.4; // Max LiPoly voltage of a 3.7 battery is 4.2

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -25200;
const int   daylightOffset_sec = 3600;

void print_on_oled(int text = -1, int height = -1) {
   u8g2.clearBuffer();
   u8g2.setDisplayRotation(U8G2_R1);
   if ( text == -1) {
    // prints something random
    u8g2.setFont(u8g2_font_streamline_pet_animals_t);

    int mod = random(10);
    text = 0x0030 + mod;
    Serial.println("emoji modifier: " + String(mod));
    
    u8g2.drawGlyph(8,60,text);
   }
   else if (height == -1){
    // prints a text in the middle
    char string[20];
    sprintf(string, "%d", text);
    u8g2.setFont(u8g2_font_logisoso16_tr);
    u8g2.drawStr(1,60,string);
   }
   else {
    // prints an emoji in the middle
     u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
     u8g2.drawGlyph(8,height,text);
   }
   u8g2.sendBuffer();
   delay(100);
   u8g2.clearBuffer();
 }

void print_droplet(void) {
   for (int i=0; i<100; i=i+5){
     print_on_oled(0x0098, i+29);
   }
 }


void callback(){
  //placeholder callback function
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void goToDeepSleep(){
  Serial.println("Going to sleep...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  // btStop();

  esp_wifi_stop();
  // esp_bt_controller_disable();

  touchAttachInterrupt(T8, callback, Threshold);
  esp_sleep_enable_touchpad_wakeup();
  
  // Go to sleep! Zzzz
  esp_deep_sleep_start();
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);            // station mode
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Only try 15 times to connect to the WiFi
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 15){
    delay(500);
    Serial.print(".");
    retries++;
  }

  // If we still couldn't connect to the WiFi, go to deep sleep
  if(WiFi.status() != WL_CONNECTED){
    goToDeepSleep();
  } else {
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RRSI: ");
    Serial.println(WiFi.RSSI());
  }
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason)
  {
    case 1  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case 2  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case 3  : Serial.println("Wakeup caused by timer"); break;
    case 4  : Serial.println("Wakeup caused by touchpad"); break;
    case 5  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
}

float read_battery_level(){
    int rawValue = analogRead(A13);

    // Reference voltage on ESP32 is 1.1V
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc.html#adc-calibration
    // See also: https://bit.ly/2zFzfMT
    
    float voltageLevel = (rawValue / 4095.0) * 2 * 1.1 * 3.3; // calculate voltage level
    float batteryFraction = ( voltageLevel - MIN_BATTERY_VOLTAGE ) / ( MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE );

    return batteryFraction;
}

void setup() {
  Serial.begin(115200); 
  
  //delay(1000); //Take some time to open up the Serial Monitor

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  u8g2.begin();
  print_droplet();
  print_on_oled();
  delay(1000);
  u8g2.clearBuffer();
  u8g2.setPowerSave(1);
  
}

void loop() {
  print_on_oled(checkmark_icon,70);
  delay(1000);
  time_t now = time(NULL);
  readings_temp[offlineReadingCount] = now;
  offlineReadingCount++;

  if(offlineReadingCount <= MAX_OFFLINE_READINGS){
      Serial.println("");
      Serial.print("# readings remaining until next trigger: ");
      Serial.print(MAX_OFFLINE_READINGS - offlineReadingCount);
      Serial.println("");
      
      goToDeepSleep();
      //return;
  } else {
      // read battery level prior to WiFi connection
      float battery_fraction = read_battery_level();
      connectToWiFi();
      if(WiFi.status()== WL_CONNECTED){

      // grab time from internet
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);        
      printLocalTime();

      if (!pump_initialized) {
          Serial.println("initialization loop");
          HTTPClient http;
          // Your Domain name with URL path or IP address with path
          http.begin(url_1);
          http.addHeader("Content-Type", "application/json");
          http.addHeader("Authorization", temp_password);         
           
          StaticJsonDocument<200> doc;
          doc["pumpID"] = "???";
          doc["batteryLevel"] = battery_fraction;
          doc["facility"] = facility_name;
          for (int i=0; i<MAX_OFFLINE_READINGS; i++){
            doc["uses"][i] = readings_temp[i];
            }
            
          String requestBody;
          serializeJson(doc, requestBody);
          int httpResponseCode = http.POST(requestBody);
          Serial.println(battery_fraction);
          
          String httpResponseBody = http.getString();
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          Serial.println(httpResponseBody);

          StaticJsonDocument<200> jsonBuffer;
          auto error = deserializeJson(jsonBuffer,httpResponseBody);
          if (error) {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(error.c_str());
            offlineReadingCount = 0; 
            return;
            } else {
              
              const char* pump_id_temp = jsonBuffer["pumpID"];
              Serial.print( "actual pump ID: " );
              Serial.print( pump_id_temp );
              Serial.println("");

              Serial.println("Looping over to copy...");
              for (int i=0;i<id_strmax;i++){
                PUMP_ID[i] = pump_id_temp[i];
                Serial.print( pump_id_temp[i] );
              }

              Serial.println("");
              Serial.print("read pump ID: ");
              Serial.print(PUMP_ID);
              Serial.println("");

              pump_initialized = true;
              
              }
          
          // Free resources
          WiFi.mode(WIFI_OFF);
          http.end();
          offlineReadingCount = 0; 
          MAX_OFFLINE_READINGS = 1;

          Serial.print("end of initialization loop: ");
          Serial.print("offlineReadingCount: ");
          Serial.print(offlineReadingCount);
          Serial.print(", MAX_OFFLINE_READINGS: ");
          Serial.print(MAX_OFFLINE_READINGS);
          
          } else {
              Serial.println("event loop");
              Serial.println(PUMP_ID);
              
              HTTPClient http;
              // Your Domain name with URL path or IP address with path
              http.begin(url_2);
              http.addHeader("Content-Type", "application/json");
              http.addHeader("Authorization", temp_password);         
               
              StaticJsonDocument<200> doc;
              doc["pumpID"] = PUMP_ID;
              doc["batteryLevel"] = battery_fraction;
              //doc["facility"] = facility_name;
              for (int i=0; i<MAX_OFFLINE_READINGS; i++){
                doc["uses"][i] = readings_temp[i];
                }
                
              String requestBody;
              serializeJson(doc, requestBody);
              int httpResponseCode = http.POST(requestBody);
              Serial.println(battery_fraction);
              
              String httpResponseBody = http.getString();
              Serial.print("HTTP Response code: ");
              Serial.println(httpResponseCode);
              Serial.println(httpResponseBody);

              WiFi.mode(WIFI_OFF);
              http.end();
              offlineReadingCount = 0; 
              Serial.print("end of trigger loop: ");
              Serial.print("offlineReadingCount: ");
              Serial.print(offlineReadingCount);
              Serial.print(", MAX_OFFLINE_READINGS: ");
              Serial.print(MAX_OFFLINE_READINGS);
              
              } 
      } else {
          MAX_OFFLINE_READINGS += 5;
          }        
    }
}
