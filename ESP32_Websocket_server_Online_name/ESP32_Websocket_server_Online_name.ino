/*********
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Complete project details at https://RandomNerdTutorials.com/esp32-websocket-server-arduino/
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/


/***
  Acobe/lttd - Class 1ELF Rud VGS 2026
  This code has been heavily modified from its original.
  Changes:
  - Modified the code to send json between the server and client enabling multiple variables to be sent instead of just a singular variable.
  - Added code to display information to an external OLED display.
  - Added code to read and process a potentiometer reading.
  - Added non blocking delays.
  - Modified the Javascript to enable JSON variables to be read and processed.
  - Modified the HTML to display multiple variables instead of just "ON" and "OFF".
    - Added "PIR Delay" which displays the processed value of the potentiometer.
    - Added "Last Activated" which dsiplays in second the last activation of the pir sensor or last manual activation.
    - Added "PIR State" which displays the state of the pir sensor, active or innactive.
    - Added "Pir Override" which displays and also overides the pir sensor if manual activation is detected.
  - Modified the wifi code so that the ESP32 is its own access point instead of connecting to an external network.
***/

// Import required libraries

// AdaFruit OLED Libaries
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// Load Wire library for I2C serial commuications.
#include <Wire.h>

// Load Wifi and ESPAsyncWebServer libraries for the webserver and websockets
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

//Oled defines
#define screenWidth 128
#define screenHeight 64

#define oledAdress 0x3C

#define oledReset -1

Adafruit_SSD1306 display(screenWidth, screenHeight, &Wire, oledReset);

// Network credentials for the ESP32 AP.
const char *ssid = "ESP32-S3-Relay";
const char *password = "IamBigStrongPassword2";

// Defines the boolean values used later.

bool pirState = 0;      // Used to display the state of the PIR sensor.
bool prevPirState = 0;  // Previous Pir status.
bool pirOverride = 0;   // Used to for the user to override the PIR sensor status. IE turn off a light while still in the room.
bool relayState = 0;    // Used for the relay state.
bool wsConnected = 0;   // Used to check if the websocket connected or not.

bool requireWebserver = false;  // Put this to true if you want to have the code running even though nobody is connected to the webserver.

// Defines the pins for the diffrent components connected to the ESP32-S3

const int relayPin = 3;  //GPIO 3
const int PIR = 2;       //GPIO 2
const int pot = 9;       //GPIO 9

// Millis for non-blocking delays.

struct {
  unsigned long currentTime;
  unsigned long pastMain;
  unsigned long pastPir;
  unsigned long pastPirDelay;
  unsigned long pastSerialMessage;
  unsigned long pastOverride;
} t = { 0, 0, 0, 0, 0 };

uint32_t displayDelay = 100;            // 100 millisec delay
uint32_t pirRefreshDelay = 1000;        // 1 sec delay
uint32_t serialMessageInterval = 1000;  // 2 Sec intervals.
uint32_t startDelayTime = 10000;        // 5 Secs start delay if enabled.
uint32_t overrideDelay = 5 * 60000;     // 5 mins delay to disable the override.

uint32_t pirDelay = 0;  // Used as a variable to update how long it takes for the light to turn off automatically after the PIR has stopped detecting.

//The delays determine how often the display and pirSensor status is refreshed.


// Makes a global json variable which can be read any time.
char globalJson[256];


// This is used to tell the code how many times to sample the potentiometer before outputting the average. (taken from arduino example code.)
const uint8_t potReadings = 30;
uint32_t readings[potReadings];
uint16_t readIndex = 0;
uint32_t total = 0;



// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char IPConfigMessage[] = R"rawr(Screen on IP:
%d.%d.%d.%d
SSID:
%s
Password
%s)rawr";


