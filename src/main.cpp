#include <NeoPixelBusLg.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <sntp.h>

#include "config.h"

#include "awattar_testdata.h"

#define LED_ROWS 8
#define LED_COLS 8
#define NUM_LEDS (LED_ROWS * LED_COLS)
#define LED_PIN 16

// append ".json" for the data and ".expiry" for the expiry time
#define PRICE_PATH "/prices"
#define POWER_MONITOR_PATH "/power-monitor"
// how long before expiry will we try and fetch?
#define DATA_EXPIRY_BUFFER 3600
#define DATA_FETCH_RETRY 600
// how often to check if display update is necessary, in milliseconds
#define DISPLAY_INTERVAL (10*1000)

#define USER_AGENT "energy-cost-display/0.1 (ESP32HTTPClient) cm@tahina.priv.at"

// https://api.awattar.at/v1/marketdata
#define AWATTAR_API_URL "https://www.tahina.priv.at/tmp/marketdata.json"

// https://awareness.cloud.apg.at/api/v1/PeakHourStatus
#define POWER_MONITOR_API_URL "https://www.tahina.priv.at/tmp/PeakHourStatus.json"

#define YELLOW_LIMIT 80
#define RED_LIMIT 120
#define PRICE_MAX 160

#define FORMAT_LITTLEFS_IF_FAILED true


// global system state
struct {
  bool wifi_up, valid_time, valid_price_data, valid_monitor_data;
  time_t price_expiry, powermonitor_expiry,
    last_fetch_price, last_fetch_powermonitor;
} state = { false, false, false, false,
  0, 0, 0, 0,
};

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

//const size_t json_capacity = JSON_OBJECT_SIZE(3) + 25 * JSON_OBJECT_SIZE(4);
const size_t json_capacity = 2048;
StaticJsonDocument<json_capacity> json_doc;
#define MAX_JSON_LEN 5120

#define BIG_BUF_LEN 4096
char big_buf[BIG_BUF_LEN], big_buf2[BIG_BUF_LEN];

double prices[LED_COLS];

WiFiMulti wifiMulti;

bool GET(const char *url, char *buf, size_t maxlen) {
  WiFiClientSecure *client = new WiFiClientSecure;
  bool error = false;
  if(client) {
    // this client only accepts a single root cert, so either you go chasing
    // new root certs everytime the upstream server changes cert, or you
    // just skip certificate verification...
    client->setInsecure();
    HTTPClient https;
    https.setUserAgent(USER_AGENT);
    if(https.begin(*client, url)) {
      int httpCode = https.GET();
      Serial.printf("GET %s: code %d\n", url, httpCode);
      if(httpCode > 0) {
	if(httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
	  String payload = https.getString();
	  strncpy(buf, payload.c_str(), maxlen);
	}
      } else {
	Serial.println("GET failed");
	error = true;
      }
      https.end();
    } else {
      Serial.println("HTTPS.begin failed");
      error = true;
    }
    delete client;
  }
  return !error;
}

