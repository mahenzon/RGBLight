let newRGBLight = require('./RGBLight_helper').newRGBLight;

// ********************* SETUP YOUR ACCESSORY

// This value changes for every independent controller
let mqttLightTopic = '/home/RGBLight02';  // MQTT topic to publish updates

// This value changes for each side of each controller
const currentSide = 'l';  // l for left, r for right

// These two values have to be unique for each accessory!
const macAddr = 'A3:D0:81:F4:BA:9A';
const accessoryName = '2 Left';

// ************* END SETUP


// function(name, mac, currentSide, mqttLightTopic)
let RGBLight = newRGBLight(accessoryName, macAddr, currentSide, mqttLightTopic);

// export HomeKit Accessory
exports.accessory = RGBLight.lightAccessory;
