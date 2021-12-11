/* Includes display */
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"
#include <stdlib.h>
#include "init.h"
/* welcome */
#include <EEPROM.h>

/* WIFI */
//#define WIFI
#ifdef WIFI
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <BluetoothSerial.h>
#include "driver/adc.h"
#include <esp_wifi.h>
#include <esp_sleep.h>
WiFiManager wifiManager;
#endif

/* led */
#include <Adafruit_DotStar.h>
#include <SPI.h>

#define DATAPIN 40
#define CLOCKPIN 39
Adafruit_DotStar strip(1, DATAPIN, CLOCKPIN, DOTSTAR_BRG);

/* scd4x */
#include <Arduino.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>
SensirionI2CScd4x scd4x;

RTC_DATA_ATTR bool initDone = false;
RTC_DATA_ATTR int refreshes = 1;
RTC_DATA_ATTR UBYTE *BlackImage;
RTC_DATA_ATTR bool BatteryMode = false;
RTC_DATA_ATTR bool commingFromDeepSleep = false;
RTC_DATA_ATTR bool fensterAuf = false;
RTC_DATA_ATTR bool fensterZu = false;
RTC_DATA_ATTR int ledbrightness = 5;
RTC_DATA_ATTR uint16_t co2 = 400;
RTC_DATA_ATTR int dataReadyErrorCount = 0;

#ifdef WIFI
#define tempOffset 13.0
#else
#define tempOffset 4.4 // was 5.8
#endif

void displayWelcome() {
  Paint_DrawBitMap(gImage_welcome);
  EPD_1IN54_V2_Display(BlackImage);
  EPD_1IN54_V2_Sleep();

  EEPROM.write(0, 1);
  EEPROM.commit();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);    // RTC IO, sensors and ULP co-processor
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF); // RTC slow memory: auto
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);  // RTC fast memory
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);          // XTAL oscillator
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);         // CPU core
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
  esp_deep_sleep_start();
}

void initOnce() {
  const int Imagesize = 5000; //25 * 200
  BlackImage = (UBYTE *)malloc(Imagesize);
  Paint_NewImage(BlackImage, EPD_1IN54_V2_WIDTH, EPD_1IN54_V2_HEIGHT, 270, WHITE);
  EPD_1IN54_V2_Init();
  Paint_Clear(WHITE); 
  EEPROM.begin(1); //EEPROM_SIZE
  int welcomeDone = EEPROM.read(0);
  if(welcomeDone != 1) displayWelcome();

  scd4x.stopPeriodicMeasurement(); // stop potentially previously started measurement
  scd4x.setSensorAltitude(50);     // Berlin: 50m über NN
  scd4x.setAutomaticSelfCalibration(1);
  scd4x.setTemperatureOffset(tempOffset);
  scd4x.startPeriodicMeasurement();
  
  Paint_DrawBitMap(gImage_init);
  EPD_1IN54_V2_Display(BlackImage);
  EPD_1IN54_V2_Sleep();
  //refreshes++;
  delay(3000); // Wait for co2 measurement
  initDone = true;
}

void setLED(uint16_t co2) {
  int red = 0, green = 0, blue = 0;

  if (co2 > 2000) {
    red = 216; green = 2; blue = 131; //magenta
  } else {
    red   =   pow((co2-400),2)/10000;       //= 0.13 * co2 - 90;
    green = - pow((co2-400),2)/4500 + 255;  //-0.21 * co2 + 318;
  }
  if (red < 0) red = 0;
  if (red > 255) red = 255;
  if (green < 0) green = 0;
  if (green > 255) green = 255;
  if (blue < 0) blue = 0;
  if (blue > 255) blue = 255;
  
  red = (int)(red * (ledbrightness/100.0));
  green = (int)(green * (ledbrightness/100.0));
  blue = (int)(blue * (ledbrightness/100.0));
    
  strip.setPixelColor(0, green, red, blue);
  strip.show();
}

