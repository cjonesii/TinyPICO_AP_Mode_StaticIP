| Supported Targets | Tiny PICO |
| ----------------- | --------- |

# _Tiny PICO AP-Mode in Static IP_

Develop compilable code for ESP32 using ESP-IDF C.
The preference is for this to be done to use VS Code with the Espressif extension.

Need to be able to interact with the ESP32 via the web interface.


Web UI needs to have:
• 2 input fields, for example credentials for home WiFi (SSID and password), First/Last name, any two fields;
• Button (Save/Send)

Visual aspects of the UI are of no importance. IP address of ESP32 needs to be static for the AP mode (192.168.0.1). 
Sending of the input field values needs to be done using POST with the JSON format.

As a result:
- Need a server, which is available when connecting to the access point of the ESP32, using the static IP address.
- When clicked on the button need to send the POST request with the values of the field (SSID, password) in JSON format and have them printed in the terminal.