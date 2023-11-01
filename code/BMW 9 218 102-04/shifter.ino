#include <mcp_can.h>
#include <SPI.h>
#include <Joystick.h>

const uint8_t crc8_lut[] = {
    0x00, 0x1d, 0x3a, 0x27, 0x74, 0x69, 0x4e, 0x53, 0xe8, 0xf5, 0xd2, 0xcf,
    0x9c, 0x81, 0xa6, 0xbb, 0xcd, 0xd0, 0xf7, 0xea, 0xb9, 0xa4, 0x83, 0x9e,
    0x25, 0x38, 0x1f, 0x02, 0x51, 0x4c, 0x6b, 0x76, 0x87, 0x9a, 0xbd, 0xa0,
    0xf3, 0xee, 0xc9, 0xd4, 0x6f, 0x72, 0x55, 0x48, 0x1b, 0x06, 0x21, 0x3c,
    0x4a, 0x57, 0x70, 0x6d, 0x3e, 0x23, 0x04, 0x19, 0xa2, 0xbf, 0x98, 0x85,
    0xd6, 0xcb, 0xec, 0xf1, 0x13, 0x0e, 0x29, 0x34, 0x67, 0x7a, 0x5d, 0x40,
    0xfb, 0xe6, 0xc1, 0xdc, 0x8f, 0x92, 0xb5, 0xa8, 0xde, 0xc3, 0xe4, 0xf9,
    0xaa, 0xb7, 0x90, 0x8d, 0x36, 0x2b, 0x0c, 0x11, 0x42, 0x5f, 0x78, 0x65,
    0x94, 0x89, 0xae, 0xb3, 0xe0, 0xfd, 0xda, 0xc7, 0x7c, 0x61, 0x46, 0x5b,
    0x08, 0x15, 0x32, 0x2f, 0x59, 0x44, 0x63, 0x7e, 0x2d, 0x30, 0x17, 0x0a,
    0xb1, 0xac, 0x8b, 0x96, 0xc5, 0xd8, 0xff, 0xe2, 0x26, 0x3b, 0x1c, 0x01,
    0x52, 0x4f, 0x68, 0x75, 0xce, 0xd3, 0xf4, 0xe9, 0xba, 0xa7, 0x80, 0x9d,
    0xeb, 0xf6, 0xd1, 0xcc, 0x9f, 0x82, 0xa5, 0xb8, 0x03, 0x1e, 0x39, 0x24,
    0x77, 0x6a, 0x4d, 0x50, 0xa1, 0xbc, 0x9b, 0x86, 0xd5, 0xc8, 0xef, 0xf2,
    0x49, 0x54, 0x73, 0x6e, 0x3d, 0x20, 0x07, 0x1a, 0x6c, 0x71, 0x56, 0x4b,
    0x18, 0x05, 0x22, 0x3f, 0x84, 0x99, 0xbe, 0xa3, 0xf0, 0xed, 0xca, 0xd7,
    0x35, 0x28, 0x0f, 0x12, 0x41, 0x5c, 0x7b, 0x66, 0xdd, 0xc0, 0xe7, 0xfa,
    0xa9, 0xb4, 0x93, 0x8e, 0xf8, 0xe5, 0xc2, 0xdf, 0x8c, 0x91, 0xb6, 0xab,
    0x10, 0x0d, 0x2a, 0x37, 0x64, 0x79, 0x5e, 0x43, 0xb2, 0xaf, 0x88, 0x95,
    0xc6, 0xdb, 0xfc, 0xe1, 0x5a, 0x47, 0x60, 0x7d, 0x2e, 0x33, 0x14, 0x09,
    0x7f, 0x62, 0x45, 0x58, 0x0b, 0x16, 0x31, 0x2c, 0x97, 0x8a, 0xad, 0xb0,
    0xe3, 0xfe, 0xd9, 0xc4
};

#define CAN_INT 2 // INT pin
MCP_CAN CAN(8); // CS pin
Joystick_ Joystick;

bool shifter_init_test_done = false;

bool shifter_released = true;
bool parking_button_released = true;

char msgString[128];
char lever_position_code[128];
char parking_position_code[128];
uint8_t status_counter = 0;

// CAN message
long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];

// Shifter status codes for GWS
byte shifter_status_parking = 0x20;
byte shifter_status_reverse = 0x40;
byte shifter_status_neutral = 0x60;
byte shifter_status_drive   = 0x81;

// Lever position/movement codes
String shifter_middle           = "0x0F";
String shifter_middle_move_down = "0x3F";
String shifter_middle_move_up   = "0x1F";
String shifter_side             = "0x7F";
String shifter_side_move_down   = "0x6F";
String shifter_side_move_up     = "0x5F";
String shifter_parking          = "0xD5";