const char SerialMessage[] = R"rawr(
Display updated and pot read with value %u and pir delay is at %u Milliseconds or %u mins
Time since last activation: %u secs.)rawr";


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Relay Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  h1 {
    font-size: 1.8rem;
    color: white;
  }
  h2{
    font-size: 1.5rem;
    font-weight: bold;
    color: #143642;
  }
  .topnav {
    overflow: hidden;
    background-color: #143642;
  }
  body {
    margin: 0;
  }
  .content {
    padding: 30px;
    max-width: 600px;
    margin: 0 auto;
  }
  .card {
    background-color: #F8F7F9;;
    box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
    padding-top:10px;
    padding-bottom:20px;
  }
  .button {
    padding: 15px 50px;
    font-size: 24px;
    text-align: center;
    outline: none;
    color: #fff;
    background-color: #0f8b8d;
    border: none;
    border-radius: 5px;
    -webkit-touch-callout: none;
    -webkit-user-select: none;
    -khtml-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;
    user-select: none;
    -webkit-tap-highlight-color: rgba(0,0,0,0);
   }
   .button:hover {background-color: #1a6a6b}
   .button:active {
     background-color: #0f8b8d;
     box-shadow: 2 2px #CDCDCD;
     transform: translateY(2px);
   }
   .state {
     font-size: 1.5rem;
     color:#8c8c8c;
     font-weight: bold;
   }
  </style>
<title>ESP32 Relay Web Server</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
</head>
<body>
  <div class="topnav">
    <h1>ESP32 Controlled Relay With Motion Detection</h1>
  </div>
  <div class="content">
    <div class="card">
      <h2>Relay State</h2>
      <p class="state">State: <span id="state">Error no connection</span></p>
      <h2>PIR Sensor Input</h2>
      <p class="state">PIR State: <span id="pirSens">Error no connection</span></p>
      <p class="state">Last Activated: <span id="pirTime">Error no connection</span></p>
      <p class="state">PIR Override: <span id="pirOverride">Error no connection</span></p>
      <h2>PIR Turn off delay</h2>
      <p class="state">Turn Off Delay: <span id="delay"Error no connection</span></p>
      <p><button id="button" class="button">Toggle</button></p>
    </div>
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }
  function onOpen(event) {
    console.log('Connection opened');
  }
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  // Function under here replaces all the placeholder values for the html code with actual values sent from the ESP32 via json.
  function onMessage(event) {
    const thingThing = JSON.parse(event.data);
    var state;
    if (thingThing.state){
      state = "ON";
    }
    else{
      state = "OFF";
    }

    document.getElementById('delay').innerText = thingThing.delay + " min";
    console.log('Data Recieved Delay: ', thingThing.delay);

    var pirState
    if (thingThing.pirState){
      pirState = "Active";
    }
    else{
      pirState = "Innactive";
    }

    document.getElementById('pirSens').innerText = pirState;
    console.log('Data Recieved PIR: ', thingThing.pirState);

    var pirOverride
    if (thingThing.pirOverride){
      pirOverride = "Enabled";
    }
    else{
      pirOverride = "Disabled";
    }
    document.getElementById('pirOverride').innerText = pirOverride;
    console.log('Data Recieved pirOverride: ', thingThing.pirOverride);

    document.getElementById('pirTime').innerText = thingThing.pirTime + " secs";
    console.log('Data Recieved pirTime: ', thingThing.pirTime);
    
    
    document.getElementById('state').innerText = state;
    console.log('Data Recieved Relay: ', thingThing.state);
  }
  function onLoad(event) {
    initWebSocket();
    initButton();
  }
  function initButton() {
    document.getElementById('button').addEventListener('click', toggle);
  }
  function toggle(){
    websocket.send('toggle');
    console.log('Button pressed');
  }
</script>
</body>
</html>
)rawliteral";

// Websocket setup code underneath.

// This is the handler for event information. It detects when the client has pressed the button.

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char *)data, "toggle") == 0) {
      pirOverride = true;
      relayState = !relayState;
      t.pastPirDelay = t.currentTime;
    }
  }
}

// Webhook code which serial prints when clients. I dont have a full understanding of this as this is from the original code.

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("\nWebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      wsConnected = 1;
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("\nWebSocket client #%u disconnected\n", client->id());
      wsConnected = 0;
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// Initialises the websocket

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}


// Initialises Json

void sendStat(bool state1, bool state2, bool state3, uint16_t state4, long state5) {
  JsonDocument doc;
  doc["state"] = state1;
  doc["pirState"] = state2;
  doc["pirOverride"] = state3;
  doc["delay"] = state4;
  doc["pirTime"] = state5;
  serializeJson(doc, globalJson, sizeof(globalJson));
  send(doc);
}

// Sends the json information to the server. This code snippit taken from offical ESP32AsyncWebServer example code.

