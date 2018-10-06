#include <Metro.h>
#include <FastLED.h>
#include <ESPHelper.h>
#include <ESPHelperFS.h>
#include <ESPHelperWebConfig.h>

// Some configs
#define DEFAULT_GLEAM_DELAY 10  // 200: 5 minutes  // 2353: 1 hour
#define DISABLE_LED_ON_SUCCESS_AFTER 30000  // 30000ms = 30 seconds. Disable status LED
//

// GPIO pins for light
#define RED_PIN_LEFT 14
#define GREEN_PIN_LEFT 16
#define BLUE_PIN_LEFT 12

#define RED_PIN_RIGHT 4
#define GREEN_PIN_RIGHT 5
#define BLUE_PIN_RIGHT 2
//

// topic that ESP8266 will be monitoring for new commands
#define TOPIC "/home/RGBLight0"
#define STATUS TOPIC "/status"
#define STATUS_STR_LEN 50
//

//
#define NET_CONFIG_FILE "/netConfig.json"  // just don't change it

// Color related
#define NUM_LEDS 1
#define LED_PIN 13
#define COLOR_ORDER GRB
#define STATUS_LED_BRIGHTNESS 20  // from 1 to 255

#define ARR_LEN 6
#define RGB_MAX 255
#define MAX_STEPS ARR_LEN * RGB_MAX

int rgbRainbowMap[ARR_LEN][3] = {
  { 1, 0, 0 },
  { 1, 1, 0 },
  { 0, 1, 0 },
  { 0, 1, 1 },
  { 0, 0, 1 },
  { 1, 0, 1 },
};

CRGB led[NUM_LEDS];

//


// default net info for unconfigured devices
netInfo homeNet = {
  .mqttHost = "10.3.14.15",
  .mqttUser = "",    //can be blank
  .mqttPass = "",    //can be blank
  .mqttPort = 1883,  //default port for MQTT is 1883 - change only if needed
  .ssid = "Some Wi-Fi Network", 
  .pass = "1234567890",
  .otaPassword = "12345678",
  .hostname = "ESP_WEB",
};


netInfo config;
ESPHelper myESP;


ESPHelperWebConfig webConfig;


//

int min(int a, int b) {
  return (a < b ? a : b);
}

enum lightModes { STANDBY, MANUAL, GLEAM };

typedef struct lightState {
  int mode;
  int fadePeriod;  // steps (ms) to make full color change
  int lightStep;
  Metro lightStepMetro;

  int red;
  int redStep;
  int redTagret;
  int green;
  int greenStep;
  int greenTagret;
  int blue;
  int blueStep;
  int blueTagret;

  int gleamStep;
  Metro gleamMetro;

  int redPin;
  int greenPin;
  int bluePin;
};

lightState lightLeft;
lightState lightRight;


char* lightTopic = TOPIC;
char* statusTopic = STATUS;
char statusString[STATUS_STR_LEN];  // line to send

//

//
Metro ledTimeout = Metro(DISABLE_LED_ON_SUCCESS_AFTER);
bool ledTimedOut = false;


// timeout before starting AP mode for configuration
Metro connectTimeout = Metro(20000);
bool timeout = false;

// AP moade setup info
const char* broadcastSSID = "ESP-Hotspot";
const char* broadcastPASS = "";
IPAddress broadcastIP = {192, 168, 1, 1};
//

void configureLightState(struct lightState *lt) {
  lt->mode = STANDBY;
  lt->fadePeriod = 1000;
  lt->lightStep = 0;
  lt->lightStepMetro = Metro(1);

  lt->red = 0;
  lt->green = 0;
  lt->blue = 0;

  lt->redTagret = 255;
  lt->greenTagret = 255;
  lt->blueTagret = 255;

  lt->gleamMetro = Metro(DEFAULT_GLEAM_DELAY);
  lt->gleamStep = 0;

  // Initialize pins as output
  pinMode(lt->redPin, OUTPUT);
  pinMode(lt->greenPin, OUTPUT);
  pinMode(lt->bluePin, OUTPUT);
}

void sendStatus(String statusLine) {
  statusLine.toCharArray(statusString, min(STATUS_STR_LEN, statusLine.length() + 1));
  myESP.publish(statusTopic, statusString);
  // myESP.publish(statusTopic, statusString, true);
}

void showStatus(CRGB color) {
    led[0] = color;
    FastLED.show();
}

