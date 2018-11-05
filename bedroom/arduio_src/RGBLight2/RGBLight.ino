#include <Metro.h>
#include <FastLED.h>
#include <ESPHelper.h>
#include <ESPHelperFS.h>
#include <ESPHelperWebConfig.h>

// Some configs
// #define DEBUG_ENABLED  // Uncomment to print debug info to console
#define DEFAULT_GLEAM_DELAY 200  // 200: 5 minutes  // 2353: 1 hour
#define WIFI_CONNECT_TIMEOUT 20000  // ms after enable AP mode
#define DISABLE_LED_ON_SUCCESS_AFTER 20000  // 20000ms = 20 seconds. Disable status LED
#define MAXIMUM_FADE_DURATION 1000
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
#define TOPIC "/home/RGBLight02"
#define STATUS TOPIC "/status"
#define STATUS_STR_LEN 50
//

//
#define NET_CONFIG_FILE "/netConfig.json"  // just don't change it

// Status LED related
#define NUM_LEDS 1
#define LED_PIN 13
#define COLOR_ORDER GRB
#define STATUS_LED_BRIGHTNESS 200  // from 1 to 255

// Constants for Gleam mode
#define ARR_LEN 6
#define RGB_MAX 255
#define MAX_GLEAM_STEPS ARR_LEN * RGB_MAX

int rgbRainbowMap[ARR_LEN][3] = {
  { 1, 0, 0 },
  { 1, 1, 0 },
  { 0, 1, 0 },
  { 0, 1, 1 },
  { 0, 0, 1 },
  { 1, 0, 1 },
};

//

CRGB led[NUM_LEDS];


// default net info for unconfigured devices
// this values will be used if nothing else is specified on web
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
bool ledTimedOut = true;


// timeout before starting AP mode for configuration
Metro connectTimeout = Metro(WIFI_CONNECT_TIMEOUT);
bool timeout = false;

// AP moade setup info
const char* broadcastSSID = "ESP-Hotspot";
const char* broadcastPASS = "";
IPAddress broadcastIP = {192, 168, 1, 1};
//

void configureLightState(struct lightState *lt) {
  // lt->mode = GLEAM;
  lt->mode = STANDBY;
  lt->fadePeriod = MAXIMUM_FADE_DURATION;
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
}

void showStatus(CRGB color) {
    led[0] = color;
    FastLED.show();
}

void setup() {

  #ifdef DEBUG_ENABLED
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Starting Up - Please Wait...");
  #endif

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
  #ifdef DEBUG_ENABLED
    Serial.println("Leaving setup");
  #endif


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
        delay(20);  // No long delays to keep Wi-Fi and MQTT connected
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

    #ifdef DEBUG_ENABLED
      Serial.println("Saving new network config and restarting...");
    #endif

    // Turn off status LED
    showStatus(CRGB::Black);

    myESP.saveConfigFile(webConfig.getConfig(), NET_CONFIG_FILE);
    delay(1000);
    ESP.restart();
  }
}


