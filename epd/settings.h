/* Feather ESP32 + 2.4" TFT wing */
// Pins for the EPD Monochrome 2.13" Featherwing with ESP32
#define EPD_CS     15 
#define EPD_DC     33 
#define SRAM_CS    32  
#define SD_CS      14
#define LEDPIN     13
#define LEDPINON   HIGH
#define LEDPINOFF  LOW 
#define EPD_RESET  -1 // was 5 can set to -1 and share with microcontroller Reset!
#define EPD_BUSY   -1 // was 7 can set to -1 to not use a pin (will wait a fixed delay)

// screen setup
#define SCREEN_WIDTH 250
#define SCREEN_HEIGHT 122

// WiFi Setup
#define WIFI_SSID "ssid"
#define WIFI_PASS "pw"
#define WIFI_HOSTNAME "WFPanelE"

#define UDP_PORT 50222

//Wind circle specs
#define X0 187
#define Y0 61
#define IN_RADIUS 20
#define SPAN 40 //max width of the wind donut

#define MIN_SPEED 0.0  // inner circle speed
#define M1_SPEED 5.0    // first minor circle speed
//#define M2_SPEED 15.0   // second minor circle speed
#define MAX_SPEED 25.0 //outer circle speed

//Graph Specs
#define XG 15  //origin of graph
#define YG 81  //origin of graph
#define HG 40  //height of the graph
#define WG 96 //width of the graph

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET  -18000  // five hours
#define DST_OFFSET  3600  // one hour change

#define DEBUG 0
