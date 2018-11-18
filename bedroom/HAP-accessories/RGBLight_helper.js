let Accessory = require('../').Accessory;
let Service = require('../').Service;
let Characteristic = require('../').Characteristic;
let uuid = require('../').uuid;

// let mqtt = require('mqtt');
let mqtt = require('async-mqtt');

let options = {
  host: 'localhost',  // your MQTT broker server IP
  port: 1883,
  clientId: 'RGBLight'  // name to be introduced for MQTT broker
};

let client = mqtt.connect(options);

// __INT_MAX__ 2147483647: https://github.com/esp8266/Arduino/blob/8ae0746e4aeaf7c2a8881831f370b40347e47a50/tools/sdk/libc/xtensa-lx106-elf/include/limits.h#L66
const maxGleamRate = 2353;  // 200: 5 minutes, 2353: 1 hour


const subtypeGleam = 'GLEAM';
const subtypeLight = 'LIGHT';


function adaptGleamRate(inputRate) {
  let minFrom, maxFrom, minTo, maxTo;
  if (inputRate > 40) {
    minFrom = 41;
    maxFrom = 100;
    minTo = 501;
    maxTo = maxGleamRate;
  } else if (inputRate > 30) {
    minFrom = 31;
    maxFrom = 40;
    minTo = 301;
    maxTo = 500;
  } else if (inputRate > 20) {
    minFrom = 21;
    maxFrom = 30;
    minTo = 101;
    maxTo = 300;
  } else if (inputRate > 10) {
    minFrom = 11;
    maxFrom = 20;
    minTo = 11;
    maxTo = 100;
  } else {  // inputRate from 1 to 10
    return inputRate;
  }
  return mapRange(inputRate, minFrom, maxFrom, minTo, maxTo);
}

function mapRange(value, low1, high1, low2, high2) {
  return Math.round(low2 + (high2 - low2) * (value - low1) / (high1 - low1));
}

// function to format numbers with fixed length (3) to make it easy to parse on ESP8266
function formatNumberLength(num) {
  let l = '' + num;
  while (l.length < 3) {
      l = '0' + l;
  };
  return l;
}

function HSVtoRGB(h, s, v) {
  s /= 100; v /= 100;
  let r, g, b, i, f, p, q, t;

  h /= 60;
  i = Math.floor(h);
  f = h - i;

  p = v * (1 - s);
  q = v * (1 - f * s);
  t = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    case 5: r = v, g = p, b = q; break;
  }
  return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)]
}


