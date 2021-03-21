#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include <TimeLib.h>
#include "MiniGrafx.h" // General graphic library
#include "ILI9341_SPI.h" // Hardware-specific library
#include <WiFiUdp.h>
#include <ArduinoJson.h> // for serializing json 
#include "settings.h"
//#include "Lato_SB_32pt7.h"
#include "Lato_SB_48pt7b.h"

#define VERSION 2.1 // with temp sparkline
// piecewise linear, back to 4 colors
//Give the colors names
#define BITS_PER_PIXEL 2 // 4 colors to keep memory down
#define BB 0
#define B1 1
#define B2 2
#define WH 3

//now define the colors
uint16_t palette[] = { 0x031F, //BB 0 dark blue
                       0x753F, //B1 1 med blue
                       0xAE3F, //B2 2 light blue
                       0xFFFF  //WH 3 white
                     };

// Parameters for Refresh
int count = 0;

#define REFRESH 50 // refresh display when count exceeds REFRESH
#define DEAD 1800000 //30 minutes
long tmillis;

//store for temp and gust
#define NA -100.0      // temp not available value
float tmp;           //last recorded temperature
float gust = 0.0;     //max wind speed
float temp[96];     //array to save temperature


float scaler(float x) { //scales speed to be between 0 and SPAN
  x = constrain(x, MIN_SPEED, MAX_SPEED);
  float y = 4 * x;
  if (x >= M1_SPEED) y = SPAN/2 + (SPAN/2) * (x - M1_SPEED)/(MAX_SPEED - M1_SPEED);
  return y;
}

float rmin = IN_RADIUS + scaler(MIN_SPEED);
float rm1  = IN_RADIUS + scaler(M1_SPEED);
float rm2  = IN_RADIUS + scaler(M2_SPEED);
float rmax = IN_RADIUS + scaler(MAX_SPEED);

//Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);  /* create tft object */
ILI9341_SPI tft = ILI9341_SPI(TFT_CS, TFT_DC);
// Create the MiniGrafx object (frame buffer object)
MiniGrafx gfx = MiniGrafx(&tft, BITS_PER_PIXEL, palette);/* create gfx object */

#define BUFFER_LENGTH 256
WiFiUDP Udp;
IPAddress ipBroadcast(255,255,255,255);
IPAddress netmask(255, 255, 255, 0);
unsigned int multicastPort = 50222;

char incomingPacket[BUFFER_LENGTH]; 

#define  OBS_X 0
#define  OBS_R 1
#define  OBS_T 2

int dsec;  //seconds since start of day
bool tnotset = true;  //time not yet sychronized
int period = -1;  // current period
long delta;  //dsec - epoch
int nobs = 0; // number of observations in current period

void setup() {
  //set all temps as na
  for (int i = 0; i<= 95; i++) temp[i] = NA;
  
  WiFi.mode(WIFI_STA); //station 
  Serial.begin(921600);  // fast SPI to display
  if (DEBUG) Serial.begin(115200); // lower the speed for the console
  if (DEBUG) Serial.printf("Connecting to %s \n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  delay(1);
  WiFi.setHostname(WIFI_HOSTNAME);
  delay(1);
  if (DEBUG) Serial.println(WiFi.getHostname());
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      if (DEBUG) Serial.print(".");
  } //while connecting
  if (DEBUG) Serial.println(" CONNECTED");
  if (DEBUG) Serial.println(WiFi.localIP());
  
  // start UDP
  Udp.begin(UDP_PORT);
  // fix delta = dsec - epoch
  delta = getDelta();
  // Initialize the driver only once

  // Turn on the background LED
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  // Initialize the driver only once
  gfx.init();
  gfx.setTextAlignment(TEXT_ALIGN_CENTER_BOTH); //text alignment
  // fill the buffer with color BB (blue background)
  gfx.fillBuffer(BB);
  windScales(); // draw the wind circles
  gfx.commit();
  tmillis = millis();  //tmillis to detect lack of data
}