void lowBatteryMode() {
  scd4x.stopPeriodicMeasurement();
  Paint_Clear(BLACK);
                    //Xstart,Ystart,Xend,Yend
  Paint_DrawRectangle( 50,  40, 150, 90, WHITE, DOT_PIXEL_3X3, DRAW_FILL_EMPTY);
  Paint_DrawRectangle(150,  55, 160, 75, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawLine(      60, 100, 140, 30, WHITE, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
  Paint_DrawString_EN(45, 120, "Batterie", &Font20, BLACK, WHITE);
  Paint_DrawString_EN(45, 145, "aufladen", &Font20, BLACK, WHITE);

  EPD_1IN54_V2_Init();
  EPD_1IN54_V2_Display(BlackImage);
  EPD_1IN54_V2_DisplayPartBaseImage(BlackImage);
  EPD_1IN54_V2_Sleep();

  esp_sleep_enable_ext0_wakeup((gpio_num_t)4,1);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);   // RTC IO, sensors and ULP co-processor
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_AUTO); // RTC slow memory: auto
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);  // RTC fast memory
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);          // XTAL oscillator
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);         // CPU core
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
  esp_deep_sleep_start();
}

void goto_deep_sleep() {
#ifdef WIFI
  //WiFi.setSleep(true);
  WiFi.disconnect(true);  // Disconnect from the network
  WiFi.mode(WIFI_OFF);    // Switch WiFi off
#endif
  esp_sleep_enable_ext0_wakeup((gpio_num_t)4,1);
  esp_sleep_enable_timer_wakeup(29000000);  // periodic measurement every 30 sec - 0.83 sec awake 
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);    // RTC IO, sensors and ULP co-processor
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_AUTO); // RTC slow memory: auto
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);  // RTC fast memory
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);          // XTAL oscillator
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);         // CPU core
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
  
  commingFromDeepSleep = true;
  esp_deep_sleep_start();
}

void goto_light_sleep(int ms){
  commingFromDeepSleep = false;
#ifdef WIFI
  delay(ms);
#else
  esp_sleep_enable_timer_wakeup(ms*1000);  // periodic measurement every 5 sec -1.1 sec awake
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);    // RTC IO, sensors and ULP co-processor
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_AUTO); // RTC slow memory: auto
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);  // RTC fast memory
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);          // XTAL oscillator
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);         // CPU core
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
  esp_light_sleep_start();
#endif
  //free(BlackImage);
  //BlackImage = NULL;
}

void setup() {
  DEV_Module_Init();

  /* scd4x */
  Wire.begin(33,34); // grün, gelb
  scd4x.begin(Wire);

  if (!initDone) initOnce();

  /* power */
  pinMode(4, INPUT);  // usb Power
  pinMode(5, INPUT);  // Battery Voltage
  
  BatteryMode = (digitalRead(4) == LOW);  
#ifdef WIFI
  if (!BatteryMode) {
    //if (commingFromDeepSleep) {}
    /*WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);*/
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.autoConnect("CO2Sensor");
  }
#endif

  strip.begin();
  if (!BatteryMode && commingFromDeepSleep) {
    delay(10);
    setLED(co2);
    scd4x.stopPeriodicMeasurement();   // stop low power measurement
    scd4x.setTemperatureOffset(tempOffset);
    scd4x.startPeriodicMeasurement();
    /* Wait for co2 measurement */
    goto_light_sleep(4000); 
  }
}

