#Version 1.0 23-07-2020
#This is my arduino code for wall-mounted watch, based on 4x max7219 8x8 LED dot matrix and ESP8266 module. Nothing more.
#The watch takes time by NTP(required for the first launch) and store the data to millis() value.Every 10 minutes by Ticker.h the time is being updated.
#The watch has HTTP server. It accepts some configuration by HTTP request. All described inside help.html page code.
#Also there is automatic brightness correction, according to the current time. Values of Day and Night brightness are stored in EEPROM and could be set via HTTP request.
#Also there are includes OTA-update functionality, MDNS responder, SSDP server.
#There is a function udpsend() for sending log strings to any Rsyslog server to UDP port 514.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266SSDP.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <Ticker.h>

WiFiClient client;
WiFiUDP udp;
NTPClient ntpClient(udp, "192.168.10.1", ((3*60*60)),6000000); // IST = GMT + 3, update interval 10 min.
int pinCS = 12; // Attach CS to this pin, DIN to MOSI(GPIO13) and CLK to SCK(GPIO14)
int numberOfHorizontalDisplays = 4;
int numberOfVerticalDisplays = 1;
const char *ssid = "Type-your-wifi-ssid";
const char *password = "type-your-wifi-password";
Ticker Timer;//timer for regular update via NTP and some other function
unsigned int debug_en = 2;//global variable for debug mode
unsigned int New = 1;//global variable for the first launch of the system
ESP8266WebServer server(80);
Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
String tmpdata_header = "wall-clock-zal.lan ";//this whould be added to the start of any log string, sent to Rsyslog server.
char tmpdata_arr[1492];//for function udpsend()
char helppage[500];//for HTTP help page
int spacer = 1;//for matrix display
char uptime[30];
int width = 5 + spacer; // The font width is 5 pixels
int TimerCounter = 0;//this counter will be incremented every 1 sec.
int h,m,s;//variables for hours, minutes and seconds.
long localEpoc = 0;//local copy of millis() to store current time
long localMillisAtUpdate = 0;
int brightness_day, brightness_night, current_brightness = 0;//variables for brightness control
int xh = 2;//shift in dots on 8x8 LED matrix to print out hours digits
int xm = 19;//shift in dots on 8x8 LED matrix to print out minutes digits
 
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  if (debug_en == 1)
  {
    udpsend("debug,http Some page was not found.");
  }
}

void handleHelp()
{
  snprintf(helppage, 1500,"<pre>General help:\r\n\
  /                       - root page. Shows current temperature\r\n\
  /description.xml        - config file from SSDP protocol\r\n\
  /brightness             - shows current brightness values, stored in EEPROM and system variables\r\n\
  /brightness/day?set=    - set new value between 0 and 15. Store to EEPROM and system variables\r\n\
  /brightness/night?set=  - set new value between 0 and 15. Store to EEPROM and system variables\r\n\
  /debug                  - shows the current status of debug mode\r\n\
  /debug/on               - permanently turns on debug to syslog\r\n\
  /debug/off              - permanently turns off debug to syslog\r\n\
  /help                   - shows this help page\r\n\
  /hours                  - shows current values of Night_start and Night_end\r\n\
  /hours/day?set=         - set new value between 7 and 20. Store to EEPROM and system variables\r\n\
  /hours/night?set=         - set new value between 20 and 7. Store to EEPROM and system variables\r\n\
  /index.html             - the same as the root page above\r\n\
  /led/on                 - turn built-in LED on\r\n\
  /led/off                - turn built-in LED off\r\n\
  /ntp                    - view the current timestamp, received from NTP server\r\n\
  /reboot                 - restart the system\r\n\
  /show                   - Showing up a text, given as GET parametr after /show?\r\n\
  /upd                    - force update of current time on the clock\r\n\
  /uptime                 - get current uptime value\r\n\</pre>\
  ");
  server.send(200, "text/html", helppage);
  if (debug_en == 1)
  {
    udpsend("debug,http Access to /help page.");
  }
}

void handleUptime()//shows up current uptime when being called via HTTP request
{
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  snprintf(uptime, 30, "Uptime: %02d:%02d:%02d\r\n", hr, min % 60, sec % 60);
  server.send(200, "text/html", uptime);
  if (debug_en == 1)
  {
    udpsend("debug,http Access to /uptime page.");
  }
}

String* udpsend(String tmpdata)//sends a string ot text to Rsyslog server, set in beginPacket() function.
{
  tmpdata = tmpdata_header + tmpdata;
  tmpdata.toCharArray(tmpdata_arr,sizeof(tmpdata_arr));
  udp.beginPacket("192.168.10.1",514);
  udp.write(tmpdata_arr);
  udp.endPacket();
  delay(200);
}

