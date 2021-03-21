#include <SPI.h>

#include <Adafruit_GFX.h>
#include "Adafruit_EPD.h"
#include "time.h"
#include <TimeLib.h>

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h> // for serializing json 
#include "settings.h"
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

const GFXfont *tfont = &FreeSans24pt7b; //font for temperature
const GFXfont *gfont = &FreeSans9pt7b; //font for gust
const GFXfont *lfont = NULL;  //tiny system font for labels

#define VERSION 2.1 // piecewise linear

// 2.13" Featherwing Monochrome 250x122 ePaper Display
Adafruit_SSD1675 display(SCREEN_WIDTH, SCREEN_HEIGHT, EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

#define BLACK EPD_BLACK

// Parameters for Refresh
int count = 0;
#define REFRESH 50 // refresh display when count exceeds REFRESH

//store for wind speed and direction
#define NA -100.0      // temp not available value
float spd[REFRESH];  //vector of speed
float dir[REFRESH];  //vector of directions
float tmp;           //last recorded temperature
float gust = 0;     //max wind speed
float temp[96];     //array to save temperature
long tmillis;       //time in milliseconds fro last temp update
#define DEAD 1800000  // 30 minutes

float scaler(float x) { //scales speed to be between 0 and SPAN
  x = constrain(x, MIN_SPEED, MAX_SPEED);
  float y = (SPAN/2)*(x-MIN_SPEED)/(M1_SPEED-MIN_SPEED);
  if (x >= M1_SPEED) y = SPAN/2 + (SPAN/2) * (x - M1_SPEED)/(MAX_SPEED - M1_SPEED);
  return y;
}

float rmin = IN_RADIUS + scaler(MIN_SPEED);
float rm1  = IN_RADIUS + scaler(M1_SPEED);
//float rm2  = IN_RADIUS + scaler(M2_SPEED);
float rmax = IN_RADIUS + scaler(MAX_SPEED);

#define BUFFER_LENGTH 256
WiFiUDP Udp;
IPAddress ipBroadcast(255,255,255,255);
IPAddress netmask(255, 255, 255, 0);
unsigned int multicastPort = 50222;

#define  OBS_X 0
#define  OBS_R 1
#define  OBS_T 2

int dsec;  //seconds since start of day
bool tnotset = true;  //time not yet sychronized
char incomingPacket[BUFFER_LENGTH]; 
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
  display.begin();
  display.clearBuffer();
  tmillis = millis();  //tmillis to detect lack of data
} //setup

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
  } //fi dsec bad
  int cperiod = constrain(dsec / 900, 0, 95);  // which 15 minute period?
  if (DEBUG) Serial.printf("in loop before switch, dsec %d, count %d, period %d, mt %d\n", dsec, count, period, mt);
  switch (mt) {
    case OBS_R: {
      spd[count] = 2.237 * obs1; //mps to mph
      dir[count] = obs2;
      gust = max(gust,spd[count]);
      count++;  // up the r packet count
    } // case r
    break;
    case OBS_T: {
      float tmp = obs1 * 1.8 + 32;  //put the temperature in array at period
      if (cperiod != period) {
        period = cperiod;
        temp[period]=tmp;
        nobs = 1;
      } else {
        temp[period] = (temp[period]*nobs + tmp)/(nobs+1); //update average
        nobs++; //update obs count
      }
      if (DEBUG) Serial.printf("Period %d\n",period);
      if (DEBUG) Serial.println(temp[period]);
    } 
    break;
  } // switch
  //if (DEBUG) Serial.printf("In loop after switch count %d, period %d\n",count, period);
  // refresh cycle
  if (count >= REFRESH) {
    refresh();
    gust=0.0;
    count = 0;
  }
 } //loop

void callDead() { //no update, kill display
  if (DEBUG) Serial.printf("Killing tmillis: %d, millis %d\n",tmillis, millis());
  display.clearBuffer();
  display.display();
  delay(DEAD);
  tmillis = millis(); //try again
}

