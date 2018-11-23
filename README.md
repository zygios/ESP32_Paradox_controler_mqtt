# ESP32_Paradox_controler_mqtt

I am workinf on integration of Paradox SP5500 alarm system to Domoticz server.
At moment using ESP32 (RX2, TX2) pins for connecting to Paradox serial port.
ESP32 sending mqtt messages to Domoticz.

Paradox serial 37 byte message is broken down into a json message with "Arm Status", "Event","Sub-group" and "dummy" - for getting zone/user text information.