/* Feather ESP32 + 2.4" TFT wing */
// Pins for the ILI9341
#define STMPE_CS 32
#define TFT_CS   15
#define TFT_DC   33
#define SD_CS    14
#define TFT_LED 5

// screen setup
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// WiFi Setup
#define WIFI_SSID "dewan"
#define WIFI_PASS "cheerful"
#define WIFI_HOSTNAME "WFPanel1"

//Wind circle specs
#define X0 120
#define Y0 225
#define IN_RADIUS 30
#define SPAN 60 //max width of the wind donut
#define MIN_SPEED 0.0  // inner circle speed
#define M1_SPEED 5.0    // first minor circle speed
#define M2_SPEED 15.0   // second minor circle speed
#define MAX_SPEED 25.0 //outer circle speed

//center of temp display
#define XT 120
#define YT 22

//Graph Specs
#define XG 30  //origin of graph
#define YG 85  //origin of graph
#define HG 60  //height of the graph
#define WG 192 //width of the graph

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET  -18000  // five hours
#define DST_OFFSET  3600  // one hour change

#define UDP_PORT 50222
#define DEBUG 0
