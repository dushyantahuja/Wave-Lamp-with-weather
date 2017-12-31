#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFiClient.h>

WiFiClient client;
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

// Open Weather Map API server name
const char server[] = "api.openweathermap.org";
String nameOfCity = "London,UK";
String apiKey = "*****************";

String text;

int jsonend = 0;
boolean startJson = false;

#define JSON_BUFF_DIMENSION 2500


static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
float timeZone = 0;

time_t getNtpTime();
void sendNTPpacket(IPAddress &address);

// How many leds are in the strip?
#define NUM_LEDS 30
int whiteLed = 0;
// Data pin that led data will be written out over
#define DATA_PIN 4
#define SECONDS_PER_PALETTE 120
#define BRIGHTNESS  170
#define FRAMES_PER_SECOND 30

// Clock pin only needed for SPI based chipsets when not using hardware SPI
//#define CLOCK_PIN 8

// This is an array of leds.  One item for each led in your strip.
CRGB leds[NUM_LEDS];

#define FASTLED_ESP8266_RAW_PIN_ORDER
const char ssid[] = "**********";        //your network SSID (name)
const char password[] = "********";       // your network password
bool gReverseDirection = false;
CRGBPalette16 gPal;



#include "palette.h"

// Single array of defined cpt-city color palettes.
// This will let us programmatically choose one based on
// a number, rather than having to activate each explicitly
// by name every time.
// Since it is const, this array could also be moved
// into PROGMEM to save SRAM, but for simplicity of illustration
// we'll keep it in a regular SRAM array.
//
// This list of color palettes acts as a "playlist"; you can
// add or delete, or re-arrange as you wish.
const TProgmemRGBGradientPalettePtr gGradientPalettes[] = {
  es_ocean_breeze_036_gp,  //  Blue (Used for Rain / Drizzle / etc)
  Sunset_Real_gp,          //  Red - Blue (Used for Snow)
  es_emerald_dragon_08_gp, //  Green (Used for Clear sky / Cloudy)
  rainbowsherbet_gp,       //  Rainbow (Used for error / default)
  Magenta_Evening_gp,
  Pink_Purple_gp,
  es_autumn_19_gp,
  BlacK_Blue_Magenta_White_gp,
  BlacK_Magenta_Red_gp,
  BlacK_Red_Magenta_Yellow_gp,
  Blue_Cyan_Yellow_gp
};


/*// Count of how many cpt-city gradients are defined:
const uint8_t gGradientPaletteCount =
  sizeof( gGradientPalettes) / sizeof( TProgmemRGBGradientPalettePtr );
// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 0;*/

CRGBPalette16 gCurrentPalette( CRGB::Black);
CRGBPalette16 gTargetPalette( gGradientPalettes[0] );

const size_t bufferSize = JSON_OBJECT_SIZE(8) + 130;
DynamicJsonBuffer jsonBuffer(bufferSize);


void setup() {
	  // sanity check delay - allows reprogramming if accidently blowing power w/leds
   	delay(2000);
    Udp.begin(localPort);
    setup_wifi();
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS).setDither(BRIGHTNESS < 255);
    FastLED.setBrightness( BRIGHTNESS );
    gCurrentPalette = gGradientPalettes[1];
    setSyncProvider(getNtpTime);
    setSyncInterval(3600);
    if (hour() == 0 && second() < 3) {
      setSyncProvider(getNtpTime);
    }
}


// This function runs over and over, and is where you do the magic to light
// your leds.
void loop() {
  ArduinoOTA.handle();
  EVERY_N_MILLISECONDS(160) {
    nblendPaletteTowardPalette( gCurrentPalette, gTargetPalette, 16);
  }
  EVERY_N_SECONDS( 3600 ) {
    makehttpRequest();
  }
  colorwaves( leds, NUM_LEDS, gCurrentPalette);
  if(hour() > 21 || hour() < 6)
    fill_solid( leds, NUM_LEDS, CRGB(0,0,0));
  FastLED.show();
  FastLED.delay(20);
}

void setup_wifi() {

  //delay(10);
  // We start by connecting to a WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(4000);
    ESP.restart();
  }
  ArduinoOTA.setHostname("wavelamp");
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void colorwaves( CRGB* ledarray, uint16_t numleds, CRGBPalette16& palette)
{
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 300, 1500);

  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5,9);
  uint16_t brightnesstheta16 = sPseudotime;

  for( uint16_t i = 0 ; i < numleds; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;
    uint16_t h16_128 = hue16 >> 7;
    if( h16_128 & 0x100) {
      hue8 = 255 - (h16_128 >> 1);
    } else {
      hue8 = h16_128 >> 1;
    }

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    uint8_t index = hue8;
    //index = triwave8( index);
    index = scale8( index, 240);

    CRGB newcolor = ColorFromPalette( palette, index, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (numleds-1) - pixelnumber;

    nblend( ledarray[pixelnumber], newcolor, 128);
  }
}

