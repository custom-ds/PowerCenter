#include "config.h"
/* Running on EPS32S3 Dev Module */

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <Wire.h>
#include "SparkFun_Qwiic_Relay.h"


/*
 * Wifi credentials are stored in config.h file. See the template.
 *
const char* ssid = "";
const char* password = "";
*/


AsyncWebServer server(80);


#define RELAY_ADDR 0x6D

Qwiic_Relay quadRelay(RELAY_ADDR);


const int relay5 = 12; // Pins connected to relays
bool relayStatus[5] = {false, false, false, false, false}; // Initial relay statuses


// Relay descriptions are configured in the config.h file


const int buttonOnOff = 27;
const int ledPower = 33;


hw_timer_t *timer0 = NULL;

void IRAM_ATTR onTimer0();
void notFound(AsyncWebServerRequest *request);
bool setRelay(int relay, bool status);
bool getRelay(int relay);
void toggleSystem(bool status);



void setup(void) {
  Wire.begin();
  Serial.begin(115200);


  pinMode(buttonOnOff, INPUT_PULLUP);

  //three legs of the Power LED - active low
  pinMode(ledPower, OUTPUT);   //red


  Serial.println("Turning off powerLED");   //active low
  analogWrite(ledPower, 0);
  


  // Initialize relay pins
  Serial.println("Initializing the Relays");
  pinMode(relay5, OUTPUT);
  setRelay(4, false);   //relay 5, index 4

  if (quadRelay.begin()) {
    Serial.println("Quad relays ready for operation.");
  } else {
    Serial.println("THERE WAS A PROBLEM INITIALIZING THE RELAYS!!!");
  }
  quadRelay.turnAllRelaysOff();

  Serial.println("Starting Wifi");
  WiFi.mode(WIFI_STA);

// Configures static IP address
  //see if we're configured for static IP
  if (useStaticIP) {
    Serial.println("Using static IP");
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("...STA Failed to configure static IP");
    }
  } else {
    Serial.println("Using DHCP");
  }



  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //configure the hardware timer 0. 
  timer0 = timerBegin(0, 80, true);   //prescale is 80
  timerAttachInterrupt(timer0, &onTimer0, true);
  timerAlarmWrite(timer0, 100000, true);    //adjust period here. 100000 is 1/10 second
  timerAlarmEnable(timer0);     //enable timer0




  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getWebpage());
  });

  // Handle API requests to toggle the relays
  server.on("/api/relay/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {

    String body = request->arg("relay");
    int relayNumber = body.toInt();

    if (relayNumber >= 1 && relayNumber <= 5) {
      int relayIndex = relayNumber - 1;
      setRelay(relayIndex, !getRelay(relayIndex));
      request->send(200, "text/plain", "Relay " + String(relayNumber) + " is " + (getRelay(relayIndex) ? "ON" : "OFF"));
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }
  });

  // Handle API requests to turn the relays on
  server.on("/api/relay/on", HTTP_POST, [](AsyncWebServerRequest *request) {

    String body = request->arg("relay");
    int relayNumber = body.toInt();

    if (relayNumber >= 1 && relayNumber <= 5) {
      int relayIndex = relayNumber - 1;
      setRelay(relayIndex, true);
      request->send(200, "text/plain", "Relay " + String(relayNumber) + " is " + (getRelay(relayIndex) ? "ON" : "OFF"));
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }
  });

  // Handle API requests to turn the relays off
  server.on("/api/relay/off", HTTP_POST, [](AsyncWebServerRequest *request) {

    String body = request->arg("relay");
    int relayNumber = body.toInt();

    if (relayNumber >= 1 && relayNumber <= 5) {
      int relayIndex = relayNumber - 1;
      setRelay(relayIndex, false);
      request->send(200, "text/plain", "Relay " + String(relayNumber) + " is " + (getRelay(relayIndex) ? "ON" : "OFF"));
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }
  });

  // Handle API requests to turn the relays off
  server.on("/api/relay/system/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Turning System On");
    toggleSystem(true);
  });  

  // Handle API requests to turn the relays off
  server.on("/api/relay/system/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Turning System Off");
    toggleSystem(false);
  });  


  server.onNotFound(notFound);

  // Get get the relay status
  server.on("/api/relay", HTTP_GET, [](AsyncWebServerRequest *request) {

    String body = request->arg("relay");
    int relayNumber = body.toInt();

    if (relayNumber >= 1 && relayNumber <= 5) {
      int relayIndex = relayNumber - 1;
      
      request->send(200, "text/plain", "Relay " + String(relayNumber) + " is " + (getRelay(relayIndex) ? "ON" : "OFF"));
    } else {
      request->send(400, "text/plain", "Invalid relay number");
    }
  });
  server.onNotFound(notFound); 

  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {

  int buttonHold = 0;

  //check to see if on button is being pressed
  while (digitalRead(buttonOnOff) == 0) {
    buttonHold++;
    Serial.println("Button press detected");
    delay(50);
  }

  if (buttonHold > 0) {
    //see if it was a short hold - to turn on
    if (buttonHold < 10) {
      Serial.println("Short press - turning on");
      toggleSystem(true);
    } else {
      Serial.println("Long press - turning everything off");
      toggleSystem(false);
    }
  }


}