// attempt to load a network configuration from the filesystem
void loadConfig() {
  // check for a good config file and start ESPHelper with the file stored on the ESP
  if (ESPHelperFS::begin()) {

    #ifdef DEBUG_ENABLED
      Serial.println("Filesystem loaded - Loading Config");
    #endif

    if (ESPHelperFS::validateConfig(NET_CONFIG_FILE) == GOOD_CONFIG) {

      #ifdef DEBUG_ENABLED
        Serial.println("Config loaded");
      #endif

      myESP.begin(NET_CONFIG_FILE);
    } else {
      // if no good config can be loaded (no file/corruption/etc.)
      // then attempt to generate a new config and restart the module

      #ifdef DEBUG_ENABLED
        Serial.println("Could not load config - saving new config from default values and restarting");
      #endif

      delay(10);
      ESPHelperFS::createConfig(&homeNet, NET_CONFIG_FILE);
      ESPHelperFS::end();
      ESP.restart();
    }
  } else {
    // if the filesystem cannot be started, just fail over
    // to the built in network config hardcoded in here

    #ifdef DEBUG_ENABLED
      Serial.println("Could not load filesystem, proceeding with default config values");
    #endif

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

  showStatus(CRGB::DarkOrange);

  #ifdef DEBUG_ENABLED
    Serial.println("Connecting to network");
  #endif

  // connect to Wi-Fi before proceeding.
  // If cannot connect then switch to AP mode and create a network to config from
  connectTimeout.reset();
  while (myESP.loop() < WIFI_ONLY) {
    checkForWifiTimeout();
    if (timeout) return;
    delay(10);
    yield();
  }

  showStatus(CRGB::Green);
  ledTimedOut = false;
  ledTimeout.reset();

  #ifdef DEBUG_ENABLED
    Serial.println("Sucess!");
  #endif

  #ifdef DEBUG_ENABLED
    Serial.println(String("To configure this device go to " + String(myESP.getIP())));
  #endif

  sendStatus("Starting with IP " + myESP.getIP());
}

// function that checks for no network connection for a period of time
// and starting up AP mode when that time has elapsed
void checkForWifiTimeout() {
  if (connectTimeout.check() && !timeout) {

    #ifdef DEBUG_ENABLED
      Serial.println("Network Connection timeout - starting broadcast (AP) mode...");
    #endif

    showStatus(CRGB::Blue);
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
}


void colorTest() {

  #ifdef DEBUG_ENABLED
    Serial.println("Doing colorTest");
  #endif


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

  #ifdef DEBUG_ENABLED
    Serial.println("Finished colorTest");
  #endif

}


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
}

//

//// Gleam

void gleamLight(struct lightState *lt) {
  if (lt->gleamMetro.check() == 0) return;

  if (lt->gleamStep >= MAX_GLEAM_STEPS) {
    lt->gleamStep = 0;
  };
  gleamRgb(lt);
  lt->gleamStep++;
}


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

  #ifdef DEBUG_ENABLED
    // Beware of a lot of information on high gleam speed
    // Serial.print("On gleam step ");
    // Serial.print(lt->gleamStep);
    // Serial.print(" colors are: [");
    // Serial.print(rgb[0]);
    // Serial.print(", ");
    // Serial.print(rgb[1]);
    // Serial.print(", ");
    // Serial.print(rgb[2]);
    // Serial.println("].");
  #endif

  showColor(lt, rgb[0], rgb[1], rgb[2]);
}

//

void showColor(struct lightState *lt, int red, int green, int blue) {
  lt->red = red;
  lt->green = green;
  lt->blue = blue;
  applyLight(lt);
}


void applyLight(struct lightState *lt) {
  analogWrite(lt->redPin, lt->red);
  analogWrite(lt->greenPin, lt->green);
  analogWrite(lt->bluePin, lt->blue);
}


void prepareManualColorChange(struct lightState *lt, int redDiff, int greenDiff, int blueDiff) {
  lt->mode = MANUAL;

  // #ifdef DEBUG_ENABLED
  //   Serial.print("Preparing manual, got in: [");
  //   Serial.print(redDiff);
  //   Serial.print(", ");
  //   Serial.print(greenDiff);
  //   Serial.print(", ");
  //   Serial.print(blueDiff);
  //   Serial.println("].");
  // #endif

  if (redDiff > 0)
    lt->redStep = lt->fadePeriod / redDiff;
  if (greenDiff > 0)
    lt->greenStep = lt->fadePeriod / greenDiff;
  if (blueDiff > 0)
    lt->blueStep = lt->fadePeriod / blueDiff;

  // #ifdef DEBUG_ENABLED
  //   Serial.print("So now steps are: [");
  //   Serial.print(lt->redStep);
  //   Serial.print(", ");
  //   Serial.print(lt->greenStep);
  //   Serial.print(", ");
  //   Serial.print(lt->blueStep);
  //   Serial.println("].");
  // #endif

  lt->lightStep = 0;
  lt->lightStepMetro.reset();
}