// Alternate rendering function just scrolls the current palette
// across the defined LED strip.
void palettetest( CRGB* ledarray, uint16_t numleds, const CRGBPalette16& gCurrentPalette)
{
  static uint8_t startindex = 0;
  startindex--;
  fill_palette( ledarray, numleds, startindex, (256 / NUM_LEDS) + 1, gCurrentPalette, 255, LINEARBLEND);
}


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1600) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      // client.publish("infinity/status",secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;

    }
  }
  //client.publish("infinity/status","NTP Time Updates");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//------------------------------------------

void makehttpRequest() {
  // close any connection before send a new request to allow client make connection to server
  client.stop();

  // if there's a successful connection:
  if (client.connect(server, 80)) {
    // Serial.println("connecting...");
    // send the HTTP PUT request:
    client.println("GET /data/2.5/forecast?q=" + nameOfCity + "&APPID=" + apiKey + "&mode=json&units=metric&cnt=2 HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("User-Agent: ArduinoWiFi/1.1");
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        //Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    char c = 0;
    while (client.available()) {
      c = client.read();
      // since json contains equal number of open and close curly brackets, this means we can determine when a json is completely received  by counting
      // the open and close occurences,
      //Serial.print(c);
      if (c == '{') {
        startJson = true;         // set startJson true to indicate json message has started
        jsonend++;
      }
      if (c == '}') {
        jsonend--;
      }
      if (startJson == true) {
        text += c;
      }
      // if jsonend = 0 then we have have received equal number of curly braces
      if (jsonend == 0 && startJson == true) {
        parseJson(text.c_str());  // parse c string text in parseJson function
        text = "";                // clear text string for the next time
        startJson = false;        // set startJson to false to indicate that a new message has not yet started
      }
    }
  }
  else {
    // if no connction was made:
    //Serial.println("connection failed");
    return;
  }
}

//to parse json data recieved from OWM
void parseJson(const char * jsonString) {
  //StaticJsonBuffer<4000> jsonBuffer;
  const size_t bufferSize = 2*JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(2) + 4*JSON_OBJECT_SIZE(1) + 3*JSON_OBJECT_SIZE(2) + 3*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(7) + 2*JSON_OBJECT_SIZE(8) + 720;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  // FIND FIELDS IN JSON TREE
  JsonObject& root = jsonBuffer.parseObject(jsonString);
  if (!root.success()) {
    Serial.println("parseObject() failed");
    return;
  }

  JsonArray& list = root["list"];
  JsonObject& nowT = list[0];
  JsonObject& later = list[1];

  // including temperature and humidity for those who may wish to hack it in

  //String city = root["city"]["name"];

  //float tempNow = nowT["main"]["temp"];
  //float humidityNow = nowT["main"]["humidity"];
  //String weatherNow = nowT["weather"][0]["description"];
  //int weatherNowID = nowT["weather"][0]["id"];

  //float tempLater = later["main"]["temp"];
  //float humidityLater = later["main"]["humidity"];
  String weatherLater = later["weather"][0]["description"];
  int weatherLaterID = later["weather"][0]["id"];

  if(weatherLaterID < 300 && weatherLaterID >=200)  // Thunderstorms
      gTargetPalette = gGradientPalettes[ 1 ];
  else if(weatherLaterID < 400 && weatherLaterID >=300)  // Drizzle
      gTargetPalette = gGradientPalettes[ 0 ];
  else if(weatherLaterID < 600 && weatherLaterID >=500)  // Rain
      gTargetPalette = gGradientPalettes[ 0 ];
  else if(weatherLaterID < 700 && weatherLaterID >=600)  // Snow
      gTargetPalette = gGradientPalettes[ 1 ];
  else if(weatherLaterID < 800 && weatherLaterID >=700)  // Fog
      gTargetPalette = gGradientPalettes[ 2 ];
  else if(weatherLaterID < 900 && weatherLaterID >=800)  // Clear / Cloudy
      gTargetPalette = gGradientPalettes[ 2 ];
  else gTargetPalette = gGradientPalettes[ 3 ];          // Was too lazy to define the additional conditions

}
