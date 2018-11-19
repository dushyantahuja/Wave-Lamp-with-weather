#define FASTLED_ESP8266_RAW_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
//#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <WorldClockClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266HTTPUpdateServer.h>

#define NUM_LEDS 30
#define DATA_PIN 4
#define BRIGHTNESS  200
#define FRAMES_PER_SECOND 60
CRGBArray<NUM_LEDS> ledsAll;
CRGBSet leds(ledsAll(0,15));
CRGBSet leds2(ledsAll(16,30));
boolean on = 1;

CRGBPalette16 gPal;
#include "palette.h"
const TProgmemRGBGradientPalettePtr gGradientPalettes[] = {
  es_ocean_breeze_036_gp,  //  Blue (Used for Cloudy)
  Sunset_Real_gp,          //  Red - Blue (Used for Snow)
  es_emerald_dragon_08_gp, //  Green (Used for Clear sky / Cloudy)
  Magenta_Evening_gp,     //  Magenta (Used for rain)
  blues_gp,                // Very Cold
  ib36_gp,                // Cold
  voxpop_gp,
  yellow_gp,
  ib_jul15_gp,
  ib04_gp,
  nsa_gp                // Used for error
};
// Count of how many cpt-city gradients are defined:
const uint8_t gGradientPaletteCount =
  sizeof( gGradientPalettes) / sizeof( TProgmemRGBGradientPalettePtr );
// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 0;

CRGBPalette16 gCurrentPalette( CRGB::Black);
CRGBPalette16 gTargetPalette( gGradientPalettes[3] );
CRGBPalette16 bCurrentPalette( CRGB::Black);
CRGBPalette16 bTargetPalette( gGradientPalettes[3] );



/*
const char ssid[] = "DUSHYANT2";        //your network SSID (name)
const char password[] = "ahuja987";       // your network password
*/

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiUDP ntpUDP;
String timeZoneIds [] = {"America/New_York", "Europe/London", "Asia/Calcutta", "Australia/Sydney"};
WorldClockClient worldClockClient("en", "UK", "E, dd. MMMMM yyyy", 4, timeZoneIds);
//NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

WiFiClient espClient;
IPAddress server(192, 168, 1, 232);
PubSubClient client(espClient, server);
void callback(const MQTT::Publish& pub);
long lastReconnectAttempt = 0, lastWifiAttempt = 0;

// This function sets up the ledsand tells the controller about them
void setup() {
    //Serial.begin(115200);
    delay(2000);
    WiFi.setAutoConnect ( true );
    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    //wifiManager.setConfigPortalTimeout(180);
    wifiManager.setTimeout(180);
    //wifiManager.setConnectTimeout(120);
    if(!wifiManager.autoConnect("WaveLamp")) {
      delay(3000);
      ESP.reset();
      delay(5000);
      }
    //setup_wifi();
    reconnect();

    MDNS.begin("WaveLamp");
    httpUpdater.setup(&httpServer);
    //httpServer.on("/time", handleRoot);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();
    MDNS.addService("http", "tcp", 80);

    //ArduinoOTA.setPassword((const char *)"avin");
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(ledsAll, NUM_LEDS);
    FastLED.setBrightness( BRIGHTNESS );
    gCurrentPalette = gGradientPalettes[3];
    bCurrentPalette = gGradientPalettes[3];
    //timeClient.begin();
    //timeClient.update();
    //Serial.println(timeClient.getHours());
    worldClockClient.updateTime();
    client.set_callback(callback);
    client.publish("wavelamp/status",worldClockClient.getFormattedTime(1));
    makehttpRequest();

    lastWifiAttempt = millis();

}