void loop(){
  BatteryMode = (digitalRead(4) == LOW); //check again in USB Power mode
  Paint_Clear(WHITE);
#ifdef WIFI
  if (!BatteryMode) wifiManager.process();
#endif

  uint16_t dataReady;
  uint16_t ready_error = scd4x.getDataReadyStatus(dataReady);
  if (ready_error || !(((dataReady | (~dataReady + 1)) >> 11) & 1)) {
    //Least significant 11 bits are 0 -> data not ready
    dataReadyErrorCount++;
    if (BatteryMode) goto_deep_sleep();
    else goto_light_sleep(4000);
    return; //otherwise continues running... why?
  }

  // Read co2 Measurement
  uint16_t new_co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;
  uint16_t error = scd4x.readMeasurement(new_co2, temperature, humidity);
  if (error) {
    char errorMessage[256];
    errorToString(error, errorMessage, 256);
    Paint_DrawString_EN(5, 40, errorMessage, &Font20, WHITE,BLACK);
  } else {
     /* dont update in Battery mode, unless co2 is +- 10 ppm different */
    if (BatteryMode && new_co2 < co2+10 && new_co2 > co2-10) goto_deep_sleep();
    if (new_co2 > 400) co2 = new_co2;
    
    /* co2 */
    if      (co2 > 9999) Paint_DrawNum(27, 78, co2, &bahn_mid, BLACK, WHITE);
    else if (co2 < 1000) Paint_DrawNum(30, 65, co2, &bahn_big, BLACK, WHITE);
    else                 Paint_DrawNum( 6, 65, co2, &bahn_big, BLACK, WHITE);
    Paint_DrawString_EN(142, 150, "ppmn", &bahn_sml, WHITE, BLACK);

    /* temperature */
    if (temperature < 10.0f) Paint_DrawNum(30, 5, temperature, &bahn_mid, BLACK, WHITE);
    else                     Paint_DrawNum( 1, 5, temperature, &bahn_mid, BLACK, WHITE);
    Paint_DrawString_EN(60, 4, "*C", &bahn_sml, WHITE, BLACK);
    Paint_DrawString_EN(60, 32, ",", &bahn_sml, WHITE, BLACK);
    char decimal[1];
    sprintf(decimal, "%d", ((int)(temperature*10))%10);
    Paint_DrawString_EN(71, 27, decimal, &bahn_sml, WHITE, BLACK);
  
    /* humidity */
    Paint_DrawNum(124, 5, humidity, &bahn_mid, BLACK, WHITE);
    Paint_DrawString_EN(184, 5, "%", &bahn_sml, WHITE, BLACK);   
  }

  if (!BatteryMode) {
    setLED(co2);
  } else if (!commingFromDeepSleep) {
    strip.clear();
    strip.show();
  }
      
#ifdef WIFI
  if(!error && !BatteryMode) {
    if (WiFi.status() == WL_CONNECTED) {
      String payload = "{\"wifi\":" + String(WiFi.RSSI()) + ",";
      payload=payload+"\"pm02\":" + "100";
      payload=payload+",";
      payload=payload+"\"rco2\":" + String(co2);
      payload=payload+",";
      payload=payload+"\"atmp\":" + String(temperature) +   ",\"rhum\":" + String(humidity);
      payload=payload+"}";
  
      String APIROOT = "http://10.0.0.4:9925/";
      String POSTURL = APIROOT + "sensors/airgradient:" + "1995c6" + "/measures";
      WiFiClient client;
      HTTPClient http;
      http.begin(client, POSTURL);
      http.addHeader("content-type", "application/json");
      int httpCode = http.POST(payload);
      String response = http.getString();
      http.end();
    }
  }
#endif

  /* Print Battery % */  
  if (BatteryMode) {
    float voltage = ((analogRead(5)*3.33)/5084); //IO5 /was 5210
    voltage += 0.02; //offset measurement
    if (voltage < 3.1) lowBatteryMode();

    char batteryvolt[8] = "";
    dtostrf(voltage, 1, 3, batteryvolt);
    char volt[10] = "V";
    strcat(batteryvolt, volt);
    //Paint_DrawString_EN(100, 180, batteryvolt, &Font20, WHITE, BLACK);
        
    uint8_t percentage = 0;
    if (voltage <= 3.61) percentage = 46 * pow((voltage-3.1),2);
    else if (voltage > 4.19) percentage = 100;
    //else percentage = 2808.3808*pow(voltage,4) - 43560.9157*pow(voltage,3) + 252848.5888*pow(voltage,2) - 650767.4615*voltage + 626532.5703;
    else percentage = 2836.9625 * pow(voltage,4) - 43987.4889 * pow(voltage,3) + 255233.8134 * pow(voltage,2) - 656689.7123*voltage + 632041.7303; 
    
    //                  Xstart,Ystart,Xend,Yend
    Paint_DrawRectangle( 15, 145, 120, 169, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(120, 149, 125, 165, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(15, 145, 105*(percentage/100.0)+15, 169, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  }

  /*Paint_DrawNum(0, 0, (int32_t)refreshes, &Font20, BLACK, WHITE);
  char dataReadyErrorCount_c[10];
  sprintf(dataReadyErrorCount_c, "%d", dataReadyErrorCount);
  Paint_DrawString_EN(160, 180, dataReadyErrorCount_c, &Font20, WHITE, BLACK);*/
  if (refreshes == 1) {
    EPD_1IN54_V2_Init();
    EPD_1IN54_V2_Display(BlackImage);
    EPD_1IN54_V2_DisplayPartBaseImage(BlackImage);
  } else {
    EPD_1IN54_V2_DisplayPart(BlackImage); // partial update
  }
  EPD_1IN54_V2_Sleep();
  if (refreshes == 720) { // every hour or every six hours on battery
    refreshes = 0; // force full update
  }
  refreshes++;

  if (BatteryMode) {
    if (!commingFromDeepSleep){
      scd4x.stopPeriodicMeasurement();
      scd4x.setTemperatureOffset(0.8); 
      scd4x.startLowPowerPeriodicMeasurement();
    }
    goto_deep_sleep();
  }
  goto_light_sleep(4000);
}