void loop() {
  // put your main code here, to run repeatedly:
  if (millis()- tmillis > DEAD) callDead();
  long epoch; int mt; float obs1; float obs2;
  readPacket(mt, epoch, obs1, obs2); //get packet from hub UDP
  dsec = epoch + delta;
  while (dsec < 0 || dsec > 86400) {  //fix dsec if it is bad
    if (DEBUG) Serial.printf("bad dsec %d\n",dsec);
    delta = getDelta();  //get fresh delta
    dsec = epoch + delta;
  } //while dsec bad
  int cperiod = constrain(dsec / 900, 0, 95);  // which 15 minute period?
  if (DEBUG) Serial.printf("in loop before switch, dsec %d, count %d, period %d, mt %d\n", dsec, count, cperiod, mt);
  switch (mt) {
    case OBS_R: {
      float spd = 2.237 * obs1; //mps to mph
      float dir = obs2;
      drawWind(spd, dir);
      gust = max(gust,spd);
      count++;  // up the r packet count
    } // case r
    break;
    case OBS_T: {
      float tmp = obs1 * 1.8 + 32;
      if (cperiod != period) {
        period = cperiod;
        temp[period]=tmp;
        nobs = 1;
      } else {
        temp[period] = (temp[period]*nobs + tmp)/(nobs+1); //update average
        nobs++; //update obs count
      }
      if (DEBUG) Serial.printf("cperiod %d, Period %d, nobs %d\n",cperiod,period, nobs);
      if (DEBUG) Serial.println(temp[period]);
      writeTemp(temp[period]);
      tmillis = millis(); //update tmillis
    } 
    break;
  } // switch
  //if (DEBUG) Serial.printf("In loop after switch count %d, period %d\n",count, period);
  // refresh cycle
  if (count >= REFRESH) {
    refresh();
    gust=0.0;
    count = 0;
  } //refresh
}//loop

void callDead() { //no update, kill display
  if (DEBUG) Serial.printf("Killing tmillis: %d, millis %d\n",tmillis, millis());
  gfx.fillBuffer(BB);
  gfx.commit();
  delay(DEAD);
  tmillis = millis();  // try again
}

void drawWind(float speed, float dir) {
  int x1 = X0 + IN_RADIUS * sin(dir/180*3.14159263);
  int y1 = Y0 - IN_RADIUS * cos(dir/180*3.14159263);
  float rEnd = IN_RADIUS + scaler(speed);
  int x2 = X0 + rEnd * sin(dir/180*3.14159263);
  int y2 = Y0 - rEnd * cos(dir/180*3.14159263);
  gfx.setColor(WH); //white
  gfx.drawLine(x1,y1,x2,y2);
  gfx.commit();
}

void writeTemp(float temp) {
  char stemp[10];                
  dtostrf(temp,4,1, stemp);
  gfx.setColor(BB); //erase to background before writing
  //gfx.fillRect(XT-40,YT-15,80,30); //top left x0, y0, w, h clean out the text
  gfx.fillRect(XT-60,YT-18,120,40); //48 font top left x0, y0, w, h draw rectangle
  gfx.setColor(WH);  
  // gfx.drawRect(XT-60,YT-18,120,40); //48 font top left x0, y0, w, h draw rectangle
  gfx.setFont(Open_Sans_Bold_48);
  //gfx.drawRect(XT-40,YT-15,80,30); //32 font top left x0, y0, w, h draw rectangle
  //gfx.setFont(Lato_Bold_32);
  gfx.drawString(XT, YT, strcat(stemp,"°") );
  gfx.commit();
}
void readPacket(int &mt, long &epoch, float &obs1, float &obs2) {
  StaticJsonDocument<1024> doc;
  bool gotPacket = false; //looking for r or o packets
  while (!gotPacket) {
    int packetLength = Udp.parsePacket(); 
      if(packetLength){
        if (DEBUG) Serial.printf("Received %d B from %s, port %d\n",packetLength, 
          Udp.remoteIP().toString().c_str(), Udp.remotePort());
        int len = Udp.read(incomingPacket, BUFFER_LENGTH);
        if (len > 0){
            //incomingPacket[len] = 0;
            if (DEBUG) Serial.printf("%s\n", incomingPacket);
            // Deserialize the JSON document
            DeserializationError error = deserializeJson(doc, incomingPacket);
            // Test if parsing succeeds.
             if (error) {
              if (DEBUG) Serial.print(F("deserializeJson() failed: "));
              if (DEBUG) Serial.println(error.f_str());
              return;
            } //fi error
            const char* mytype = doc["type"];
            mt = OBS_X;
            if (strcmp(mytype,"rapid_wind")==0) mt = OBS_R;
            if (strcmp(mytype,"obs_st")==0) mt = OBS_T;
            // if (DEBUG) Serial.printf("In readPacket mytype %s, mt %d\n", mytype, mt);
            switch (mt) {
                 case OBS_R: {
                     gotPacket = true;
                     epoch = doc["ob"][0].as<long>();
                     obs1 = doc["ob"][1].as<float>();  
                     obs2 = doc["ob"][2].as<float>();
                 } // case r
                 break;
                 case OBS_T: {
                   gotPacket = true;
                   JsonArray obs_0 = doc["obs"][0];
                   epoch = obs_0[0].as<long>();
                   obs1 = obs_0[7]; 
                 } 
                 break;
            } // switch
        } // non-zero length packet
    } // fi packetLength
  } //while
} //readpacket function