// This function runs over and over, and is where you do the magic to light
// your leds.
void loop() {
  //ArduinoOTA.handle();
  if (!client.connected()) {
    if (millis() - lastReconnectAttempt > 1000) {
      lastReconnectAttempt = millis();
    // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
  // Client connected
    client.loop();
    }
  if (WiFi.status() != WL_CONNECTED){
    WiFi.reconnect();
    if(millis() - lastWifiAttempt > 30000 && !WiFi.isConnected()) ESP.reset();  // Restart the ESP after 30 seconds
    else if(WiFi.isConnected()) lastWifiAttempt = millis();                // Reset counter if connected
  }
  EVERY_N_MILLISECONDS(300) {
    nblendPaletteTowardPalette( gCurrentPalette, gTargetPalette, 32);
    nblendPaletteTowardPalette( bCurrentPalette, bTargetPalette, 32);
  }
  EVERY_N_MINUTES( 30 ) {
    if (worldClockClient.getHours(1).toInt() <= 21 && worldClockClient.getHours(1).toInt() >= 6 && on == 1)
      makehttpRequest();
      worldClockClient.updateTime();
    //Serial.println(timeClient.getHours());
    //client.publish("wavelamp/status",timeClient.getFormattedTime());
  }
  if (worldClockClient.getHours(1).toInt() == 6 && worldClockClient.getMinutes(1).toInt() == 45) ESP.restart();
  colorwaves( leds, NUM_LEDS/2, gCurrentPalette);
  colorwaves( leds2, NUM_LEDS/2, bCurrentPalette);
  if(worldClockClient.getHours(1).toInt() > 21 || worldClockClient.getHours(1).toInt() < 6 || on == 0)
    fill_solid( leds, NUM_LEDS, CRGB(0,0,0));
  FastLED.show(); // display this frame
  FastLED.delay(10);
  httpServer.handleClient();
  yield();
}

//------------------------------------------ Weather code

#include "JsonStreamingParser.h"
#include "JsonListener.h"
#include "WeatherListner.h"
#include "CurrentWeatherListner.h"

void makehttpRequest() {
  WeatherListner wl;
  CurrentWeatherListner cwl;
  wl.updateCurrent();
  cwl.updateCurrent();
  int weatherLaterID = wl.weatherId;
  client.publish("wavelamp/status", "Weather: " + wl.weatherDescription + "\n" +
    "Weather Code: " + String(weatherLaterID)+ "\nTemp: " + String(cwl.temp));      // Look at current temperature

  if(weatherLaterID < 300 && weatherLaterID >=200)  // Thunderstorms
      gTargetPalette = gGradientPalettes[ 1 ];
  else if(weatherLaterID < 400 && weatherLaterID >=300)  // Drizzle
      gTargetPalette = gGradientPalettes[ 0 ];
  else if(weatherLaterID < 600 && weatherLaterID >=500)  // Rain
      gTargetPalette = gGradientPalettes[ 4 ];
  else if(weatherLaterID < 700 && weatherLaterID >=600)  // Snow
      gTargetPalette = gGradientPalettes[ 1 ];
  else if(weatherLaterID < 762 && weatherLaterID >=700)  // Fog
      gTargetPalette = gGradientPalettes[ 0 ];
  else if(weatherLaterID < 800 && weatherLaterID >=762)  // Squalls
          gTargetPalette = gGradientPalettes[ 1 ];
  else if(weatherLaterID < 804 && weatherLaterID >=800)  // Clear / Partially Cloudy
      gTargetPalette = gGradientPalettes[ 2 ];
  else if(weatherLaterID == 804)                        // Cloudy
      gTargetPalette = gGradientPalettes[ 0 ];
  else gTargetPalette = gGradientPalettes[ 10 ];          // Error

  if(cwl.temp < 5) bTargetPalette = gGradientPalettes[ 4 ];       //dark blue
  else if (cwl.temp <10) bTargetPalette = gGradientPalettes[ 5 ];
  else if (cwl.temp <15) bTargetPalette = gGradientPalettes[ 6 ];
  else if (cwl.temp <20) bTargetPalette = gGradientPalettes[ 7 ];
  else if (cwl.temp <25) bTargetPalette = gGradientPalettes[ 8 ]; //orange
  else bTargetPalette = gGradientPalettes[ 9 ];                  //red
}

// MQTT callback

void callback(const MQTT::Publish& pub) {
 if(pub.topic() == "wavelamp/command"){
    String payload = pub.payload_string();
    if(payload == "update"){
        makehttpRequest();
    }
    else if(payload == "stop"){
      on = 0;
    }
    /*else if (message.equals("next")) {
      gCurrentPaletteNumber = addmod8( gCurrentPaletteNumber, 1, gGradientPaletteCount);
      gTargetPalette = gGradientPalettes[ gCurrentPaletteNumber ];
      client.publish("moire1/status","Next Pattern");
    }*/
  }
}

// FastLED colorwaves

void colorwaves( CRGB* ledarray, uint16_t numleds, CRGBPalette16& palette)
{
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  //uint8_t sat8 = beatsin88( 87, 220, 250);
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

// Wifi Code

/*void setup_wifi() {
  //delay(10);
  // We start by connecting to a WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    //client.publish("wavelamp/status","Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.onStart([]() {
    client.publish("wavelamp/status","Start");
  });
  ArduinoOTA.onEnd([]() {
    client.publish("wavelamp/status","\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    client.publish("wavelamp/status","Error");
    if (error == OTA_AUTH_ERROR) client.publish("wavelamp/status","Auth Failed");
    else if (error == OTA_BEGIN_ERROR) client.publish("wavelamp/status","Begin Failed");
    else if (error == OTA_CONNECT_ERROR) client.publish("wavelamp/status","Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) client.publish("wavelamp/status","Receive Failed");
    else if (error == OTA_END_ERROR) client.publish("wavelamp/status","End Failed");
  });
  ArduinoOTA.begin();
}*/

boolean reconnect() {
    if (client.connect("Wavelamp")) {
      // Once connected, publish an announcement...
      client.publish("wavelamp/status", "Alive - subscribing to wavelamp/command, publishing to wavelamp/status");
      client.publish("wavelamp/status",ESP.getResetReason());
      // ... and resubscribe
      client.subscribe("wavelamp/command");
    }
    return client.connected();
}


void handleNotFound(){
  //digitalWrite(led, 1);
  String message = "Time: " + worldClockClient.getFormattedTime(1);
  /* message += "URI: ";
  message += httpServer.uri();
  message += "\nMethod: ";
  message += (httpServer.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += httpServer.args();
  message += "\n";
  for (uint8_t i=0; i<httpServer.args(); i++){
    message += " " + httpServer.argName(i) + ": " + httpServer.arg(i) + "\n";
  }*/
  httpServer.send(404, "text/plain", message);
  //digitalWrite(led, 0);
}
