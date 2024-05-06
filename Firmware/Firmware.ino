/* Power Center uses: Sparkfun ESP32 Thing Plus C */


// Add buildflag ASYNCWEBSERVER_REGEX to enable the regex support

// For platformio: platformio.ini:
//  build_flags = 
//      -DASYNCWEBSERVER_REGEX

// For arduino IDE: create/update platform.local.txt
// Windows: C:\Users\(username)\AppData\Local\Arduino15\packages\espxxxx\hardware\espxxxx\{version}\platform.local.txt
// Linux: ~/.arduino15/packages/espxxxx/hardware/espxxxx/{version}/platform.local.txt
//
// compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1
//
// Restart the IDE after making changes to the platform.local.txt file


#include "config.h"

//Networking Includes
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <WiFiClient.h>
#include <Wire.h>

//EEPROM Includes
#include <Preferences.h>

//Qwiic i2c libraries
#include "SparkFun_Qwiic_Relay.h"


/*
 * Wifi credentials are stored in config.h file. See the template.
 *
const char* ssid = "";
const char* password = "";
*/

//Defines
#define FIRMWARE_VERSION "1.0.0"
#define PAGE_TITLE "W0ZC Power Center"

#define RELAY_ADDR 0x6D


//Global Variables
Qwiic_Relay quadRelay(RELAY_ADDR);
Preferences prefs;

AsyncWebServer server(80);

const int relay5 = 12; // Pins connected to relays
bool relayStatus[5] = {false, false, false, false, false}; // Initial relay statuses

const String pageTitle = "W0ZC Power Center";
String sessionAPIKey = "";

// Define the labels for the 5 relays
String relayDescriptions[] = {
    "Relay 1", 
    "Relay 2", 
    "Relay 3", 
    "Relay 4", 
    "Relay 5 - High Power"
};

const int buttonOnOff = 27;
const int ledPower = 33;


hw_timer_t *timer0 = NULL;


// Function Prototypes
void IRAM_ATTR onTimer0();
bool setRelay(int relay, int state);
bool getRelay(int relay);
void setSystem(int state);
String generateKey();
int getRandom(int max);
bool checkAPIKey(String key);
void saveRelayName(int relay, String name);
void factoryReset();
void loadRelays();
void displayConfiguration();


void notFound(AsyncWebServerRequest *request);
String getHeader();
String getFooter(String javascript);
String getPageInterface();
String getPageConfiguration();
String getJavascript();
String getCSS();


