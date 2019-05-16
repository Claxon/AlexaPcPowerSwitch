// This sketch allows a Wemos D1 Mini ESP8266 board to turn a pc on and off using
// the fauxmo library (alowing control via amazon Echo etc)
// To use:
// - Connect the Pwr header of a pc motherboard to the pin specified by PIN_POWERSWITCH
//   I did this via an optocoupler.
// - Feed the Power LED header into the build to close a switch on pin PIN_POWERDETECTOR
//   Again I used an optocoupler. This is used to detect when power is being sent to the 
//   PC case power LED, and thus to determine whether the machine is powered on.

#include <Arduino.h>
#ifdef ESP32
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif
#include "fauxmoESP.h"

// Rename the credentials.sample.h file to credentials.h and 
// edit it according to your router configuration
#include "credentials.h"

fauxmoESP fauxmo;

// -----------------------------------------------------------------------------

#define SERIAL_BAUDRATE     115200

#define LED_INDICATOR       LED_BUILTIN
#define PIN_POWERDETECTOR   4 // Pin D2
#define PIN_POWERSWITCH     13 // Pin D7
#define ID_SERVER            "media server" // The name of your server eg. "Alexa turn on 'media server'"

// -----------------------------------------------------------------------------
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin
bool lastOnlineState = false;

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

// Triggers actions
bool powerUp;
bool powerDown;

// -----------------------------------------------------------------------------
// Wifi
// -----------------------------------------------------------------------------

void wifiSetup() {

    // Set WIFI module to STA mode
    WiFi.mode(WIFI_STA);

    // Connect
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Wait
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();

    // Connected!
    Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

}

/*
 * Returns true if the attached machine is powered on.
 */
bool isOnline()
{
  int reading = digitalRead(PIN_POWERDETECTOR);
  return !!reading;
}
bool callbackGetState(unsigned char device_id, const char * device_name)
{
  return isOnline();
}
void triggerPowerSwitch()
{
  // Triggers the LED and optocoupler for the specified duration
  // then turns them off again
  Serial.println("Power Pressed...");
  digitalWrite(LED_INDICATOR, LOW);
  digitalWrite(PIN_POWERSWITCH, LOW);
  delay(250);
  Serial.println("Power Released...");
  digitalWrite(LED_INDICATOR, HIGH);
  digitalWrite(PIN_POWERSWITCH, HIGH);
}

void setup() {
    powerUp = false;
    powerDown = false;
    
    // Init serial port and clean garbage
    Serial.begin(SERIAL_BAUDRATE);
     delay(10);
    Serial.println();
    Serial.println();

    // LEDs
    pinMode(LED_INDICATOR, OUTPUT);
    pinMode(PIN_POWERSWITCH, OUTPUT);
    pinMode(PIN_POWERDETECTOR, INPUT_PULLUP);
    digitalWrite(LED_INDICATOR, HIGH);
    digitalWrite(PIN_POWERSWITCH, HIGH);
    
    // Wifi
    wifiSetup();

    // By default, fauxmoESP creates it's own webserver on the defined port
    // The TCP port must be 80 for gen3 devices (default is 1901)
    // This has to be done before the call to enable()
    fauxmo.createServer(true); // not needed, this is the default value
    fauxmo.setPort(80); // This is required for gen3 devices

    // You have to call enable(true) once you have a WiFi connection
    // You can enable or disable the library at any moment
    // Disabling it will prevent the devices from being discovered and switched
    fauxmo.enable(true);

    // You can use different ways to invoke alexa to modify the devices state:
    // "Alexa, turn yellow lamp on"
    // "Alexa, turn on yellow lamp
    // "Alexa, set yellow lamp to fifty" (50 means 50% of brightness, note, this example does not use this functionality)

    // Add virtual devices
    //fauxmo.addDevice(ID_YELLOW);
    //fauxmo.addDevice(ID_GREEN);
    //fauxmo.addDevice(ID_BLUE);
    //fauxmo.addDevice(ID_PINK);
    ///fauxmo.addDevice(ID_WHITE);
    fauxmo.addDevice(ID_SERVER);

    fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
        
        // Callback when a command from Alexa is received. 
        // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
        // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
        // Just remember not to delay too much here, this is a callback, exit as soon as possible.
        // If you have to do something more involved here set a flag and process it in your main loop.
        
        Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);

        // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
        // Otherwise comparing the device_name is safer.

        if (strcmp(device_name, ID_SERVER)==0) {
            if (state)
            {
              powerUp = true;
            }
            else
            {
              powerDown = true;
            }
        } 

    });
}

void loop() {

    // fauxmoESP uses an async TCP server but a sync UDP server
    // Therefore, we have to manually poll for UDP packets
    fauxmo.handle();

    bool online = isOnline();
    
    if (powerUp)
    {
      powerUp = false;
      if(!online)
      {
        Serial.println("Powering up...");
        triggerPowerSwitch();
      }
      else
      {
        Serial.println("Attempting power up, but device is already on");
      }
    }
    
    if (powerDown)
    {
      powerDown = false;
      if(online)
      {
        Serial.println("Powering down...");
        triggerPowerSwitch();
      }
      else
      {
        Serial.println("Attempting power down, but device is already off");
      }
    }
    
    // This is a sample code to output free heap every 5 seconds
    // This is a cheap way to detect memory leaks
    static unsigned long last = millis();
    if (millis() - last > 2000) {
        last = millis();
        Serial.printf("[MAIN] isOnline: %s\n", online ? "TRUE" : "FALSE");
        Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
    }

    // If your device state is changed by any other means (MQTT, physical button,...)
    // you can instruct the library to report the new state to Alexa on next request:
    // fauxmo.setState(ID_YELLOW, true, 255);
    if (online != lastOnlineState)
    {
       fauxmo.setState(ID_SERVER, online, 255);
       lastOnlineState = online;
    }
}
