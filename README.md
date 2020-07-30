Version 1.0 23-07-2020

This is my arduino code for wall-mounted watch, based on 4x max7219 8x8 LED dot matrix and ESP8266 module. Nothing more.

The watch takes time by NTP(required for the first launch) and store the data to millis() value.Every 10 minutes by Ticker.h the time is being updated.

The watch has HTTP server. It accepts some configuration by HTTP request. All described inside help.html page code.

Also there is automatic brightness correction, according to the current time. Values of Day and Night brightness are stored in EEPROM and could be set via HTTP request.

Also there are includes OTA-update functionality, MDNS responder, SSDP server.
There is a function udpsend() for sending log strings to any Rsyslog server to UDP port 514.

Used external libraries:

https://github.com/markruys/arduino-Max72xxPanel/

https://github.com/adafruit/Adafruit-GFX-Library