// MQTT ----

void callbackMQTT(char* topic, byte* payload, unsigned int length) {

  char newPayload[42];
  memcpy(newPayload, payload, length);
  newPayload[length] = '\0';

  #ifdef DEBUG_ENABLED
    Serial.println("MQTT Message arrived: " + String(newPayload));
  #endif

  const char mode = payload[0];

  struct lightState *lt;

  if (payload[1] == 'l')
    lt = &lightLeft;
  else if (payload[1] == 'r')
    lt = &lightRight;
  else if (mode == 's') {  // 's' for sync
    // Check this only if left or right are not stated
    lightLeft.gleamStep = MAX_GLEAM_STEPS;
    lightRight.gleamStep = MAX_GLEAM_STEPS;
    lightLeft.gleamMetro.reset();
    lightRight.gleamMetro.reset();
  } else return;


  if (mode == 'c') {  // color

    #ifdef DEBUG_ENABLED
      Serial.println("Searching for numbers..");
    #endif

    // Get values from a line, for example "cr255|120|042"
    // This will be red=255, green=210, blue=42
    // and "cr" means `color` (mode) `right` 
    const int newRedTarget = atoi(&newPayload[2]);
    const int newGreenTarget = atoi(&newPayload[6]);
    const int newBlueTarget = atoi(&newPayload[10]);

    #ifdef DEBUG_ENABLED
      Serial.print("Fetched numbers: [");
      Serial.print(newRedTarget);
      Serial.print(", ");
      Serial.print(newGreenTarget);
      Serial.print(", ");
      Serial.print(newBlueTarget);
      Serial.println("].");
    #endif

    if (
      (lt->red != newRedTarget) ||
      (lt->green != newGreenTarget) ||
      (lt->blue != newBlueTarget)
      ) {

      #ifdef DEBUG_ENABLED
        Serial.println("Got new colors! Writing them down..");
      #endif

      lt->redTagret = newRedTarget;
      lt->greenTagret = newGreenTarget;
      lt->blueTagret = newBlueTarget;

      #ifdef DEBUG_ENABLED
        Serial.println("Preparing..");
      #endif

      prepareManualColorChange(
        lt,
        (abs(lt->redTagret - lt->red)),
        (abs(lt->greenTagret - lt->green)),
        (abs(lt->blueTagret - lt->blue))
      );

      #ifdef DEBUG_ENABLED
        Serial.println("Prepared, done.");
      #endif
    }

  } else if (mode == 'g') {  // gleam
    // Just for fun show Purple status LED for 2 seconds on gleam interval change
    showStatus(CRGB::Purple);
    ledTimedOut = false;
    ledTimeout.interval(2000);
    ledTimeout.reset();
    
    // message: "gl300" = gleam left 300 (refresh rate)
    const int newGleamInterval = atoi(&newPayload[2]);
    if (newGleamInterval > 0)
      lt->gleamMetro.interval(newGleamInterval);
    lt->gleamMetro.reset();
    lt->mode = GLEAM;

  } else if (mode == 'p') {  // power on|off
    // "pr1[g|m]" - power right 1 (on) gleam|manual
    // "pl0" - power left 0 (off) (any)
    if (payload[2] == '1') { // Power ON
      if (payload[3] == 'g') {
        gleamRgb(lt);  // Start Gleam mode instantly (turn on)
        lt->gleamMetro.reset();
        lt->mode = GLEAM;
      } else {
        prepareManualColorChange(
          lt,
          lt->redTagret,
          lt->greenTagret,
          lt->blueTagret
        );
      }
    } else {  // if == '0' or whatever - power OFF
      lt->mode = STANDBY;
      showColor(lt, 0, 0, 0);
    }
  };

  #ifdef DEBUG_ENABLED
    Serial.println("Leaving mode check, publishing..");

    strcpy(statusString, newPayload);
    myESP.publish(statusTopic, statusString);

    Serial.println("==OK Published!");
  #endif
}