void setup() {
  Serial.begin(115200);  // Serial debug prints

  // print some debug
  Serial.println("");  // Serial debug prints
  Serial.println("Starting Up - Please Wait...");  // Serial debug prints
  delay(100);

  // Setup FastLED
  FastLED.addLeds<WS2812, LED_PIN, COLOR_ORDER>(led, NUM_LEDS);
  FastLED.setBrightness(STATUS_LED_BRIGHTNESS);
  // Show status RED color
  showStatus(CRGB::Red);

  //
  analogWriteRange(256);

  // Setup light states

  lightLeft.redPin = RED_PIN_LEFT;
  lightLeft.greenPin = GREEN_PIN_LEFT;
  lightLeft.bluePin = BLUE_PIN_LEFT;

  lightRight.redPin = RED_PIN_RIGHT;
  lightRight.greenPin = GREEN_PIN_RIGHT;
  lightRight.bluePin = BLUE_PIN_RIGHT;

  configureLightState(&lightLeft);
  configureLightState(&lightRight);

  colorTest();
  //

  // Configure MQTT
  myESP.addSubscription(lightTopic);
  myESP.setCallback(callbackMQTT);

  // startup the Wi-Fi and web server
  startWifi();

  // setup the http server and config page (fillConfig will
  // take the netInfo file and use that for default values)
  webConfig.fillConfig(&config);
  webConfig.begin(config.hostname);
  webConfig.setSpiffsReset("/reset");

  ///
  Serial.println("Leaving setup");  // Serial debug prints

}


void loop() {
  manageESPHelper(myESP.loop());
  processLight();
  yield();
  turnOffStatusLEDifTimedOut();
}

void turnOffStatusLEDifTimedOut() {
    if (ledTimedOut) return;
    if (ledTimeout.check()) {
        showStatus(CRGB::White);
        delay(30);  // No long delays to keep Wi-Fi and MQTT connected
        showStatus(CRGB::Black);
        ledTimedOut = true;
    }
}

/////////////////////// Network related

// ESPHelper & config setup and runtime handler functions
void manageESPHelper(int wifiStatus) {
  // if the unit is broadcasting or connected to Wi-Fi then reset the timeout vars
  if (wifiStatus == BROADCAST || wifiStatus >= WIFI_ONLY) {
    connectTimeout.reset();
    timeout = false;
  }
  // otherwise check for a timeout condition and handle setting up broadcast
  else if (wifiStatus < WIFI_ONLY) {
    checkForWifiTimeout();
  }
  // handle saving a new network config
  if (webConfig.handle()) {
    // Turn off status LED
    showStatus(CRGB::Black);
    Serial.println("Saving new network config and restarting...");  // Serial debug prints
    myESP.saveConfigFile(webConfig.getConfig(), NET_CONFIG_FILE);
    delay(1000);
    ESP.restart();
  }
}


// attempt to load a network configuration from the filesystem
void loadConfig() {
  // check for a good config file and start ESPHelper with the file stored on the ESP
  if (ESPHelperFS::begin()) {
    Serial.println("Filesystem loaded - Loading Config");  // Serial debug prints
    if (ESPHelperFS::validateConfig(NET_CONFIG_FILE) == GOOD_CONFIG) {
      Serial.println("Config loaded");  // Serial debug prints
      myESP.begin(NET_CONFIG_FILE);
    } else {
      // if no good config can be loaded (no file/corruption/etc.) 
      // then attempt to generate a new config and restart the module
      Serial.println("Could not load config - saving new config from default values and restarting");  // Serial debug prints
      delay(10);
      ESPHelperFS::createConfig(&homeNet, NET_CONFIG_FILE);
      ESPHelperFS::end();
      ESP.restart();
    }
  } else {
    // if the filesystem cannot be started, just fail over
    // to the built in network config hardcoded in here
    // Serial.println("Could not load filesystem, proceeding with default config values");  // Serial debug prints
    delay(10);
    myESP.begin(&homeNet);
  }
  // load the netInfo from ESPHelper for use in the config page
  config = myESP.getNetInfo();
}


void startWifi() {
  // if ok, config file will be filled
  loadConfig();

  // setup other ESPHelper info and enable OTA updates
  myESP.setHopping(false);
  myESP.OTA_setPassword(config.otaPassword);
  myESP.OTA_setHostnameWithVersion(config.hostname);
  myESP.OTA_enable();

  Serial.println("Connecting to network");  // Serial debug prints
  // connect to Wi-Fi before proceeding.
  // If cannot connect then switch to AP mode and create a network to config from
  while (myESP.loop() < WIFI_ONLY) {
    checkForWifiTimeout();
    if (timeout) return;
    delay(10);
    yield();
  }

  showStatus(CRGB::Green);
  ledTimeout.reset();
  Serial.println("Sucess!");  // Serial debug prints
  Serial.println(String("To connect to this device go to " + String(myESP.getIP())));  // Serial debug prints
  sendStatus("Starting with IP " + myESP.getIP());  // Serial debug prints
}

