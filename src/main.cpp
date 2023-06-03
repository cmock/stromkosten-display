#include <NeoPixelBusLg.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include "config.h"

#include "awattar_testdata.h"

#define LED_ROWS 8
#define LED_COLS 8
#define NUM_LEDS (LED_ROWS * LED_COLS)
#define LED_PIN 16
//CRGB leds[NUM_LEDS];
//CRGBArray<NUM_LEDS> leds;


#define YELLOW_LIMIT 80
#define RED_LIMIT 120
#define PRICE_MAX 160

#define FORMAT_LITTLEFS_IF_FAILED true

// colors
// CRGB::Yellow
// CRGB::Green
// CRGB::Red
// fill_solid(&leds[i], num_leds, CRGB::color)

int dot = 0;

NeoPixelBusLg<NeoGrbFeature, NeoEsp32I2s0Ws2812xMethod> leds(NUM_LEDS, LED_PIN);
NeoTopology<RowMajorLayout> topo(LED_ROWS, LED_COLS);

#define brightness 64

RgbColor red(255, 0, 0);
RgbColor green(0, 255, 0);
RgbColor blue(0, 0, 255);
RgbColor yellow(255, 255, 0);
RgbColor white(255);
RgbColor black(0);

const uint16_t left = 0;
const uint16_t right = LED_COLS - 1;
const uint16_t top = 0;
const uint16_t bottom = LED_ROWS - 1;

const size_t json_capacity = JSON_OBJECT_SIZE(3) + 24 * JSON_OBJECT_SIZE(4);
StaticJsonDocument<json_capacity> json_doc;
#define MAX_JSON_LEN 5120
#define NOW (1685514246)

double prices[LED_COLS];

WiFiMulti wifiMulti;
bool wifi_up = false;

WiFiClientSecure client;

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }
    file.close();
}

void parse_json(char input[], time_t now) {
  DeserializationError error = deserializeJson(json_doc, input, MAX_JSON_LEN);
  if(error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char *object = json_doc["object"];
  if(strcmp(object, "list")) {
    Serial.println("JSON: object != list");
    return;
  }

  now -= (now % 3600); // round to hour
  
  for (JsonObject data_item : json_doc["data"].as<JsonArray>()) {
    time_t start_timestamp = data_item["start_timestamp"].as<long long>()/1000;
    time_t end_timestamp = data_item["end_timestamp"].as<long long>()/1000;
    double marketprice = data_item["marketprice"]; 
    const char* unit = data_item["unit"];
    if(strcmp(unit, "Eur/MWh")) {
      Serial.println("JSON: unit not Eur/Mwh");
      return;
    }
    if(start_timestamp < now)
      continue;
    time_t slot = (start_timestamp - now)/3600;
    char buf[1024];
    sprintf(buf, "%d %d %.2f\n", start_timestamp, slot, marketprice);
    Serial.print(buf);
    if(slot >= LED_COLS)
      continue;
    prices[slot] = marketprice;
  }
}

// col: 0..7, len: 0..8
void bar(uint16_t col, double len, RgbColor color) {
  if(col >= LED_COLS)
    col = LED_COLS-1;
  if(len > 1.0)
    len = 1.0;

  uint16_t full = len * LED_ROWS; // integer part
  float frac = len * LED_ROWS - full;
  char buf[1024];
  //  sprintf(buf, "blend: len %.2f, full %d, frac %.1f\n", len, full, frac);
  //  Serial.print(buf);
  
  // clear
  for(uint16_t y = 0; y < LED_ROWS; y++)
    leds.SetPixelColor(topo.Map(col, y), black);
  // always set lowest pixel
  leds.SetPixelColor(topo.Map(col, LED_ROWS-1), color);
  for(uint16_t y = 1; y <= full; y++)
    leds.SetPixelColor(topo.Map(col, LED_ROWS-y), color);
  if(frac > 0.09)
    leds.SetPixelColor(topo.Map(col, LED_ROWS-full-1),
		       RgbColor::LinearBlend(black, color, frac));
}

void

void setup() {
  Serial.begin(115200);
  while(!Serial);
  delay(2000);
  Serial.println();
  Serial.println("Starting...");
  Serial.print("json_capacity: "); Serial.println(json_capacity);
  //  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  leds.Begin();
  leds.SetLuminance(brightness);
  leds.Show();

  WiFi.setHostname(HOSTNAME);
  wifiMulti.addAP(WIFI1_SSID, WIFI1_PSK);
  wifiMulti.addAP(WIFI2_SSID, WIFI2_PSK);


  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed");
    return;
  }
  
  Serial.println("LittleFS mounted, Running.");

  readFile(LittleFS, "/cache.txt");
  writeFile(LittleFS, "/cache.txt", "first test\n");

  parse_json(testdata, NOW);
}


void loop() {

  if(wifiMulti.run() == WL_CONNECTED) {
    wifi_up = true;
  } else {
    wifi_up = false;
  }     

  for (uint16_t i = 0; i < LED_COLS; i++) {
    bar(i, prices[i]/PRICE_MAX, (prices[i] >= RED_LIMIT ? red : 
		 (prices[i] >= YELLOW_LIMIT ? yellow :
		  green)));
  }
  //Serial.println("bars");
  leds.Show();
  delay(1000);

}