void send(JsonDocument &doc) {
  const size_t len = measureJson(doc);

  // original API from me-no-dev
  AsyncWebSocketMessageBuffer *buffer = ws.makeBuffer(len);
  assert(buffer);  // up to you to keep or remove this
  serializeJson(doc, buffer->get(), len);
  ws.textAll(buffer);
}

// Non websocket code.

// Function to send information to serial monitor.

void serialUpdate() {
  char buf[256];
  sprintf(buf, SerialMessage, pirDelay / 60000, pirDelay, pirDelay / 60000, (t.currentTime - t.pastPirDelay) / 1000);
  Serial.println(buf);
  Serial.println("\nNotified clients with new json:");
  Serial.println(globalJson);
}


// Function to display the IP address, SSID and WiFi password on the OLED display.


void displayIP() {
  char buf[128];  // Creates a 128 byte temporary buffer where it later loads in the string to be printed out via the display and serial monitor.
  IPAddress WiFiIP = WiFi.softAPIP();
  sprintf(buf, IPConfigMessage, WiFiIP[0], WiFiIP[1], WiFiIP[2], WiFiIP[3], ssid, password);  // Loads the dynamic variables such as IP address, SSID and Password into the buffer and converts them into a string using the IPConfigMessage template.
  Serial.println("\nDisplayIP called. Displaying IP and AP information on screen as well as serial monitor.");
  Serial.println(buf);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(buf);
  display.display();
}

// Function used to display the state of the relay.

void displayState(bool x, bool pirS, uint8_t potentiometer) {

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 2);
  display.println("Relay 1 Pir Status");
  display.setCursor(8, 20);
  display.print("Relay Shutoff Delay");
  display.setCursor(5, 35);
  display.setTextSize(3);
  display.print(potentiometer);
  display.setCursor(70, 35);
  display.print("min");

  // The code under here is for all the squares and things for the display. Basically just layout and making it pretty.
  display.drawRect(0, 14, screenWidth, screenHeight - 14, WHITE);
  display.drawRect(0, 0, screenWidth - 75, 14, WHITE);
  display.drawRect(screenWidth - 75, 0, 75, 14, WHITE);

  // Code under here is to dynamically change a circle to be filled or not filled depending on the state of the relay or pir sensor. Basically also just to make the thing prettier while still displaying useful information.

  if (x) {
    display.fillCircle(5, 5, 3, WHITE);
  } else {
    display.drawCircle(5, 5, 3, WHITE);
  }
  if (pirS) {
    display.fillCircle(screenWidth - 6, 5, 3, WHITE);
  } else {
    display.drawCircle(screenWidth - 6, 5, 3, WHITE);
  }
}

// Reads the potentiometer and averages out the output of the pot.

uint32_t readPot() {

  total = total - readings[readIndex];                          // Averaging code taken from https://docs.arduino.cc/built-in-examples/analog/Smoothing/
  readings[readIndex] = map(analogRead(pot), 0, 4095, 1, 120);  // The code is in the public domain.
  total = total + readings[readIndex];
  readIndex = readIndex + 1;
  if (readIndex >= potReadings) {
    readIndex = 0;
  }

  uint32_t average = total / potReadings;

  return average;
}

bool readPir() {  // reads the pir and then just returns the value. To be honest no idea why this had to be its own function, but here it is.
  bool statePir = digitalRead(PIR);
  return statePir;
}