// returns timestamp of last entry, or 0 on error
time_t parse_awattar_json(char input[], time_t now) {
  time_t last_timestamp = 0;
  
  DeserializationError error = deserializeJson(json_doc, input, MAX_JSON_LEN);
  if(error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    Serial.printf("Capacity: %d\n", json_doc.capacity());
    return 0;
  }

  const char *object = json_doc["object"];
  if(strcmp(object, "list")) {
    Serial.println("JSON: object != list");
    return 0;
  }

  now -= (now % 3600); // round to hour
  
  for (JsonObject data_item : json_doc["data"].as<JsonArray>()) {
    time_t start_timestamp = data_item["start_timestamp"].as<long long>()/1000;
    time_t end_timestamp = data_item["end_timestamp"].as<long long>()/1000;
    double marketprice = data_item["marketprice"]; 
    const char* unit = data_item["unit"];
    if(strcmp(unit, "Eur/MWh")) {
      Serial.println("JSON: unit not Eur/Mwh");
      return 0;
    }
    if(start_timestamp > last_timestamp)
      last_timestamp = start_timestamp;
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
  return last_timestamp;
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

void fetch_awattar() {
  time_t now = time(NULL);
  // fetch
  if(!GET(AWATTAR_API_URL, big_buf, BIG_BUF_LEN))
    return;
  // copy big_buf because json parsing modifies it...
  memcpy(big_buf2, big_buf, BIG_BUF_LEN);
  // check
  time_t last_price = parse_awattar_json(big_buf, now);
  if(last_price == 0)
    return;
  // update expiry time
  state.price_expiry = last_price;
  state.valid_price_data = true;
  // write cache
  writeFile(LittleFS, PRICE_PATH ".json", big_buf2);
}


bool readFile(fs::FS &fs, const char * path, char *buf, size_t maxlen){
  char *p = buf;
  size_t avail = maxlen;
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return false;
  }

  Serial.println("- read from file:");
  while(file.available()){
    size_t n = file.readBytes(p, avail);
    Serial.println(n);
    p+=n;
    avail-=n;
    if(avail <= 0) {
      Serial.println("No more buffer space while reading");
      return false;
    }
  }
  file.close();
  return true;
}

// read cached data and expiry timestamps
bool read_caches() {
  time_t now = time(NULL);
  
  if(readFile(LittleFS, PRICE_PATH ".json", big_buf, BIG_BUF_LEN)) {
    Serial.println("read " PRICE_PATH ".json");
    // Serial.println("---");
    // Serial.println(big_buf);
    // Serial.println("---");
    time_t last_price = parse_awattar_json(big_buf, now);
    if(last_price != 0) {
      // update expiry time
      state.price_expiry = last_price;
      state.valid_price_data = true;
    }
  }
  if(readFile(LittleFS, POWER_MONITOR_PATH ".json", big_buf, BIG_BUF_LEN)) {
    Serial.println("read " POWER_MONITOR_PATH " .json");
  }
  if(readFile(LittleFS, POWER_MONITOR_PATH ".expiry", big_buf, BIG_BUF_LEN)) {
    Serial.println("read " POWER_MONITOR_PATH " .expiry");
  }
  return true;
}


// col: 0..7, len: 0..8
void bar(uint16_t col, double len, RgbColor color, RgbColor bgcolor) {
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
    leds.SetPixelColor(topo.Map(col, y), bgcolor);
  // always set lowest pixel
  leds.SetPixelColor(topo.Map(col, LED_ROWS-1), color);
  for(uint16_t y = 1; y <= full; y++)
    leds.SetPixelColor(topo.Map(col, LED_ROWS-y), color);
  if(frac > 0.09)
    leds.SetPixelColor(topo.Map(col, LED_ROWS-full-1),
		       RgbColor::LinearBlend(black, color, frac));
}

void printTime()
{
  struct tm timeinfo;
  time_t now;
  if(!getLocalTime(&timeinfo)){
    Serial.println("No time available (yet)");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  time(&now);
  Serial.println(now);
}

// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t)
{
  Serial.println("Got time adjustment from NTP!");
  state.valid_time = true;
}

void print_timers() {
  time_t now;
  time(&now);
  Serial.println("Timers:");
  Serial.print("Prices: ");
  Serial.print(state.price_expiry);
  Serial.print(" - ");
  Serial.println(state.last_fetch_price - now);
  Serial.print("Monitor: ");
  Serial.print(state.powermonitor_expiry);
  Serial.print(" - ");
  Serial.println(state.last_fetch_powermonitor - now);
}

void ls_littlefs() {
  File root = LittleFS.open("/");
  if(!root) {
    Serial.println("ls_littlefs: failed to open /");
    return;
  }
  if(!root.isDirectory()) {
    Serial.println("ls_littlefs: / is not a directory");
    return;
  }
  File file = root.openNextFile();
  while(file) {
    Serial.printf("%3s %6d %s\n", file.isDirectory() ? "DIR" : "",
		  file.size(), file.name());
    file = root.openNextFile();
  }
}
  

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
  leds.ClearTo(black);
  leds.SetPixelColor(0, green); // show power on state
  leds.SetPixelColor(1, red); // show waiting for wifi
  leds.Show();

  sntp_set_time_sync_notification_cb( timeavailable );
  // we don't need local time offset
  configTime(0, 0, NTP_SERVER, NULL, NULL);
  
  WiFi.setHostname(HOSTNAME);
  wifiMulti.addAP(WIFI1_SSID, WIFI1_PSK);
  wifiMulti.addAP(WIFI2_SSID, WIFI2_PSK);


  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed");
    return;
  }
  
  Serial.println("LittleFS mounted, Running.");
  ls_littlefs();
}


void loop() {
  time_t now;
  static unsigned long last_display_millis = 0;

  if(wifiMulti.run() == WL_CONNECTED) {
    state.wifi_up = true;
  } else {
    state.wifi_up = false;
  }

  if(state.wifi_up && state.valid_time &&
     last_display_millis + DISPLAY_INTERVAL < millis()) {
    last_display_millis = millis();
    // check if we need to do some API calls
    read_caches();
    time(&now);
    if(state.last_fetch_price + DATA_FETCH_RETRY < now &&
       state.price_expiry < now + DATA_EXPIRY_BUFFER) {
      Serial.println("fetch awattar");
      fetch_awattar();
      state.last_fetch_price = now;
      print_timers();
    }
    if(state.last_fetch_powermonitor + DATA_FETCH_RETRY < now &&
       state.powermonitor_expiry < now + DATA_EXPIRY_BUFFER) {
      Serial.println("fetch APG");
      state.last_fetch_powermonitor = now;
      print_timers();
    }
  }
    

  if(state.valid_price_data) {
    for (uint16_t i = 0; i < LED_COLS; i++) {
      bar(i, prices[i]/PRICE_MAX, (prices[i] >= RED_LIMIT ? red : 
				   (prices[i] >= YELLOW_LIMIT ? yellow :
				    green)),
	  black);
    }
    //Serial.println("bars");
    leds.Show();
  }
  if(state.valid_monitor_data) {
    // show monitor data
  }
  if(!state.valid_price_data && !state.valid_monitor_data) { // show system state
    // first N pixels:
    // 0 green -- power on
    // 1 wifi
    // 2 time
    // 3 price data
    // 4 power monitor data
    leds.ClearTo(black);
    leds.SetPixelColor(0, green);
    leds.SetPixelColor(1, state.wifi_up ? green : red);
    leds.SetPixelColor(2, state.valid_time ? green : red);
    leds.SetPixelColor(3, state.valid_price_data ? green: red);
    leds.SetPixelColor(4, state.valid_monitor_data ? green: red);
    leds.Show();
    //    delay(500);
  }


}
