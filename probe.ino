
#include <U8g2lib.h>

// Инициализация дисплея
U8X8_SSD1306_128X32_UNIVISION_HW_I2C u8x8(/* reset=*/ 16, /* clock=*/ 5, /* data=*/ 4);

#include <Arduino.h>

extern "C" {
  #include <user_interface.h>
}

#define DATA_LENGTH           112

#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x04

#define MAX_MAC_ADDR 250
#define RSSI_MIN 6
#define RSSI_MAX 7

#define RSSI_THRESHOLD 5
uint8_t macAddr[MAX_MAC_ADDR][8];
int NumberOfMacs = 0;

char emptyStr[] = "                ";

struct RxControl {
 signed rssi:8; // signal intensity of packet
 unsigned rate:4;
 unsigned is_group:1;
 unsigned:1;
 unsigned sig_mode:2; // 0:is 11n packet; 1:is not 11n packet;
 unsigned legacy_length:12; // if not 11n packet, shows length of packet.
 unsigned damatch0:1;
 unsigned damatch1:1;
 unsigned bssidmatch0:1;
 unsigned bssidmatch1:1;
 unsigned MCS:7; // if is 11n packet, shows the modulation and code used (range from 0 to 76)
 unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not
 unsigned HT_length:16;// if is 11n packet, shows length of packet.
 unsigned Smoothing:1;
 unsigned Not_Sounding:1;
 unsigned:1;
 unsigned Aggregation:1;
 unsigned STBC:2;
 unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not.
 unsigned SGI:1;
 unsigned rxend_state:8;
 unsigned ampdu_cnt:8;
 unsigned channel:4; //which channel this packet in.
 unsigned:12;
};

struct SnifferPacket{
    struct RxControl rx_ctrl;
    uint8_t data[DATA_LENGTH];
    uint16_t cnt;
    uint16_t len;
};

// Declare each custom function (excluding built-in, such as setup and loop) before it will be called.
// https://docs.platformio.org/en/latest/faq.html#convert-arduino-file-to-c-manually
static void showMetadata(SnifferPacket *snifferPacket);
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length);
static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data);
static void getMAC(char *addr, uint8_t* data, uint16_t offset);
static void getsmallMAC(char *addr, uint8_t* data, uint16_t offset);
static bool updateMacArray(uint8_t* data, uint16_t offset);
static uint8_t calcActiveMacs();
void channelHop();


static void showMetadata(SnifferPacket *snifferPacket) {

  unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

  uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
  uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
  uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
  uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
  uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;

  // Only look for probe request packets
  if (frameType != TYPE_MANAGEMENT ||
      frameSubType != SUBTYPE_PROBE_REQUEST)
        return;

  Serial.println("updateMacArray");
  uint8_t rssi = abs(snifferPacket->rx_ctrl.rssi);
  if (updateMacArray(snifferPacket->data, 10, rssi))
  {
    char cstr[20];
    u8x8.setFont(u8x8_font_amstrad_cpc_extended_r);
    
    for (int i = 0; i<4; i++)
    {
        u8x8.drawString(0, i, emptyStr);
    }
    
    char addr[] = "00:00:00";
    getsmallMAC(addr, snifferPacket->data, 10);
    sprintf(cstr, " Mac: %s", addr);
    u8x8.drawString(0, 0, cstr);

    sprintf(cstr, " RSSI  %i", snifferPacket->rx_ctrl.rssi);
    u8x8.drawString(0, 1, cstr);
    
    sprintf(cstr, " Ch: %i", wifi_get_channel());
    u8x8.drawString(0, 2, cstr);

    sprintf(cstr, " Dev: %i", NumberOfMacs);
    u8x8.drawString(0, 3, cstr);

    uint8_t activeDevices = calcActiveMacs();
    sprintf(cstr, " Dev: %i Act: %i", NumberOfMacs, activeDevices);
    u8x8.drawString(0, 3, cstr);
    
  //Serial.print(" Peer MAC: ");
  //Serial.print(addr);

  //uint8_t SSID_length = snifferPacket->data[25];
 // Serial.print(" SSID: ");

  //printDataSpan(26, SSID_length, snifferPacket->data);

  //Serial.println();
  }
  else
  {
        char cstr[20];
        u8x8.setFont(u8x8_font_amstrad_cpc_extended_r);
        u8x8.drawString(0, 3, emptyStr);
        
        uint8_t activeDevices = calcActiveMacs();
        sprintf(cstr, " Dev: %i Act: %i", NumberOfMacs, activeDevices);
        u8x8.drawString(0, 3, cstr);
  }
}