void setup(void) {
  Wire.begin();
  Serial.begin(115200);


  //factoryReset();

  pinMode(buttonOnOff, INPUT_PULLUP);

  //three legs of the Power LED - active low
  pinMode(ledPower, OUTPUT);   //red


  analogWrite(ledPower, 0);
  


  // Initialize relay pins
  Serial.println("Initializing the Relays");
  pinMode(relay5, OUTPUT);
  setRelay(5, 0);   //turn off the power relay

  if (quadRelay.begin()) {
    Serial.println("Quad relays ready for operation.");
  } else {
    Serial.println("THERE WAS A PROBLEM INITIALIZING THE RELAYS!!!");
  }
  quadRelay.turnAllRelaysOff();


  prefs.begin("power");


  Serial.println("Starting Wifi");
  WiFi.mode(WIFI_STA);

  //see if we're configured for static IP
  if (useStaticIP) {
    Serial.println("Using static IP");
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("...STA Failed to configure static IP");
    }
  } else {
    Serial.println("Using DHCP");
  }

  //Load the relay names from eeprom
  loadRelays();

  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }


  //configure the hardware timer 0. 
  timer0 = timerBegin(0, 80, true);   //prescale is 80
  timerAttachInterrupt(timer0, &onTimer0, true);
  timerAlarmWrite(timer0, 100000, true);    //adjust period here. 100000 is 1/10 second
  timerAlarmEnable(timer0);     //enable timer0



  // ******* Server Route Handlers *******
  // Server Route Handler: /api/{key}/set/relay/state/{relay}/{state}    -- 0=off, 1=on, 2=toggle
  server.on("^\\/api\\/([A-Z0-9]+)\\/set\\/relay\\/state\\/([0-9])\\/([0-9])$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String relay = request->pathArg(1);
    String state = request->pathArg(2);
    int relayID = relay.toInt();
    int stateID = state.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    if (stateID < 0 || stateID > 2) {
      request->send(400, "text/plain", "Invalid state");
      return;
    }

    if (relayID >= 1 && relayID <= 5) {
      setRelay(relayID, stateID);
      request->send(200, "text/plain", "Relay " + relay + " state set to " + state);
    } else if (relayID == 0) {
      //Toggle the system on or off.
      setSystem(stateID);
      request->send(200, "text/plain", "System state set to " + state);
    } else {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
    }
  });


  // Server Route Handler: /api/{key}/set/relay/name/{relay}/{name}
  server.on("^\\/api\\/([A-Z0-9]+)\\/set\\/relay\\/name\\/([0-9]+)\\/(.+)$", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String relay = request->pathArg(1);
    String name = request->pathArg(2);

    int relayID = relay.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    //limit the relay name to 20 characters
    if (name.length() > 20) {
      name = name.substring(0, 20);
    }

    if (relayID >= 1 && relayID <= 5) {
      saveRelayName(relayID, name);

      loadRelays();   //reload the relay names
      request->send(200, "text/plain", "Relay " + relay + " name saved");
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }

  });

  // Server Route Handler: /api/{key}/get/relay/state/{relay}
  server.on("^\\/api\\/([A-Z0-9]+)\\/get\\/relay\\/state\\/([0-9]+)$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String relay = request->pathArg(1);

    int relayID = relay.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    if (relayID >= 0 && relayID <= 5) {
      //returns the status of the relay. Relay==0 is the entire system, which is keyed off of relay 5
      request->send(200, "text/plain", String(getRelay(relayID)));
    } else if (relayID == 9) {
      //return the status of all relays
      String response = "";
      for (int i = 1; i <= 5; i++) {

        if (getRelay(i)) {
          response += "ON";
        } else {
          response += "OFF";
        }
        if (i < 5) response += ":";
      }
      request->send(200, "text/plain", response);
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }

  });

  // Server Route Handler: /api/{key}/get/relay/name/{relayID}
  server.on("^\\/api\\/([A-Z0-9]+)\\/get\\/relay\\/name\\/([0-9])$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String apiKey = request->pathArg(0);
    String relay = request->pathArg(1);

    int relayID = relay.toInt();

    //Check the API key
    if (!checkAPIKey(apiKey)) {
      request->send(400, "text/plain", "API Request Denied. Valid key?");
      return;
    }

    if (relayID >= 1 && relayID <= 5) {
      request->send(200, "text/plain", relayDescriptions[relayID -1]);
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }

  });

  // Server Route Handler: /
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getPageInterface());
  });

  // Server Route Handler: /config
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getPageConfiguration());
  });

  // Server Route Handler: /main.js
  server.on("/main.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/javascript", getJavascript());
  });

  // Server Route Handler: /main.css
  server.on("/main.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/css", getCSS());
  });
  

  // Server Route Handler: Not Found
  server.onNotFound(notFound);



  server.begin();

 //Generate the temporary session API key - Note that this should happen after Wifi has been established.
  sessionAPIKey = generateKey();  

  //Get the API key. Generate a new one if necessary
  String apiKey = getAPIKey(false);   //Don't force the creation of a new key if one already exists

  //Show the running configuration on the Serial port
  displayConfiguration();
}

void loop(void) {

  int buttonHold = 0;

  //check to see if on button is being pressed
  while (digitalRead(buttonOnOff) == 0) {
    buttonHold++;
    delay(50);
  }

  if (buttonHold > 0) {
    //see if it was a short hold - to turn on
    if (buttonHold < 10) {
      setSystem(1);
    } else {
      setSystem(0);
    }
  }


}