function newHomeKitAccessoryFromRGBLight(RGBLight) {
  // HomeKit Accessory:

  // Generate a consistent UUID for our light Accessory that will remain the same even when
  // restarting our server. We use the `uuid.generate` helper function to create a deterministic
  // UUID based on an arbitrary "namespace" and the word "light".
  let lightUUID = uuid.generate('hap-nodejs:accessories:light' + RGBLight.username);

  // This is the Accessory that we'll return to HAP-NodeJS that represents our light.
  let lightAccessory = new Accessory(RGBLight.name, lightUUID);

  // Add properties for publishing (in case we're using Core.js and not BridgedCore.js)
  lightAccessory.username = RGBLight.username;
  lightAccessory.pincode = RGBLight.pincode;

  // set some basic properties (these values are arbitrary and setting them is optional)
  lightAccessory
    .getService(Service.AccessoryInformation)
      .setCharacteristic(Characteristic.Manufacturer, RGBLight.manufacturer)
      .setCharacteristic(Characteristic.Model, RGBLight.model)
      .setCharacteristic(Characteristic.SerialNumber, RGBLight.serialNumber)
      .setCharacteristic(Characteristic.FirmwareRevision, RGBLight.firmwareRevision);

  // listen for the "identify" event for this Accessory
  lightAccessory.on('identify', function(paired, callback) {
    RGBLight.identify();
    callback();
  });


  // Add LIGHT service &&&&

  // Add the actual Lightbulb Service and listen for change events from iOS.
  // We can see the complete list of Services and Characteristics in `lib/gen/HomeKitTypes.js`
  lightAccessory
    .addService(Service.Lightbulb, RGBLight.name, subtypeLight) // services exposed to the user should have "names" like "Light" for this case
    .getCharacteristic(Characteristic.On)
    .on('set', function(value, callback) {
      RGBLight.setLightPower(value);

      // Our light is synchronous - this value has been successfully set
      // Invoke the callback when you finished processing the request
      // If it's going to take more than 1s to finish the request, try to invoke the callback
      // after getting the request instead of after finishing it. This avoids blocking other
      // requests from HomeKit.
      callback();
    })
    // We want to intercept requests for our current power state so we can query the hardware itself instead of
    // allowing HAP-NodeJS to return the cached Characteristic.value.
    .on('get', function(callback) {
      callback(null, RGBLight.getLightPower());
    });


  // also add an "optional" Characteristic for Hue
  lightAccessory
    .getService(subtypeLight)
    .addCharacteristic(Characteristic.Hue)
    .on('set', function(value, callback) {
      RGBLight.setHue(value);
      callback();
    })
    .on('get', function(callback) {
      callback(null, RGBLight.getHue());
    });

  // also add an "optional" Characteristic for Saturation
  lightAccessory
    .getService(subtypeLight)
    .addCharacteristic(Characteristic.Saturation)
    .on('set', function(value, callback) {
      RGBLight.setSaturation(value);
      callback();
    })
    .on('get', function(callback) {
      callback(null, RGBLight.getSaturation());
    });

  // also add an "optional" Characteristic for Brightness
  lightAccessory
    .getService(subtypeLight)
    .addCharacteristic(Characteristic.Brightness)
    .on('set', function(value, callback) {
      RGBLight.setBrightness(value);
      callback();
    })
    .on('get', function(callback) {
      callback(null, RGBLight.getBrightness());
    });

  ////// &


  // Add GLEAM service #####

  // Add another service, to separate them use subtype
  lightAccessory
    .addService(Service.Lightbulb, RGBLight.gleamName, subtypeGleam)
    .getCharacteristic(Characteristic.On)
    .on('set', function(value, callback) {
      RGBLight.setGleamPower(value);
      callback();
    })
    .on('get', function(callback) {
      callback(null, RGBLight.getGleamPower());
    });

  // Characteristic 'Brightness' - but actually it will change gleam rate
  lightAccessory
    .getService(subtypeGleam)
    .addCharacteristic(Characteristic.Brightness)
    .on('set', function(value, callback) {
      RGBLight.setGleamRate(value);
      callback();
    })
    .on('get', function(callback) {
      callback(null, RGBLight.getGleamRate());
    });

  ////// #

  return lightAccessory;
}