/**
 * Callback for promiscuous mode
 */
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data) {
  for(uint16_t i = start; i < DATA_LENGTH && i < start+size; i++) {
    Serial.write(data[i]);
  }
}

static void getMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", data[offset+0], data[offset+1], data[offset+2], data[offset+3], data[offset+4], data[offset+5]);
}

static void getsmallMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x", data[offset+3], data[offset+4], data[offset+5]);
}

static bool updateMacArray(uint8_t* data, uint16_t offset, uint8_t rssi) 
{
  Serial.print("rssi: ");
  Serial.println(rssi);

  if (NumberOfMacs == 0)
  {
    for (int j = 0; j<6; j++)
    {
      macAddr[NumberOfMacs][j] = data[offset+j];
    }

    macAddr[NumberOfMacs][RSSI_MIN] = rssi;
    macAddr[NumberOfMacs][RSSI_MAX] = rssi;
    
    NumberOfMacs++;
    return true;
  }

  // Is this Mac new or not
  bool newMacFound = true;

  for (int i = 0; i < NumberOfMacs; i++)
  {
        
    bool macEqual = true;
    for (int j = 0; j < 6; j++)
    {      
      // Compare byte by byte
      if (macAddr[i][j] != data[offset+j])  
      {      
        macEqual = false;
        break;
      }
    }
        
    if (macEqual)
    {
      // Already have this mac
      newMacFound = false;

      // Compare Rssi
      if (macAddr[i][RSSI_MIN] > rssi)
      {
         macAddr[i][RSSI_MIN] = rssi;
      }

      if (macAddr[i][RSSI_MAX] < rssi)
      {
         macAddr[i][RSSI_MAX] = rssi;
      }
      
      break;
    }
  }
  
  if (newMacFound)
  {        
    // Copy mac address to array   
    for (int j = 0; j<6; j++)
    {
      macAddr[NumberOfMacs][j] = data[offset+j];
    }

    macAddr[NumberOfMacs][RSSI_MIN] = rssi;
    macAddr[NumberOfMacs][RSSI_MAX] = rssi;
    NumberOfMacs++;
  }

  return newMacFound;
}

static uint8_t calcActiveMacs() 
{
  int activeMacs = 0;
    
  for (int i = 0; i < NumberOfMacs; i++)
  {
    Serial.print("Rssi min max ");
    Serial.println(macAddr[i][RSSI_MIN]);
    Serial.println(macAddr[i][RSSI_MAX]);
    
    if ((macAddr[i][RSSI_MAX] - macAddr[i][RSSI_MIN]) >  RSSI_THRESHOLD)
    {
      activeMacs++;    
    }        
  }

  return activeMacs;
}

#define CHANNEL_HOP_INTERVAL_MS   1000
static os_timer_t channelHop_timer;

/**
 * Callback for channel hoping
 */
void channelHop()
{
  // hoping channels 1-13
  uint8 new_channel = wifi_get_channel() + 1;
  if (new_channel > 13) {
    new_channel = 1;
  }
  wifi_set_channel(new_channel);
}

#define DISABLE 0
#define ENABLE  1

void setup() {
  // set the WiFi chip to "promiscuous" mode aka monitor mode
  Serial.begin(115200);
  delay(10);
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(1);
  wifi_promiscuous_enable(DISABLE);
  delay(10);
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  delay(10);
  wifi_promiscuous_enable(ENABLE);

  u8x8.begin();
  u8x8.setFont(u8x8_font_amstrad_cpc_extended_r);
  char cstr[] = "Searching...";   
  u8x8.drawString(0, 1, cstr);

  Serial.print(cstr);
  
  // setup the channel hoping callback timer
  os_timer_disarm(&channelHop_timer);
  os_timer_setfn(&channelHop_timer, (os_timer_func_t *) channelHop, NULL);
  os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL_MS, 1);
}

void loop() {
  delay(10);
}