void signature() {
  Serial.print(R"rawr(


Signature
Original code by Rui Santos & Sara Santos at Random Nerd Tutorials (https://RandomNerdTutorials.com/esp32-websocket-server-arduino/)

Heavily modified by Acobe/lttd 1ELF(2026) at Rud videregående skole.)rawr");
  Serial.flush();
  Serial.print(R"rawr(
Contact: lttd@outlook.com

Entire code and documentation as well as project files are hosted on https://github.com/AcobeLttd/-1ELF-ESP32-S3-Controlled-relay-V2

)rawr");
  Serial.flush();
  Serial.print(R"rawr(
License:

MIT License

Copyright (c) 2026 Acobe

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.




)rawr");
  Serial.flush();
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  // Initialises the OLED display on the I2C bus. (Code from adafruit)

  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAdress)) {

    Serial.println(F("SSD1306 allocation failed"));

    for (;;)
      ;  // Don't proceed, loop forever
  }

  // Initialises all the I/0

  pinMode(PIR, INPUT);
  pinMode(pot, INPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  // Sets all the numbers in the readings array to 0.

  for (uint8_t i = 0; i < potReadings; i++) {
    readings[i] = 0;
  }

  // Starts the WiFI AP
  WiFi.softAP(ssid, password);



  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  // Start server
  server.begin();
  // Displays IP information
  displayIP(); 

  delay(5000);  // These are the ONLY code blocking delays in the entire code and just ensures the signature gets sent through serial monitor.
  // Sends the signature and license at the end of the setup.
  signature();
  // Displays IP information again after the delay in case the serial monitor IP information didnt get sent properly.
  displayIP(); 


  delay(10000);

  

  // Fixes the timings as a result of the delay.
  t.pastMain = millis();
  t.pastPir = millis();
  t.pastPirDelay = millis();
  t.pastSerialMessage = millis();
  t.pastOverride = millis();
}

void loop() {
  digitalWrite(relayPin, relayState);                                           // Turns on or off the relay pin
  t.currentTime = millis();                                                     // Sets the currenTime variable in the t structure to the current millis time.
  if (t.currentTime >= startDelayTime && (wsConnected || !requireWebserver)) {  // Checks if webhook is active and if the requireWebserver vairable is false. If webhook is active it runs the main loop after waiting on the timer to run out. If requireWebserver variable is false then it starts immediately after the timer has run out.

    uint32_t potValue = readPot();  // Calls the readPot() function which returns the processed value of the potentiometer and updates that to potValue.
    pirDelay = potValue * 60000;    // Takes pot value and times 60000 to convert minutes into milliseconds which the ESP32 can use to calclate the time.
                                    // It does this constantly due to the time it takes for the pot to get an average signal and calling it every 100ms caused a large amount of lag

    if (t.currentTime - t.pastMain >= displayDelay) {  // Non-blocking delay to refresh the display and send info to the client website.

      displayState(relayState, pirState, potValue);                                                    // Updates the display with the newest values.
      sendStat(relayState, pirState, pirOverride, potValue, (t.currentTime - t.pastPirDelay) / 1000);  // Sends updated values to be processed and sent as a json file to the clients.

      t.pastMain = t.currentTime;  // Updates the past time for the non-blocking delay.
    }
    if (t.currentTime - t.pastPir >= pirRefreshDelay) {  // Non-blocking delay to refresh the pir sensor.

      pirState = readPir();  // Calls the readPir() function to read the status of the pir pin.

      if (pirState && !pirOverride) {  // This if statement toggles the LED on if the pir sensor is active and the pirOveride is disabled.

        relayState = true;
        prevPirState = pirState;  // Sets the previous pir state to current pir state

        t.pastPirDelay = t.currentTime;

      } else if (t.currentTime - t.pastOverride >= pirDelay && !prevPirState && pirState) {  // Resets the pirOverride after the timer has gone and if the previous pir state was innactive and the current pir state is active.

        pirOverride = false;
        prevPirState = pirState;

        t.pastOverride = t.currentTime;
      }
      if (t.currentTime - t.pastPirDelay >= pirDelay && relayState) {  // Turns off the light after the set amount of delay

        relayState = false;
      }

      prevPirState = pirState;
    }
    if (t.currentTime - t.pastSerialMessage >= serialMessageInterval) {  // The serial message intervals.
      serialUpdate();                                                    // calls the SerialUpdate function to update variables in serial monitor for debugging.
    }
  } else if (t.currentTime - t.pastSerialMessage >= serialMessageInterval) {  // Prints an error message to serial every 2 seconds if requireWebserver is true.
    char buf[128];
    sprintf(buf, "\n\n\nNo websocket connected, code innactive. Sent at %ld millis.", t.currentTime);
    Serial.print(buf);
    displayIP();  // Calls the display IP function to print the IP and WiFi AP info in terminal as well as on the OLED.

    relayState = false;
    pirState = false;
  }


  if (t.currentTime - t.pastSerialMessage >= serialMessageInterval) t.pastSerialMessage = t.currentTime;  // Updates the past serial message.

  display.display();    // Displays the newest information
  ws.cleanupClients();  // Cleans up the webhooks, otherwise memory usage builds up and things start to break. (Part of original code.)
}