long getDelta() {
  //first get epoch from hub
  int mt; long epoch; float obs1; float obs2;
    readPacket(mt, epoch, obs1, obs2);
    Udp.stop(); //stop any udp processes to get ntp
    //init and get the time
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
       if (DEBUG) Serial.println("Failed to obtain time");
     }
    if (DEBUG) Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    dsec = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
    long delta = dsec - epoch;
    if (DEBUG) Serial.printf("In getDelta dsec %d, epoch %d, delta %d\n", dsec, epoch, delta);
    //start UDP again
    Udp.begin(UDP_PORT);
  return(delta);
}


void refresh() {
  //dim the old wind lines
  int wy = YG+HG/2+13;
  for (int i = 0; i <= 240; i++) {
      for (int j = wy; j <= 320; j++) {
        gfx.setColor(constrain(gfx.getPixel(i, j)-1,0,3));
        gfx.setPixel(i, j);
      }
     }
  // redraw circles
  windScales(); // draw the circles, to undim them, also clear the gust area
  // put the gust in wind circle
  // deal with gust
  gfx.setColor(B2);  //light blue
  char sgust[10];  
  dtostrf(gust,4,1, sgust);
  gfx.setFont(ArialMT_Plain_24);
  gfx.drawString(X0, Y0, sgust );
  gust = 0.0;
  // clear the temp graph
  // origin XG, YG, height and width HG, WG
  gfx.setColor(BB);
  gfx.fillRect(0, YG-HG/2-10,240,HG+25); //account for the labels
  //gfx.setColor(WH);
  //gfx.drawRect(0, YG-HG/2-10,240,HG+25); //account for the labels
  //gfx.drawRect(0,YG+HG/2+13,240,320-HG/2-13); //the dimming rectangle
  // now redraw the temp graph
  drawGraph(XG, YG, WG, HG);
  //write to the screen
  gfx.commit();
} //refresh


void drawGraph(int xg, int yg, int wg, int hg) {
  //draw the horizontal lines
  gfx.setColor(B1); 
  //draw lines 
  gfx.drawLine(xg, yg+hg/2, xg+wg, yg+hg/2);  //lower line
  gfx.drawLine(xg, yg, xg+wg, yg); //middle line
  gfx.drawLine(xg, yg-hg/2, xg+wg, yg-hg/2); //upper line
  // x axis labels on the center line
  gfx.setColor(1);
  gfx.setFont(ArialMT_Plain_16);
  gfx.drawString(xg+wg/4,yg+hg/2+10, "AM" );
  gfx.drawString(xg+3*wg/4,yg+hg/2+10, "PM" );
  //gfx.setFont(ArialMT_Plain_16);
  //gfx.drawString(xg+32,yg, "08" );
  //gfx.drawString(xg+wg/2,yg+7, "12" );
  //gfx.drawString(xg+wg,yg+7, "24" );
  //vertical grid
  gfx.drawLine(xg,yg+hg/2,xg,yg-hg/2);
  gfx.drawLine(xg+wg/2,yg+hg/2,xg+wg/2,yg-hg/2);
  gfx.drawLine(xg+wg,yg+hg/2,xg+wg,yg-hg/2);
  //now set all the pixels for the graph
  // x depends on the period
  // y depends on the scale
  float tmin = 150.0;    // really high value, will be replaced for sure
  float tmax = -100.0;   // really low value, will be replaced for sure
  for (int i=0; i<= 95; i++) {
     if (temp[i] != NA) { 
     tmin = min(tmin, temp[i]);
     tmax = max(tmax, temp[i]);
     } // temp not NA
  } //for each temp
  if (DEBUG) Serial.println("tmin tmax");
  if (DEBUG) Serial.println(tmin);
  if (DEBUG) Serial.println(tmax);
  int tmid = (tmin + tmax)/10;
  tmid = tmid*5 + 5;
  int tspan = (tmax - tmin)/10;
  tspan = tspan * 10 + 10;
  if (tmin < tmid - tspan/2 || tmax > tmid + tspan/2) tspan = tspan + 10;
  if (DEBUG) Serial.printf("tmid %d; tspan %d\n", tmid, tspan);
  // y-axis labels
  gfx.setFont(ArialMT_Plain_16);
  char stemp[10];
  dtostrf(tmid-tspan/2,4,0, stemp);
  gfx.drawString(xg-15,yg+hg/2, stemp);
  dtostrf(tmid,4,0, stemp);
  gfx.drawString(xg-15,yg, stemp);
  dtostrf(tmid+tspan/2,4,0, stemp);
  gfx.drawString(xg-15,yg-hg/2, stemp);
  // now draw the graph
  gfx.setColor(WH); //white
  // where possible, we will draw a line
  // if that cannot be done, just the pixel will be colored
  if (temp[0] != NA) {  // temp 0 is available
    float ty = (temp[0]-tmid)*hg/tspan; // do this separately to keep as float
    int y1 = yg - ty;  // now covert to integer
    gfx.setPixel(xg,y1);
  } // temp[0] not NA
  for (int i=1; i<= 95; i++) {
    if (temp[i] != NA ) { 
      float ty = (temp[i]-tmid)*hg/tspan; // do this separately to keep as float
      int x2 = xg+i*WG/96;
      int y2 = yg - ty;  // now covert to integer
      gfx.setPixel(x2,y2);
      if (temp[i-1] != NA && i-1 != period) { //now draw a line, ignore going back to period in next day
        float ty = (temp[i-1]-tmid)*hg/tspan; // do this separately to keep as float
        int x1 = xg+(i-1)*WG/96;
        int y1 = yg - ty;  // now covert to integer
        gfx.drawLine(x1,y1,x2,y2);
      } // fi temp[i] and temp[i-1] are not NA
    }  //fi temp[i]
  } // for each temp  
  // now a filled circle at current period
  float ty = (temp[period]-tmid)*hg/tspan;
  int x1 = xg+(period)*WG/96;
  int y1 = yg - ty;
  gfx.fillCircle(x1, y1, 2);  
} //draw graph

