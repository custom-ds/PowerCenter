# Power Center - Firmware
The C firmware for the Sparkfun ESP32-based power controller for amateur radio stations.


## Hardware
The W0ZC Power Center utilizes the "Sparkfun ESP32 Thing Plus C" as the main processor board and Wifi interface
for the coax switch controller. 



## Programming the ESP32 

### Setting up the Environment
The ESP32 controller used in this project is the Sparkfun ESP32 Thing Plus C. To program it from the Arduino IDE 2.x 
platform, start by installing the board support tools.

In the Boards Manager section of Arduino IDE, search for the esp32 by Espressif Systems boards, and click the Install 
button.

There are some libraries that are required. Go to Tools and Manage Libraries. In the Search box, find and install these
libraries:

 * AsyncTCP (by dvarrel). Currently 1.1.4
 * ESPAsyncTCP (by dvarrel). Currently v1.2.4
 * ESPAsyncWebSrv (by dvarrel). Currently v1.2.7
 * Sparkfun Qwiic Relay Arduino Library (by Ellias Santistevan). Currently v1.3.1

The ESPAsyncWebSrv utilizes Regular Expressions to handle some of the routing. This requires an additional file be
added to the Arduino IDE environment.

For arduino IDE: create/update platform.local.txt
 * Windows: C:\Users\(username)\AppData\Local\Arduino15\packages\espxxxx\hardware\espxxxx\{version}\platform.local.txt
 * Linux: ~/.arduino15/packages/espxxxx/hardware/espxxxx/{version}/platform.local.txt

Unless there are other items that have previously been added, the platform.local.txt file should just contain
a single line:

```
compiler.cpp.extra_flags=-DASYNCWEBSERVER_REGEX=1
```

### Programming the Firmware
Plug in the USB-C cable from the PC to the Sparkfun controller board. If the relays are already attached to the
Switch Controller, you may need to apply external 12V to the Power Center.

Click the Select Board dropdown, and select the COM port that is associated with the Sparkfun device.

That will bring up another window where you can select the type of board being programmed. Scroll down or search the list
for "Sparkfun ESP32 Thing Plus C" and click the OK button.

In the upper left corner on the IDE, click the Right-Arrow (Upload) button to begin the compile and upload of the firmware.

### Using Visual Studio Code
I find it more efficient to code within Microsoft's Visual Studio Code, rather than directly in the Arduino IDE. To do that,
open up the .ino file inside of VSCode and make any code changes in there.

Leave the Arduino IDE open in the background, with the same .ino firmware file open. 

Modify the code inside of VSCode and save the changes. Alt-Tab into the Arduino IDE and just click the Upload button to 
load the firmware on the ESP32. If you need to make a small tweak inside of the Arduino IDE, go ahead and save, and VSCode
will immediately reflect the changes when to switch task back to it.