/******************************************************************************************/
void IRAM_ATTR onTimer0() {
  //timer overflow interrupt. Handle the power light blinking here

  static int fadeCount = 0;
  if (digitalRead(relay5)) {
    //the primary power relay is engaged - so we're on
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
void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}
/******************************************************************************************/
bool setRelay(int relay, bool status) {
  relayStatus[relay] = status;


  if (relay == 4) {
    //This is the digital output
    Serial.println("Relay 5 Toggled");
    digitalWrite(relay5, status ? HIGH : LOW);
  } else {
    //This is one of the Qwiic relays

    Serial.print("Relay ");
    Serial.print(relay);
    Serial.println(" toggled.");

    if (status) {
      //turn on

      quadRelay.turnRelayOn(relay + 1);
      Serial.println("On");
    } else {
      //turn off
      quadRelay.turnRelayOff(relay + 1);
      Serial.println("Off");
    }
  }
  

  return status;
}
/******************************************************************************************/
bool getRelay(int relay) {
  return relayStatus[relay];
}
/******************************************************************************************/
void toggleSystem(bool status) {
  int dly = 500;
  if (status) {
    //turn things on
    for (int i=0;i<4;i++) {
      setRelay(i, true);
      delay(dly);
    }
    //extra delay before radio
    delay(dly);
    setRelay(4, true);

  } else {
    //turning things off
    setRelay(4, false);
    delay(dly);

    for (int i=3;i>=0;i--) {
      delay(dly);
      setRelay(i, false);
    }
  }
}
/******************************************************************************************/
String getWebpage() {

  String html = R"(<!DOCTYPE html>
<html>

<head>
  <title>%TITLE%</title>
</head>

<body>
  <h1>%TITLE%</h1>

  <form id='relayForm'>
    <label for='systemon'>System On:</label>
    <button type='button' id='systemon' onclick='toggleSystem(true)'>System On</button><br>    
  
    <label for='systemoff'>System Off:</label>
    <button type='button' id='systemoff' onclick='toggleSystem(false)'>System Off</button><br>  

    <label for='relay1'>Relay 1 :: %RELAY1DESC%</label>
    <button type='button' id='relay1' onclick='toggleRelay(1)'>Toggle</button><br>

    <label for='relay2'>Relay 2 :: %RELAY2DESC%</label>
    <button type='button' id='relay2' onclick='toggleRelay(2)'>Toggle</button><br>

    <label for='relay3'>Relay 3 :: %RELAY3DESC%</label>
    <button type='button' id='relay3' onclick='toggleRelay(3)'>Toggle</button><br>

    <label for='relay4'>Relay 4 :: %RELAY4DESC%</label>
    <button type='button' id='relay4' onclick='toggleRelay(4)'>Toggle</button><br>

    <label for='relay5'>Relay 5 :: %RELAY5DESC%</label>
    <button type='button' id='relay5' onclick='toggleRelay(5)'>Toggle</button><br>


  </form>

  <script>
    async function toggleRelay(relayNumber) {
      try {
        var uri = '/api/relay/toggle?relay=' + relayNumber.toString();
        console.log(uri);

        const response = await fetch(uri, {
          method: 'POST'
        });

        if (response.ok) {
          const result = await response.text();
          //alert(result); // Display the result (e.g., Relay 1 is ON/OFF)
        } else {
          alert('Error toggling relay');
        }
      } catch (error) {
        console.error(error);
        alert('An error occurred');
      }
    }

    async function toggleSystem(systemOn) {
      try {
        var uri ='';
        if (systemOn) {
          uri = '/api/relay/system/on';
        } else {
          uri = '/api/relay/system/off';
        }
      
        console.log(uri);

        const response = await fetch(uri, {
          method: 'POST'
        });

        if (response.ok) {
          const result = await response.text();
          //alert(result); // Display the result (e.g., Relay 1 is ON/OFF)
        } else {
          alert('Error toggling relay');
        }
      } catch (error) {
        console.error(error);
        alert('An error occurred');
      }
    }

  </script>
</body>

</html>
)"; 

  // Replace placeholders with actual values
  html.replace("%TITLE%", pageTitle);
  html.replace("%RELAY1DESC%", relayDescriptions[0]);
  html.replace("%RELAY2DESC%", relayDescriptions[1]);
  html.replace("%RELAY3DESC%", relayDescriptions[2]);
  html.replace("%RELAY4DESC%", relayDescriptions[3]);
  html.replace("%RELAY5DESC%", relayDescriptions[4]);

  return html;



}
/******************************************************************************************/
/******************************************************************************************/