module.exports = {
  newRGBLight: function(name, mac, currentSide, mqttLightTopic) {
    let RGBLight = {
      name: 'RGB Light ' + name,  // Has to be unique!
      gleamName: 'RGB Gleam ' + name,
      pincode: '111-69-111',  // you can change numbers keeping '-' symbols
      username: mac,  // MAC like address used by HomeKit to differentiate accessories. Has to be unique in your net
      manufacturer: 'Suren Khorenyan',  // (optional)
      model: 'RGBLight_WebConfig',  // (optional)
      serialNumber: 'RGBLight 2 ' + name,  // (optional)
      firmwareRevision: '2.0',  // (optional)


      lightPower: false,  // current light (manual mode) power status
      gleamPower: false,  // current gleam power status

      currentHue: 0,
      currentSaturation: 0,
      currentBrightness: 100,

      newHue: 0,
      newSaturation: 0,
      newBrightness: 100,

      gleamRate: 10,

      // outputLogs: false,  // change to true for debug purposes
      mqttLightTopic: mqttLightTopic,
      currentSide: currentSide,
      lightAccessory: undefined,


      publish: function(command) {
        client.publish(this.mqttLightTopic, command);
        // if (this.outputLogs) console.log("Publishing", command, '...');
        // client.publish(mqttLightTopic, command).then(function(){
        //   if (this.outputLogs) console.log("Published", command, '!');
        // });
      },


      identify: function () {
        // if(this.outputLogs) console.log("Identify the '%s'", this.name);
        this.sendGleamRate(1);
        setTimeout(function() {
          this.publish('p' + this.currentSide + '0');  // turn off - power left 0
        }, 2000);
      },


      setLightPower: async function(status) {
        // if(this.outputLogs) console.log("Turning light on the '%s' %s", this.name, status ? "on" : "off");
        if (status == this.lightPower) return;
        this.lightPower = status;

        let state = 0;
        if (this.lightPower) {
          state = 1;
          if (this.gleamPower) {  // Check if gleam is on - then power off
            this.gleamPower = false;
            this.updateServicePower(subtypeGleam);
          }
        }
        this.updatePower(state, 'm');
      },
      getLightPower: function() {
        // if(this.outputLogs) console.log("'%s' light is %s.", this.name, this.power ? "on" : "off");
        return this.lightPower;
      },

      setGleamPower: async function(status) {
        // if(this.outputLogs) console.log("Turning gleam on the '%s' %s", this.name, status ? "on" : "off");
        if (status == this.gleamPower) return;
        this.gleamPower = status;

        let state = 0;
        if (this.gleamPower) {
          state = 1;
          if (this.lightPower) {  // Check if manual is on - then power off
            this.lightPower = false;
            this.updateServicePower(subtypeLight);
          }
        }
        this.updatePower(state, 'g');
      },
      getGleamPower: function() {
        // if(this.outputLogs) console.log("'%s' gleam is %s.", this.name, this.power ? "on" : "off");
        return this.gleamPower;
      },


      setHue: async function(hue) {
        // if(this.outputLogs) console.log("Setting '%s' hue to %s", this.name, hue);
        this.newHue = hue;
        this.updateLight();
      },
      getHue: function() {
        // if(this.outputLogs) console.log("'%s' hue is %s", this.name, this.currentHue);
        return this.currentHue;
      },

      setSaturation: async function(saturation) {
        // if(this.outputLogs) console.log("Setting '%s' saturation to %s", this.name, saturation);
        this.newSaturation = saturation;
        this.updateLight();
      },
      getSaturation: function() {
        // if(this.outputLogs) console.log("'%s' saturation is %s", this.name, this.currentSaturation);
        return this.currentSaturation;
      },

      setBrightness: async function(brightness) {
        // if(this.outputLogs) console.log("Setting '%s' brightness to %s", this.name, brightness);
        this.newBrightness = brightness;
        this.updateLight();
      },
      getBrightness: function() {
        // if(this.outputLogs) console.log("'%s' brightness is %s", this.name, this.currentBrightness);
        return this.currentBrightness;
      },

      setGleamRate: async function (rate) {
        // if(this.outputLogs) console.log("'%s' got gleam rate %s", this.name, rate);
        if (rate != this.gleamRate) {
          this.sendGleamRate(adaptGleamRate(rate));
          this.gleamRate = rate;
        }
      },
      getGleamRate: function () {
        // if(this.outputLogs) console.log("'%s' gleam rate is %s", this.name, this.currentGleamRate);
        return this.gleamRate;
      },


      sendGleamRate: function (rate) {
        this.publish('g' + this.currentSide + rate);
      },

      updatePower: function (state, mode) {
        this.publish('p' + this.currentSide + state + mode);
      },

      updateLight: function () {
        if (this.currentHue == this.newHue &&
            this.currentSaturation == this.newSaturation &&
            this.currentBrightness == this.newBrightness) return

        this.currentHue = this.newHue;
        this.currentSaturation = this.newSaturation;
        this.currentBrightness = this.newBrightness;

        if (!this.lightPower) {
          this.lightPower = true
          this.updateServicePower(subtypeLight, true);
          if (this.gleamPower) {
            this.gleamPower = false
            this.updateServicePower(subtypeGleam);
          }
        }

        let rgbArray = HSVtoRGB(this.currentHue, this.currentSaturation, this.currentBrightness);
        // if(this.outputLogs) console.log("Updating light with %s", rgbArray);
        for (let i = 0; i < rgbArray.length; i++) {
          rgbArray[i] = formatNumberLength(rgbArray[i]);
        }
        this.publish('c' + this.currentSide + rgbArray.join('|'));
      },

      updateServicePower: function(service, state) {
        this.lightAccessory
          .getService(service)
          .getCharacteristic(Characteristic.On)
          .updateValue(!!state);
      }
    };
    RGBLight.lightAccessory = newHomeKitAccessoryFromRGBLight(RGBLight);
    return RGBLight;
  },
};