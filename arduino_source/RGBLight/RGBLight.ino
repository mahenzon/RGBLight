#include <Metro.h>
#include <FastLED.h>
#include <ESPHelper.h>
#include <ESPHelperFS.h>
#include <ESPHelperWebConfig.h>


#define NET_CONFIG_FILE "/netConfig.json"


netInfo config;
ESPHelper myESP;


ESPHelperWebConfig webConfig;

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


// timeout before starting AP mode for configuration
Metro connectTimeout = Metro(20000);
bool timeout = false;

// AP moade setup info
const char* broadcastSSID = "ESP-Hotspot";
const char* broadcastPASS = "";
IPAddress broadcastIP = {192, 168, 1, 1};



void setup() {
  Serial.begin(115200);  // Serial debug prints

  // print some debug
  Serial.println("");  // Serial debug prints
  Serial.println("Starting Up - Please Wait...");  // Serial debug prints
  delay(100);

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
  yield();
}


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
  if (webConfig.handle()){
    Serial.println("Saving new network config and restarting...");  // Serial debug prints
    myESP.saveConfigFile(webConfig.getConfig(), NET_CONFIG_FILE);
    delay(500);
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

  Serial.println("Sucess!");  // Serial debug prints
  Serial.println(String("To connect to this device go to " + String(myESP.getIP())));  // Serial debug prints
}

// function that checks for no network connection for a period of time 
// and starting up AP mode when that time has elapsed
void checkForWifiTimeout() {
  if (connectTimeout.check() && !timeout) {
      Serial.println("Network Connection timeout - starting broadcast (AP) mode...");  // Serial debug prints
      timeout = true;
      myESP.broadcastMode(broadcastSSID, broadcastPASS, broadcastIP);
    }
}