void handleDebug()//handle request via HTTP and shows up the current status of debug.
{
  if (debug_en == 0)
  {
    server.send(200, "text/plain", "Debug is DISABLED\r\n");
  }
  if (debug_en == 1)
  {
    server.send(200, "text/plain", "Debug is ENABLED\r\n"); 
  }
  if (debug_en == 1)
  {
    udpsend("debug,http Access to /debug page.");
  }
}

void handleDebugOn()
{
  if (debug_en == 0)
  {
    EEPROM.begin(512);
    EEPROM.write(0,1);
    EEPROM.commit();
    EEPROM.end();
    debug_en = 1;
    server.send(200, "text/plain", "Debug activated!\r\n");
    if (debug_en == 1)
    {
      udpsend("debug,info Debug activated!");
    }
  }
  else
  {
    server.send(200, "text/plain", "Debug already ENABLED!\r\n");
    udpsend("debug,info Wrong trying to turn on Debug! Already turned on!");
  }
}

void handleDebugOff()
{
  if (debug_en == 1)
  {
    EEPROM.begin(512);
    EEPROM.write(0,0);
    EEPROM.commit();
    EEPROM.end();
    server.send(200, "text/plain", "Debug is deactivated!\r\n");
    if (debug_en == 1)
    {
      udpsend("debug,info Debug deactivated!");
    }
    debug_en = 0;
  }
  else
  {
    server.send(200, "text/plain", "Debug is already deactivated!\r\n");
  }
}

//updates time every 10 minutes and check out do we need to increase or decrease a brightness of LED via checkDayNight() function.
void ntpTimer() {
  if (TimerCounter >= 600) 
  { 
     if(ntpClient.forceUpdate())
     {
        String source_time = ntpClient.getFormattedTime();
        h = source_time.substring(0, 2).toInt();
        m = source_time.substring(3, 6).toInt();
        s = source_time.substring(6, 8).toInt();
        localMillisAtUpdate = millis();
        localEpoc = (h * 60 * 60 + m * 60 + s); 
        TimerCounter = 0;
        udpsend("system,info ntpTimer: time updated successfully.");
        if (debug_en == 1)
        {
          
          udpsend("system,debug Debug data of getTime:.");
          udpsend("system,debug Source time: " + source_time);
          udpsend("system,debug H: " + String(h));
          long curEpoch = localEpoc + ((millis() - localMillisAtUpdate) / 1000);
          long epoch = round(curEpoch + 86400L);
          udpsend("system,debug H1: " + String(((epoch  % 86400L) / 3600) % 24));
          udpsend("system,debug M: " + String(m));
          udpsend("system,debug S: " + String(s));
          udpsend("system,debug localEpoc: " + String(localEpoc));
        }
     }
     else
     {
         udpsend("system,error ntpTimer: unable to update NTP time!");
         TimerCounter = 0;
     }
     checkDayNight();
  }
  TimerCounter++;
}

//shows up current date
String getDate() {
   String formattedDate = ntpClient.getFormattedDate();
   int splitT = formattedDate.indexOf("T");
   String dayStamp = formattedDate.substring(0, splitT);
   return dayStamp;
}