/******************************************************************************************
                    Functions
******************************************************************************************/
void IRAM_ATTR onTimer0() {
/*
Timer overflow interrupt. Handle the power light blinking here.
*/

  static int fadeCount = 0;
  if (getRelay(0)) {
    //Check the state of the system (which is keyed off of relay 5 - so we're on
    fadeCount = 255;
  } else {
    fadeCount += 20;
    if (fadeCount > 255) {
      fadeCount = -200;    //set to a really low value so the light appears off for a bit before fading in.
    }
  }

  //set the LED output. Allow fadeCount to go negative
  if (fadeCount >= 0 && fadeCount <= 255) {
    analogWrite(ledPower, fadeCount);
  } else {
    if (fadeCount < 0) {
      analogWrite(ledPower, 0);
    } else {
      analogWrite(ledPower, 255);
    }
  }
}
/******************************************************************************************/
String getAPIKey(bool force) {
/* 
Retrieves the API key from eeprom. If it doesn't exist, or if force is true, it will generate a new one. If a new
one is generated, it will be saved to eeprom.
*/


  //build an array of possible characters for the API key, exlucing look-alike characters
  char possibleChars[] = {'2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};

  bool foundValid = true;    //assume we have a valid one unless proven otherwise

  //check to see if a valid API key alredy exists in eeprom
  String storedKey = prefs.getString("apikey", "");

  if (storedKey.length() == 14) {
    //see if it's a valid key consisting only of possibleChars

    for (int i = 0; i < 14; i++) {
      bool validChar = false;
      for (int j = 0; j < 32; j++) {
        if (storedKey[i] == possibleChars[j]) {
          validChar = true;
        }
      }
      if (!validChar) {
        foundValid = false;
      }

    }
  } else {
    //it wasn't even 14 chars long
    foundValid = false;
  }

  if (foundValid && !force) {
    //we have a valid key already, and we're not forcing a new one
    return storedKey;
  }

  String key = generateKey();   //generate a new key

  prefs.putString("apikey", key);   //save the key to eeprom

  return key;
}
/******************************************************************************************/
String generateKey() {
/*
Generates a new 14 character key to be used as an API key. The key is generated from the possibleChars array which
excludes look-alike characters such as 0, O, 1, I, etc.
*/

  //build an array of possible characters for the API key, exlucing look-alike characters
  char possibleChars[] = {'2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};


  String key = "";
  for (int i = 0; i < 14; i++) {
    key += possibleChars[getRandom(32)];
  }
  return key;
}
/******************************************************************************************/
int getRandom(int max) {
/*
Get a random number between 0 and max
*/
  return (esp_random() % max);
}
/******************************************************************************************/
bool checkAPIKey(String key) {
/* 
Check the key argument against the short-term session API key (stored in RAM) and the 
long-term API key stored in eeprom. If either match, return true. Otherwise, return false.
*/
  
  if (key == sessionAPIKey) return true;    //The session key is good
  if (key == getAPIKey(false)) return true;    //The long-term key is good

  return false;
}
/******************************************************************************************/
bool setRelay(int relay, int state) {
/*
Sets the relay to 0=off, 1=on, 2=toggle. Returns true if the relay is on, false if it is off.
*/
  if (state == 2) {
    //toggle the relay
    state = !relayStatus[relay -1];
  }

  //save the state of the relay back to the array
  relayStatus[relay - 1] = state;


  if (relay == 5) {
    //This is the digital output
    digitalWrite(relay5, state ? HIGH : LOW);
  } else {
    //This is one of the Qwiic relays


    if (state) {
      //turn on
      quadRelay.turnRelayOn(relay);
    } else {
      //turn off
      quadRelay.turnRelayOff(relay);
    }
  }
  

  return (state == 1);
}
/******************************************************************************************/
bool getRelay(int relay) {
/*
Gets the state of the relay. Returns true if the relay is on, false if it is off.
*/

  if (relay < 1 || relay > 5) relay = 5;    //A relay of 0 is the entire system, which is keyed off of the power relay (5). Any other invalid value will check the Power Relay

  return relayStatus[relay - 1];    //return the state of the relay from Base-0.
}
/******************************************************************************************/
void setSystem(int state) {
/*
Turns the system on or off. If state is 2, then toggle the system. If state is 1, turn the system on. If state is 0, turn the system off.
*/
  int dly = 500;

  if (state == 2) {
    //figure out if it's already on or not
    getRelay(0) ? state = 0 : state = 1;    //if it's already on, then set the state to 0 for off
  }

  if (state == 1) {
    //turn things on
    for (int i=1;i<5;i++) {
      setRelay(i, 1);
      delay(dly);
    }
    //extra delay before power relay
    delay(dly);
    setRelay(5, 1);

  } else {
    //turning things off
    setRelay(5, 0);
    delay(dly);

    for (int i=4;i>=1;i--) {
      delay(dly);
      setRelay(i, 0);
    }
  }
}
/******************************************************************************************/
void saveRelayName(int relay, String name) {
/*
Save the name of the relay channel to eeprom.
*/

  //limit the relay name to 20 characters
  if (name.length() > 20) {
    name = name.substring(0, 20);
  }
  
  int szLen = name.length() + 1;
  char szName[szLen];	
	name.toCharArray(szName, szLen);

  String key = "rly" + String(relay);
  szLen = key.length() + 1;
  char szKey[szLen];
  key.toCharArray(szKey, szLen);

  prefs.putString(szKey, szName);
}
/******************************************************************************************/
void factoryReset() {
/*
Reset the device to factory defaults. This will clear all settings and restore the device to the default.
*/

  //Clear the eeprom
  prefs.clear();

  //Reset the relay names to the defaults
  prefs.putString("rly1", "Relay 1");
  prefs.putString("rly2", "Relay 2");
  prefs.putString("rly3", "Relay 3");
  prefs.putString("rly4", "Relay 4");
  prefs.putString("rly5", "Relay 5 - Power");


  //Reset the API key
  getAPIKey(true);    //force the creation of a new API key

}
/******************************************************************************************/
void loadRelays() {
/*
Load the relay names from eeprom
*/

  // pull the descriptions from eeprom
  relayDescriptions[0] = prefs.getString("rly1", "1");
  relayDescriptions[1] = prefs.getString("rly2", "2");
  relayDescriptions[2] = prefs.getString("rly3", "3");
  relayDescriptions[3] = prefs.getString("rly4", "4");
  relayDescriptions[4] = prefs.getString("rly5", "5");

  //Check for invalid settings - reset to default
  if (relayDescriptions[0].length() > 20) relayDescriptions[0] = "Relay 1";
  if (relayDescriptions[1].length() > 20) relayDescriptions[1] = "Relay 2";
  if (relayDescriptions[2].length() > 20) relayDescriptions[2] = "Relay 3";
  if (relayDescriptions[3].length() > 20) relayDescriptions[3] = "Relay 4";
  if (relayDescriptions[4].length() > 20) relayDescriptions[4] = "Relay 5";

}
/******************************************************************************************/
void displayConfiguration() {
/*
Dumps all of the configuration information to the Serial port, including the API Key.
*/
  Serial.println("");
  Serial.println("");
  Serial.println("");

  Serial.println(PAGE_TITLE);
  Serial.println("Firmware:     " + String(FIRMWARE_VERSION));
  Serial.println("API Key:      " + getAPIKey(false));
  Serial.println("");

  Serial.println("Relay 1:      " + relayDescriptions[0]);
  Serial.println("Relay 2:      " + relayDescriptions[1]);
  Serial.println("Relay 3:      " + relayDescriptions[2]);
  Serial.println("Relay 4:      " + relayDescriptions[3]);
  Serial.println("Relay 5:      " + relayDescriptions[4]);
  Serial.println("");

  Serial.println("Wifi SSID:    " + WiFi.SSID());
  Serial.println("IP Address:   " + WiFi.localIP());
  Serial.print("IP Mode:      ");
  if (useStaticIP) {
    Serial.println("Static");
  } else {
    Serial.println("DHCP");
  }
  // Serial.println("Gateway:      " + WiFi.gatewayIP());
  // Serial.println("Subnet Mask:  " + WiFi.subnetMask());
  // Serial.println("Primary DNS:  " + WiFi.dnsIP(0));
  // Serial.println("Secondary DNS:" + WiFi.dnsIP(1));
}
/******************************************************************************************/







/******************************************************************************************
                    Views
******************************************************************************************/
void notFound(AsyncWebServerRequest *request) {
/*
Respond with a 404 File Not Found error.
*/
    request->send(404, "text/plain", "Not found");
}
/******************************************************************************************/
String getHeader() {

  String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%TITLE%</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.7.1/jquery.min.js"></script>

    <link rel="stylesheet" href="/main.css">   

    
</head>
<body>
    <header class="bg-primary text-white py-3">
        <div class="container d-flex justify-content-between align-items-center">
            <h1 class="mb-0">%TITLE%</h1>
            <a class="btn btn-light" href="/config">Configuration</a>
            <a class="btn btn-light" href="/">Control</a>
        </div>
    </header>

)"; 
  
  html.replace("%TITLE%", PAGE_TITLE);


    return html;
  }
/******************************************************************************************/
String getFooter(String javascript) {

  String html = R"(
<footer class="bg-dark text-white py-3 mt-5">
        <div class="container">
            <p class="mb-0">%TITLE%</br /><a href="http://www.w0zc.com/" target="_blank">W0ZC.com</a></p>
        </div>
    </footer>

    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
    <script src="/main.js"></script>
    <script type="text/javascript">
%JAVASCRIPT%
    </script>

    
</body>
</html>
)"; 

  html.replace("%TITLE%", PAGE_TITLE);
  html.replace("%JAVASCRIPT%", javascript);

  return html;
}
/******************************************************************************************/
String getPageInterface() {

  String html = R"(
    <main class="container mt-4">
        <form>
            <h1>Power</h1>
            <div class="container">
                <div class="row">
                    <div class="col-md-6">
                        <h2 class="text-center">System Controls</h2>
                        <a class="btn btn-primary w-100 mb-2" onclick="setSystem(1);" id="systemOn">System On</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="setSystem(0);" id="systemOff">System Off</a>
                    </div>
                    <div class="col-md-6">
                        <h2 class="text-center">Relays</h2>
                        <a class="btn btn-primary w-100 mb-2" onclick="toggleRelay(1);" id="relay1">%RLY1%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="toggleRelay(2);" id="relay2">%RLY2%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="toggleRelay(3);" id="relay3">%RLY3%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="toggleRelay(4);" id="relay4">%RLY4%</a>
                        <a class="btn btn-primary w-100 mb-2" onclick="toggleRelay(5);" id="relay5">%RLY5%</a>
                    </div>
                </div>
            </div>
        </form>
    </main>
)"; 


  //Replace placeholders with actual values
  html.replace("%RLY1%", relayDescriptions[0]);
  html.replace("%RLY2%", relayDescriptions[1]);
  html.replace("%RLY3%", relayDescriptions[2]);
  html.replace("%RLY4%", relayDescriptions[3]);
  html.replace("%RLY5%", relayDescriptions[4]);

  String js = R"(