void refresh() {
  //refresh the display
  if (DEBUG) Serial.println("Refreshing....");
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
  float rEnd = 0.0;
  // start by clearing rhe buffer
  display.clearBuffer();
  // temperature display
  char stemp[10];  
  if (temp[period] != NA) {              
    dtostrf(temp[period],4,1, stemp);
    display.setTextColor(BLACK);
    display.setFont(tfont);
    tempPrint(stemp,60,20);
  } //temp not NA
  // now for the wind display
  // first draw the circles
  display.drawCircle(X0, Y0, IN_RADIUS, BLACK);
  display.drawCircle(X0, Y0, IN_RADIUS-1, BLACK);
  // all other scales in dim
  display.setFont(lfont);
  char str[10];                
  dtostrf(MIN_SPEED,3,0, str);
  x1 = X0 + IN_RADIUS * sin(0.75*3.14159263)-5;
  y1 = Y0 - IN_RADIUS * cos(0.75*3.14159263);
  centerPrint(str, x1, y1);
  //first minor scale circle
  display.drawCircle(X0, Y0, rm1, BLACK);
  x1 = X0 + rm1 * sin(0.75*3.14159263)-5;
  y1 = Y0 - rm1 * cos(0.75*3.14159263);
  dtostrf(M1_SPEED,3,0, str);
  centerPrint(str, x1, y1);
  //outer circle
  display.drawCircle(X0, Y0, rmax, BLACK);
  x1 = X0 + rmax * sin(0.75*3.14159263);
  y1 = Y0 - rmax * cos(0.75*3.14159263);
  dtostrf(MAX_SPEED,3,0, str);
  centerPrint(str, x1, y1);
  //now for the wind lines
  for (int i=0;i<REFRESH;i++) {
     x1 = X0 + IN_RADIUS * sin(dir[i]/180*3.14159263);
     y1 = Y0 - IN_RADIUS * cos(dir[i]/180*3.14159263);
     rEnd = IN_RADIUS + scaler(spd[i]);
     x2 = X0 + rEnd * sin(dir[i]/180*3.14159263);
     y2 = Y0 - rEnd * cos(dir[i]/180*3.14159263);
     display.drawLine(x1,y1,x2,y2,BLACK);         
  } // for i
  // put the gust in wind circle
  char sgust[10];  
  if (gust < 10.0) {
      dtostrf(gust,3,1, sgust);
   } else {
      dtostrf(gust,4,1, sgust);
   }
  display.setFont(gfont);
  centerPrint(sgust,X0-2,Y0);
  drawGraph(XG, YG, WG, HG);
  // some resetting
  display.display();
} //refresh

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
            if (DEBUG) Serial.printf("In readPacket mytype %s, mt %d\n", mytype, mt);
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
                   tmillis = millis(); //update tmillis
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

 void centerPrint(const char *buf, int x, int y)
{
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
    display.setCursor(x - w / 2, y + h/2);
    display.print(buf);
}

 void tempPrint(const char *buf, int x, int y)
{
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
    display.setCursor(x - w / 2, y + h/2);
    display.print(buf);
    display.setCursor(x+w/2+7,y-h/2+14);
    display.setFont(gfont);
    display.print('O');
}

void drawGraph(int xg, int yg, int wg, int hg) {
  //draw the horizontal lines
  //draw line -2 (lowest)
  display.drawLine(xg, yg+hg/2, xg+wg, yg+hg/2, BLACK);  //lowest index -2
  display.setTextColor(BLACK);
  display.setFont(lfont);
  // x axis labels on the center line
  // AM PM
  centerPrint("AM",xg+24,yg+hg/2+2);
  centerPrint("PM",xg+72,yg+hg/2+2);
  /*
  //centerPrint("00",xg,yg);
  //centerPrint("04",xg+16,yg);
  centerPrint("08",xg+32,yg);
  //centerPrint("12",xg+48,yg);
  centerPrint("16",xg+64,yg);
  //centerPrint("20",xg+80,yg);
  centerPrint("24",xg+96,yg);
  */
  //draw horizontal lines
  display.drawLine(xg,yg+hg/2, xg+wg, yg+hg/2, BLACK); //lower line
  display.drawLine(xg, yg, xg+wg, yg, BLACK); //mid line
  display.drawLine(xg, yg-hg/2, xg+wg, yg-hg/2, BLACK); //top line
  // vertical grid lines
  display.drawLine(xg,yg+hg/2,xg,yg-hg/2, BLACK);
  display.drawLine(xg+wg/2,yg+hg/2,xg+wg/2,yg-hg/2, BLACK);
  display.drawLine(xg+wg,yg+hg/2,xg+wg,yg-hg/2, BLACK);
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
  display.setFont(lfont);
  char stemp[10];
  dtostrf(tmid-tspan/2,4,0, stemp);
  centerPrint(stemp,xg-15,yg+hg/2-7);
  dtostrf(tmid,4,0, stemp);
  centerPrint(stemp,xg-15,yg-7);
  dtostrf(tmid+tspan/2,4,0, stemp);
  centerPrint(stemp,xg-15,yg-hg/2-7);
  // now draw the graph
  if (temp[0] != NA) {
    float ty = (temp[0]-tmid)*hg/tspan; // do this separately to keep as float
    int y1 = yg - ty;  // now covert to integer
    display.drawPixel(xg,y1, BLACK);
  } // temp[0] not NA
  for (int i=1; i<= 95; i++) {
    if (temp[i] != NA ) { 
      float ty = (temp[i]-tmid)*hg/tspan; // do this separately to keep as float
      int y2 = yg - ty;  // now covert to integer
      display.drawPixel(xg+i,y2, BLACK);
      if (temp[i-1] != NA && i-1 != period) { //now draw a line, do not connect back to prior day
        float ty = (temp[i-1]-tmid)*hg/tspan; // do this separately to keep as float
        int y1 = yg - ty;  // now covert to integer
        display.drawLine(xg+i-1,y1,xg+i,y2,BLACK);
      } // fi temp[i] and temp[i-1] are not NA
    }  //fi temp[i]
  } // for each temp  
  // now a filled circle at current period
  float ty = (temp[period]-tmid)*hg/tspan;
  display.fillCircle(xg+period, yg - ty, 2, BLACK);  
} //draw graph
