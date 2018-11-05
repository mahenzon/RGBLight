var newRGBLight = require('./RGBLight_helper').newRGBLight;

// ********************* SETUP YOUR ACCESSORY

// This value changes for every independent controller
var mqttLightTopic = '/home/RGBLight01';  // MQTT topic to publish updates

// This value changes for each side of each controller
const currentSide = 'r';  // l for left, r for right

// These two values have to be unique for each accessory!
const macAddr = 'A2:D0:81:F4:BA:9A';
const accessoryName = '1 Right';

// ************* END SETUP


// function(name, mac, currentSide, mqttLightTopic)
var RGBLight = newRGBLight(accessoryName, macAddr, currentSide, mqttLightTopic);

// export HomeKit Accessory
exports.accessory = RGBLight.lightAccessory;