// Shifter pattern and entry shifter position/gear
const byte shifter_schema[3] = {shifter_status_reverse, shifter_status_neutral, shifter_status_drive};
uint8_t shifter_schema_position = 1;

bool shifter_mode_auto = true;

byte shifter_status_initial = shifter_status_neutral;
byte shifter_status_current = shifter_status_initial;

uint8_t crc8(uint8_t crc, uint8_t const *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t data = buf[i] ^ crc;
        crc = crc8_lut[data] ^ (crc << 8);
    }
    return crc ^ 0x70;
}

void turnBacklight()
{
  // 0x202 code is backlight turning code
  byte payload[5] = {0xFF, 0x00, 0x00};
  CAN.sendMsgBuf(0x202, 0, 4, payload);
}

void pressButton(uint8_t button_id)
{
  Joystick.pressButton(button_id);
  delay(50);
  Joystick.releaseButton(button_id);
}

void setShifterStatus(byte status_code)
{
  uint8_t buf[] = {status_counter, status_code, 0x00, 0x00};
  uint8_t crc = crc8(0, buf, 4);
  byte payload[5] = {crc, status_counter, status_code, 0x00, 0x00};

  CAN.sendMsgBuf(0x3FD, 0, 8, payload);

  status_counter++;
  if ((status_counter & 0xF) == 0xF) status_counter++;
}

void shifterInitTest(long unsigned int rxId) {
  if((rxId & 0x197) == 0x197) {
    setShifterStatus(shifter_status_neutral);
    delay(400);
    setShifterStatus(shifter_status_parking);
    delay(400);
    setShifterStatus(shifter_status_neutral);
    delay(400);
    setShifterStatus(shifter_status_parking);
    delay(400);
  }
  shifter_init_test_done = true;
}

void printDebugMessages(long unsigned int rxId, unsigned char len, unsigned char rxBuf[8]) {
    sprintf(msgString, "Standard ID: 0x%.3lX       DLC: %1d  Data:", rxId, len);
    Serial.print(msgString);

    for(byte i = 0; i<len; i++){
      sprintf(msgString, " 0x%.2X", rxBuf[i]);
      Serial.print(msgString);
    }
    Serial.println();
}

void setup()
{
  Serial.begin(9600);
  Joystick.begin();

  CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  CAN.setMode(MCP_NORMAL);

  turnBacklight();
}

void loop()
{
  // delay(100);

  if(!digitalRead(CAN_INT))
  {
    // Read CAN message
    CAN.readMsgBuf(&rxId, &len, rxBuf);
    sprintf(lever_position_code, "0x%.2X", rxBuf[2]);
    sprintf(parking_position_code, "0x%.2X", rxBuf[3]);

    // Perform CAN broadcast test
    if(shifter_init_test_done == false) shifterInitTest(rxId);

    printDebugMessages(rxId, len, rxBuf);

    // Set and update Shifter status continously
    setShifterStatus(shifter_status_current);

    // Detect switch from auto-mode to semi-auto-mode and vice versa
    // On switching main Shifter mode press the same button
    if(String(lever_position_code) == shifter_side && shifter_mode_auto == true) {
      shifter_mode_auto = false;
      pressButton(2);
    }
    else if(String(lever_position_code) == shifter_middle && shifter_mode_auto == false) {
      shifter_mode_auto = true;
      pressButton(2);
    }
  
    // Shifter auto-mode gear up
    if(String(lever_position_code) == shifter_middle_move_down && (shifter_schema_position + 1) <= 2 && shifter_released == true) {
      shifter_released = false;
      shifter_schema_position += 1;
      shifter_status_current = shifter_schema[shifter_schema_position];
      pressButton(0);
    }
    // Shifter auto-mode gear down
    else if(String(lever_position_code) == shifter_middle_move_up && (shifter_schema_position -1) >= 0 && shifter_released == true) {
      shifter_released = false;
      shifter_schema_position -= 1;
      shifter_status_current = shifter_schema[shifter_schema_position];
      pressButton(1);
    }

    // Shifter semi-auto-mode gear up
    if(String(lever_position_code) == shifter_side_move_down && shifter_released == true) {
      shifter_released = false;
      pressButton(0);
    }
    // Shifter semi-auto-mode gear down
    else if(String(lever_position_code) == shifter_side_move_up && shifter_released == true) {
      shifter_released = false;
      pressButton(1);
    }

    // Shifter parking button pressed 
    if(String(parking_position_code) == shifter_parking && parking_button_released == true) {
      parking_button_released = false;
      pressButton(3);
    }

    // Release buttons
    if(String(lever_position_code) == shifter_middle || String(lever_position_code) == shifter_side) shifter_released = true;
    if(String(parking_position_code) == "0xC0") parking_button_released = true;

  }
}