// function that checks for no network connection for a period of time 
// and starting up AP mode when that time has elapsed
void checkForWifiTimeout() {
  if (connectTimeout.check() && !timeout) {
      showStatus(CRGB::Purple);
      Serial.println("Network Connection timeout - starting broadcast (AP) mode...");  // Serial debug prints
      timeout = true;
      myESP.broadcastMode(broadcastSSID, broadcastPASS, broadcastIP);
    }
}

/////////////////////////////////////////////////////////////////////////


////// Service. Light control


void processLight() {
  processSideLight(&lightLeft);
  processSideLight(&lightRight);
};


void processSideLight(struct lightState *lt) {
  if (lt->mode == MANUAL) {
    manualLight(lt);
  } else if (lt->mode == GLEAM) {
    gleamLight(lt);
  };
};


void colorTest() {
  Serial.println("Doing colorTest");  // Serial debug prints

  showColor(&lightLeft, 255, 0, 0);
  showColor(&lightRight, 255, 0, 0);
  delay(500);
  showColor(&lightLeft, 0, 255, 0);
  showColor(&lightRight, 0, 255, 0);
  delay(500);
  showColor(&lightLeft, 0, 0, 255);
  showColor(&lightRight, 0, 0, 255);
  delay(500);
  showColor(&lightLeft, 255, 255, 255);
  showColor(&lightRight, 255, 255, 255);
  delay(500);
  showColor(&lightLeft, 0, 0, 0);
  showColor(&lightRight, 0, 0, 0);

  Serial.println("Finished colorTest");  // Serial debug prints
};


//// Manual settings

void manualLight(struct lightState *lt) {
  if (lt->lightStepMetro.check() == 0) return;

  if (lt->red != lt->redTagret ||
      lt->green != lt->greenTagret ||
      lt->blue != lt->blueTagret) lt->lightStep++;
  else return;

  /// Only copy-pase :(

  // Check RED
  if ((lt->red != lt->redTagret) && (lt->lightStep % lt->redStep == 0)) {
    if (lt->red > lt->redTagret)
      lt->red--;
    else
      lt->red++;
  }
  // Check GREEN
  if ((lt->green != lt->greenTagret) && (lt->lightStep % lt->greenStep == 0)) {
    if (lt->green > lt->greenTagret)
      lt->green--;
    else
      lt->green++;
  }
  // Check BLUE
  if ((lt->blue != lt->blueTagret) && (lt->lightStep % lt->blueStep == 0)) {
    if (lt->blue > lt->blueTagret)
      lt->blue--;
    else
      lt->blue++;
  }

  /// ^

  applyLight(lt);
};

//

//// Gleam

void gleamLight(struct lightState *lt) {
  if (lt->gleamMetro.check() == 0) return;

  if (lt->gleamStep >= MAX_STEPS) {
    lt->gleamStep = 0;
  };
  lt->gleamStep++;
  gleamRgb(lt);
};


void gleamRgb(struct lightState *lt) {

  int rgb[3] = {0, 0, 0};
  const int index = lt->gleamStep / RGB_MAX;
  const int _mod = lt->gleamStep % RGB_MAX;
  const int _nxt = index + 1;
  const int next = (_nxt < ARR_LEN) ? _nxt : 0;

  for (int i = 0; i < 3; i++) {
    const int section = rgbRainbowMap[index][i];
    const int nextSection = rgbRainbowMap[next][i];
    if (section == nextSection)
      rgb[i] = section * RGB_MAX;
    else if (section > nextSection)
      rgb[i] = RGB_MAX - _mod;
    else
      rgb[i] = _mod;
  };

  lt->red = rgb[0];
  lt->green = rgb[1];
  lt->blue = rgb[2];
  applyLight(lt);
};
//

void showColor(struct lightState *lt, int red, int green, int blue) {
  lt->red = red;
  lt->green = green;
  lt->blue = blue;
  applyLight(lt);
};


void applyLight(struct lightState *lt) {
  analogWrite(lt->redPin, lt->red);
  analogWrite(lt->greenPin, lt->green);
  analogWrite(lt->bluePin, lt->blue);
}


// MQTT ----

void callbackMQTT(char* topic, byte* payload, unsigned int length) {

  char newPayload[42];
  memcpy(newPayload, payload, length);
  newPayload[length] = '\0';

  Serial.println("MQTT Message arrived: " + String(newPayload));  // Serial debug prints

  strcpy(statusString, newPayload);
  myESP.publish(statusTopic, statusString);
}