void windScales() {  //draw the wind scales
  //wind inner circle, double width
  gfx.setColor(3);
  gfx.drawCircle(X0, Y0, IN_RADIUS);
  gfx.drawCircle(X0, Y0, IN_RADIUS-1);
  // all other scales in dim
  gfx.setColor(1);
  gfx.setFont(ArialMT_Plain_16);
  char str[10];                
  dtostrf(MIN_SPEED,3,0, str);
  int x1 = X0 + IN_RADIUS * sin(0.75*3.14159263)+5;
  int y1 = Y0 - IN_RADIUS * cos(0.75*3.14159263)+5;
  gfx.drawString(x1, y1, str );
  //first minor scale circle
  gfx.drawCircle(X0, Y0, rm1);
  x1 = X0 + rm1 * sin(0.75*3.14159263) - 7;
  y1 = Y0 - rm1 * cos(0.75*3.14159263) - 7;
  dtostrf(M1_SPEED,3,0, str);
  gfx.drawString(x1+10, y1+10, str );
  //second minor circle
  //gfx.drawCircle(X0, Y0, rm2);
  //x1 = X0 + rm2 * sin(0.75*3.14159263);
  //y1 = Y0 - rm2 * cos(0.75*3.14159263);
  //dtostrf(M2_SPEED,3,0, str);
  //gfx.drawString(x1, y1, str );
  //outer circle
  gfx.drawCircle(X0, Y0, rmax);
  x1 = X0 + rmax * sin(0.75*3.14159263)+10;
  y1 = Y0 - rmax * cos(0.75*3.14159263)+10;
  dtostrf(MAX_SPEED,3,0, str);
  gfx.drawString(x1, y1, str );
  //version number
  gfx.setFont(ArialMT_Plain_10);
  gfx.setColor(1);
  dtostrf(VERSION,3,1, str);
  gfx.drawString(20, 300, str);
  //put the word Gust in the center
  gfx.setColor(2);
  gfx.drawString(X0, Y0-18, "GUST" );
  // clear the center part
  gfx.setColor(BB);
  gfx.fillRect(X0-23, Y0-12,45,25); //top left x0, y0, w, h draw rectangle
  //trial value to check rectangle
  //gfx.setColor(3);
  //gfx.drawRect(X0-23, Y0-12,45,25); //top left x0, y0, w, h draw rectangle
  //gfx.setFont(ArialMT_Plain_24);
  //gfx.drawString(X0, Y0,"88.8"); //trial value
  // write the buffer to the display
}