setInterval(tmrRefresh, 500);
)";

  //Concat header, body, and footer
  String header = getHeader();
  String footer = getFooter(js);
  html = header + html + footer;

  return html;



}
/******************************************************************************************/
String getPageConfiguration() {

  String html = R"(
<main class="container mt-4">
  <div class="container">
    <div class="row">
        <div class="col-md-6">
        <form>
            <h2>Relay Labels</h2>
            <div class="form-group">
                <label for="relay1">Relay 1:</label>
                <input type="text" class="form-control" id="relay1" name="relay1" maxlength="20" value="%RLY1%">
            </div>
            <div class="form-group">
                <label for="relay2">Relay 2:</label>
                <input type="text" class="form-control" id="relay2" name="relay2" maxlength="20" value="%RLY2%">
            </div>
            <div class="form-group">
                <label for="relay3">Relay 3:</label>
                <input type="text" class="form-control" id="relay3" name="relay3" maxlength="20" value="%RLY3%">
            </div>
            <div class="form-group">
                <label for="relay">Relay 4:</label>
                <input type="text" class="form-control" id="relay4" name="relay4" maxlength="20" value="%RLY4%">
            </div>
            <div class="form-group">
                <label for="relay5">Relay 5 - High Power:</label>
                <input type="text" class="form-control" id="relay5" name="relay5" maxlength="20" value="%RLY5%">
            </div>

            
            <div class="form-group">
                <button type="button" class="btn btn-primary w-100 mt-4" onclick="saveRelayNames();">Save Relays</button>
            </div>                
        </form>
      </div>
      <div class="col-md-6">
        
        <form>
            <h2>&nbsp;</h2>
            <h4>&nbsp;</h4>
            <!--div class="form-group">
                <button type="button" class="btn btn-primary w-100 mt-4" onclick="saveColors();">Save Colors</button>
            </div--> 
        </form>
        </div>
      </div>
    </div>

    <div class="row">
      <div class="col-md-12">
        <span class="text-center">Firmware Version: %FIRMWARE%</span>
      </div>
    </div>
    </main>    
)";


  //Replace placeholders with actual values
  html.replace("%RLY1%", relayDescriptions[0]);
  html.replace("%RLY2%", relayDescriptions[1]);
  html.replace("%RLY3%", relayDescriptions[2]);
  html.replace("%RLY4%", relayDescriptions[3]);
  html.replace("%RLY5%", relayDescriptions[4]);

  html.replace("%FIRMWARE%", FIRMWARE_VERSION);


  //Concat header, body, and footer
  String header = getHeader();
  String footer = getFooter("");
  html = header + html + footer;

  return html;


}
/******************************************************************************************/
String getJavascript() {

  String html = R"(

var apiKey = "%APIKEY%";

function saveRelayNames() {
  setRelayName(1, document.getElementById('relay1').value);
  setRelayName(2, document.getElementById('relay2').value);
  setRelayName(3, document.getElementById('relay3').value);
  setRelayName(4, document.getElementById('relay4').value);
  setRelayName(5, document.getElementById('relay5').value);
}

async function setRelayName(relay, name) {
  try {
      var uri = '/api/' + apiKey + '/set/relay/name/' + relay.toString() + '/' + name;
      //console.log(uri);

      const response = await fetch(uri, {
          method: 'POST'
      });

      if (response.ok) {
          const result = await response.text();
      } else {
          alert('Error in API call.');
      }

  } catch (error) {
      console.log(error);
      //alert('An error occurred');
  }
}


async function toggleRelay(relay) {
  try {
      

    var uri = '/api/' + apiKey + '/set/relay/state/' + relay.toString() + '/2';
    //console.log(uri);

    const response = await fetch(uri, {
        method: 'POST'
    });

    if (response.ok) {
        const result = await response.text();
    } else {
        alert('Error in API call.');
    }
  } catch (error) {
    console.log(error);
    //alert('An error occurred');
  }
}

async function setSystem(state) {
  try {
    var uri = '/api/' + apiKey + '/set/relay/state/0/' + state.toString();
    //console.log(uri);

    const response = await fetch(uri, {
        method: 'POST'
    });

    if (response.ok) {
        const result = await response.text();
    } else {
        alert('Error in API call.');
    }
  } catch (error) {
    console.log(error);
    //alert('An error occurred');
  }
}  

async function tmrRefresh() {
  try {
    var uri = '/api/' + apiKey + '/get/relay/state/9';
    //console.log(uri);

    const response = await fetch(uri, {
      method: 'GET'
    });

    if (response.ok) {
      const relays = await response.text();

      console.log("Get Results: " + relays);

      var rlys = relays.split(":");

      //loop through all of the relay buttons and set the class to btn-primary except for the one that is connected
      for (var i = 0; i < 5; i++) {
        var button = document.getElementById('relay' + (i+1).toString());
        if (rlys[i] == "ON") {
          button.className = "btn btn-success w-100 mb-2";
        } else {
          button.className = "btn btn-primary w-100 mb-2";
        }
      }   

    } else {
      //alert('Error in API call.');
    }
  } catch (error) {
    console.log(error);
  }
}
)";


  html.replace("%APIKEY%", sessionAPIKey);

  return html;
}
/******************************************************************************************/
String getCSS() {

  String html = R"(

body {
  width: 100%;
  height: 80%;
  margin: auto;
}
#main {
  height: 400px;
}
#color {
  margin-left: 10%;
  width: 50%;
}
  
)";

  return html;

}