//update current time and set variables of H,M,S to show on the screen
void updateTime()
{
  long curEpoch = localEpoc + ((millis() - localMillisAtUpdate) / 1000);
  long epoch = round(curEpoch + 86400L);
  h = ((epoch  % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
}

//just for fun function. Can show up scrolling text, taken by HTTP request.
void scrollText(String tape, int Speed)
{
    if (Speed == 0) 
    { 
      Speed = 40; 
    }
    for ( int i = 0 ; i < width * tape.length() + matrix.width() - 1 - spacer; i++ ) {
    matrix.fillScreen(LOW);
    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically
    while ( x + width - spacer >= 0 && letter >= 0 ) {
      if ( letter < tape.length() ) {
        matrix.drawChar(x, y, tape[letter], HIGH, LOW, 1);
      }
      letter--;
      x -= width;
    }
    matrix.write();
    delay(Speed);
  }
}

//change brightness of LED screen at night.Levels of brightness are between 0 and 15.The are stored in EEPROM.Could be set via HTTP request.
void checkDayNight()
{
  if (h == 22 || h == 23 || h == 00 || h == 01 || h == 02 || h == 03 || h == 04 || h == 05 || h == 06)
  {
    if (current_brightness != brightness_night)
    {
      matrix.setIntensity(brightness_night);
      current_brightness = brightness_night;
      if (debug_en == 1)
      {
         udpsend("system,debug Brightness changed to Night");
      }
    }
  }
  else
  {
    if (current_brightness != brightness_day)
    {
      matrix.setIntensity(brightness_day);
      current_brightness = brightness_day;
      if (debug_en == 1)
      {
         udpsend("system,debug Brightness changed to Day");
      }
    }
  }
}

void setupWebserver()
{
  server.on("/led/on", []() {
  digitalWrite(LED_BUILTIN, LOW);
    server.send(200, "text/plain", "Led turned ON\r\n");
  });
  server.on("/led/off", []() {
    digitalWrite(LED_BUILTIN, LOW);
    server.send(200, "text/plain", "Led turned OFF\r\n");
  });
  server.on("/show", []() {
    server.send(200, "text/plain", "Showing up your text: "+ String(server.argName(0)) + "!\r\n");
    scrollText(String(server.argName(0)),40);
  });
  server.on("/", handleHelp);
  server.on("/index.html", handleHelp);
  server.on("/help", handleHelp);
  server.on("/uptime", handleUptime);
  server.on("/brightness", []() 
  {
      String data = "Brightness_day = " + String(brightness_day) + "\n";
      data += "Brightness_night = " + String(brightness_night) + "\n";
      data += "Brightness_current = " + String(current_brightness) + "\n";
      server.send(200, "text/plain", data);
      if (debug_en == 1)
      {
         udpsend("system,debug Access to /brightness");
      }
  });
  server.on("/brightness/day", []() 
  {
      if (server.argName(0) == "set")
      {
        if ((server.arg(0).toInt() <= 15) and (server.arg(0).toInt() >= 0))
        {
          EEPROM.begin(512);
          EEPROM.write(1,server.arg(0).toInt());
          EEPROM.commit();
          brightness_day = EEPROM.read(1);
          if (brightness_day == server.arg(0).toInt())
          {
            server.send(200, "text/plain", "New value of Brightness_day successfully set to " + String(brightness_day) + "\n");
            if (debug_en == 1)
            {
              udpsend("system,debug New value of Brightness_day successfully set to " + String(brightness_day));
            }
          }
          else
          {
            server.send(200, "text/plain", "Error! New value of Brightness_day not set to " + String(brightness_day) + "\n");
            if (debug_en == 1)
            {
              udpsend("system,debug Error! New value of Brightness_day not set to " + String(brightness_day));
            }
          }
        }
      }
      else
      {
           server.send(200, "text/plain", "Error! Unknown command. See /help.\n");
           if (debug_en == 1)
           {
             udpsend("system,debug Error! Unknown command received for /brightness/day");
          }
      }
  });
  server.on("/brightness/night", []() 
  {
      if (server.argName(0) == "set")
      {
        if ((server.arg(0).toInt() <= 15) and (server.arg(0).toInt() >= 0))
        {
          EEPROM.begin(512);
          EEPROM.write(2,server.arg(0).toInt());
          EEPROM.commit();
          brightness_night = EEPROM.read(2);
          if (brightness_night == server.arg(0).toInt())
          {
            server.send(200, "text/plain", "New value of Brightness_night successfully set to " + String(brightness_night) + "\n");
            if (debug_en == 1)
            {
              udpsend("system,debug New value of Brightness_night successfully set to " + String(brightness_night));
            }
          }
          else
          {
            server.send(200, "text/plain", "Error! New value of Brightness_night not set to " + String(server.arg(0)) + "\n");
            if (debug_en == 1)
            {
              udpsend("system,debug Error! New value of Brightness_night not set to " + String(server.arg(0)));
            }
          }
        }
      }
      else
      {
           server.send(200, "text/plain", "Error! Unknown command. See /help.\n");
           if (debug_en == 1)
           {
             udpsend("system,debug Error! Unknown command received for /brightness/night");
          }
      }
  });
  server.on("/night", []() 
  {
      String data = "Night_start = " + String(night_start) + "\n";
      data += "Night_end = " + String(night_end) + "\n";
      data += "Hour_current = " + String(h) + "\n";
      server.send(200, "text/plain", data);
      if (debug_en == 1)
      {
         udpsend("system,debug Access to /brightness");
      }
  });
  server.on("/debug", handleDebug);
  server.on("/debug/on", handleDebugOn);
  server.on("/debug/off", handleDebugOff);
  server.on("/description.xml", []() 
  {
      SSDP.schema(server.client());
      if (debug_en == 1)
      {
         udpsend("system,debug Access to description.xml");
      }
  });
  server.on("/ntp", []() 
  {
      ntpClient.update();
      String data = getDate() + " " + ntpClient.getFormattedTime() + "\n";
      server.send(200, "text/plain", data);
      if (debug_en == 1)
      {
         udpsend("system,debug Access to /ntp page");
      }
  });
  server.on("/light", []() 
  {
      int bri = String(server.argName(0)).toInt();
      matrix.setIntensity(bri);
      server.send(200, "text/plain", "Set brightness to: " + String(server.argName(0)));
      if (debug_en == 1)
      {
         udpsend("system,debug Set brightness to: " + String(server.argName(0)));
      }
  });
  server.on("/upd", []() 
  {
      TimerCounter = 600;
      ntpTimer();
      server.send(200, "text/plain", "Force updating time!\n");
      
  });
  server.on("/reboot", []() 
  {
      server.send(200, "text/plain", "System is going to reboot!\n");
      udpsend("system,info System is going to reboot!");
      ESP.restart();
  });
  server.onNotFound(handleNotFound);
  server.begin();
  udpsend("system,info HTTP server started.");
}

void setupSSDP()
{
  SSDP.setDeviceType("upnp:rootdevice");
  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("Wi-Fi Wall Clock in Zal");
  SSDP.setSerialNumber("2707202001");
  SSDP.setURL("index.html");
  SSDP.setModelName("Wall Clock Zal");
  SSDP.setModelNumber("2507202001");
  SSDP.setModelURL("https://rusua.org.ua/");
  SSDP.setManufacturer("RUSUA test devices");
  SSDP.setManufacturerURL("https://rusua.org.ua/");
  if (SSDP.begin())
  {
    udpsend("system,info SSDP service started.");
  }
}

void setupEEPROM()
{
  EEPROM.begin(512);
  debug_en = EEPROM.read(0);
  if (debug_en > 1)
  {
    EEPROM.write(0,0);
    EEPROM.commit();
    debug_en = 0;
    udpsend("system,info EEPROM value Debug initialized first time. Set to 0.");
  }
  brightness_day = EEPROM.read(1);
  if (brightness_day > 15)
  {
    EEPROM.write(1,15);
    EEPROM.commit();
    brightness_day = 15;
    udpsend("system,info EEPROM value Brightness_day initialized first time. Set to 15.");
  }
  brightness_night = EEPROM.read(2);
  if (brightness_night > 15)
  {
    EEPROM.write(2,3);
    EEPROM.commit();
    brightness_night = 1;
    udpsend("system,info EEPROM value Brightness_night initialized first time. Set to 1.");
  }
  if (debug_en == 1)
  {
    udpsend("system,info Debug is enabled in the system.");
  }
}

void setupOTA()
{
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname("Wall-Clock-Zal");
  ArduinoOTA.onStart([]() {
    udpsend("system,info Started OTA firmware update!");
    //this shows up a symbols < < < < on the screen when being OTA updating 
    int y = (matrix.height() - 8) / 2;
    matrix.drawChar(xh, y, '<', HIGH, LOW, 1);
    matrix.drawChar(xh+6, y, '<', HIGH, LOW, 1);
    matrix.drawChar(xm, y, '<', HIGH, LOW, 1);
    matrix.drawChar(xm+6, y, '<', HIGH, LOW, 1);
    matrix.write();
  });
  ArduinoOTA.begin();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("Wall-Clock-Zal");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  digitalWrite(LED_BUILTIN, HIGH);
  udpsend("system,info System just has been started!");
  setupWebserver();
  setupSSDP();
  setupEEPROM();
  setupOTA();
  if (MDNS.begin("wall-clock-bedroom"))
  {
    udpsend("system,info MDNS responder started. Hostname: Wall-Clock-Zal.");
  }
  udp.begin(123);
  Timer.attach(1,ntpTimer);
  udpsend("system,info NTP update timer activated!");
  ntpClient.begin();
  matrix.setIntensity(15);
  matrix.setRotation(0, 1);
  matrix.setRotation(3, 1);
  matrix.setRotation(2, 1);
  matrix.setRotation(1, 1);
  udpsend("system,info System started successfully!");
}

void loop() {
  if (New == 1)//impirtant function of the first launch - shows up a scrolling text --Hello!-- and syncronizing time.
  {
    TimerCounter = 600;
    ntpTimer();
    New = 0;
    if (debug_en == 1)
    {
      udpsend("system,info First launch - time successfully syncronized.");
    }
    scrollText("--Hello!--",30);
  }
  ArduinoOTA.handle();
  server.handleClient();
  MDNS.update();
  updateTime();
  matrix.fillScreen(LOW);
  int y = (matrix.height() - 8) / 2; // Centering text by vertical
  if(s & 1){matrix.drawChar(14, y, (String(":"))[0], HIGH, LOW, 1);} //printing : every odd second to make doubledots blinking
  else{matrix.drawChar(14, y, (String(" "))[0], HIGH, LOW, 1);}
  String hour1 = String (h/10);
  String hour2 = String (h%10);
  String min1 = String (m/10);
  String min2 = String (m%10);
  String sec1 = String (s/10);
  String sec2 = String (s%10);
  matrix.drawChar(xh, y, hour1[0], HIGH, LOW, 1);
  matrix.drawChar(xh+6, y, hour2[0], HIGH, LOW, 1);
  matrix.drawChar(xm, y, min1[0], HIGH, LOW, 1);
  matrix.drawChar(xm+6, y, min2[0], HIGH, LOW, 1);
  matrix.write(); 
}
