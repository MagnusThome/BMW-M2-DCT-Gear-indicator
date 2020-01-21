#include "esp32_can.h"            // https://github.com/collin80/esp32_can AND https://github.com/collin80/can_common
/*              
        NOTE: YOU MUST ACTIVATE MCP2515 AND DEACTIVATE MCP2517 IN TWO FILES IN THE ABOVE LIBRARY LIKE THIS:

        In library\esp32_can\src\esp32_can.cpp
            //MCP2517FD CAN1(5, 27);
            MCP2515 CAN1(5, 27);
      
        In library\esp32_can\src\esp32_can.h
            //extern MCP2517FD CAN1;
            extern MCP2515 CAN1;
*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "soc/soc.h"              // Disable brownout
#include "soc/rtc_cntl_reg.h"     // Disable brownout


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SOME USER SETTINGS:

#define CAR_CHARGING_TRIP_VOLTAGE 1900  // SET SO:
                                        // ABOVE THIS VALUE EQUALS VOLTAGE WHEN ENGINE IS RUNNING (CHARGING) 
                                        // BELOW THIS VALUE EQUALS VOLTAGE WHEN ENGINE IS OFF (NOT CHARGING)
//#define DISPLAY_MPH
//#define DEBUG

////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define DISPLAYMODES 4

#define BUTTON_GPIO 32
#define VOLTAGE_SENSE_GPIO 12
#define CAN1_CS_GPIO  5
#define CAN1_INT_GPIO 26
#define CAN0_RX_GPIO GPIO_NUM_16 
#define CAN0_TX_GPIO GPIO_NUM_17

#define ENGINE_RPM                  0x0C
#define VEHICLE_SPEED               0x0D
#define ENGINE_COOLANT_TEMPERATURE  0x05
#define ENGINE_OIL_TEMPERATURE      0x5C
#define CAN_REQ_ID                  0x7DF 
#define CAN_REP_ID                  0x7E8
 
uint8_t displaymode = 0;
int16_t display;
uint8_t kmh = 0;
uint8_t mph = 0;
uint16_t rpm = 0;
uint8_t rpmOBDH = 0;
uint8_t rpmOBDL = 0;
uint8_t h2o = 40;
uint8_t oil = 40;
uint8_t gear;
uint16_t ratio;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout
	Serial.begin(115200);
  Serial.println("Booting Rejsa.nu OBD2 Gear translator...");
  pinMode(BUTTON_GPIO, INPUT_PULLUP);
  startCAN();
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {

#ifdef DEBUG
  Serial.println(analogRead(VOLTAGE_SENSE_GPIO));
#endif
  if (analogRead(VOLTAGE_SENSE_GPIO) > CAR_CHARGING_TRIP_VOLTAGE) {
    requestCar();                   // REQUEST DATA FROM THE CAR IF THE ENGINE IS RUNNING (CHARGING THE BATTERY = HIGHER VOLTAGE)
  }                                 // REPLIES ARE HANDLED BY CALLBACK. DATA PUT IN GLOBAL VARS
                                    
  handleOBDDisplay();               // SEND DATA TO OBD2 DISPLAY 
  printData();                      // PRINT DATA ON SERIAL USB

  checkButton();                    // CHECK FOR BUTTON PRESS TO CHANGE DISPLAYMODE

  delay(100);                       // MAIN LOOP DELAY FOR SENDING DATA REQUESTS TO THE CAR AND REPLYING TO DATA REQUESTS FROM THE DISPLAY -- BUT ALSO DEBOUNCE FOR THE BUTTON.
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void startCAN(void) {

  CAN0.setCANPins(CAN0_RX_GPIO, CAN0_TX_GPIO);
  if(CAN0.begin()) { Serial.println("CAN0 (car): Init OK");  } 
  else {             Serial.println("CAN0 (car): Init Failed");  }
  CAN0.watchFor(CAN_REP_ID);
  CAN0.setCallback(0, fromCar);

  CAN1.setCSPin(CAN1_CS_GPIO);
  CAN1.setINTPin(CAN1_INT_GPIO);
  if(CAN1.begin()) { Serial.println("CAN1 MCP2515 (display): Init OK");  } 
  else {             Serial.println("CAN1 MCP2515 (display): Init Failed");  }
  CAN1.watchFor(CAN_REQ_ID);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void requestCar(void) {    

  static uint8_t req_cntr = 0;
  CAN_FRAME outgoing;

  outgoing.id = CAN_REQ_ID;
  outgoing.length = 8;
  outgoing.extended = 0;
  outgoing.rtr = 0;
  outgoing.data.uint8[0] = 0x02;  
  outgoing.data.uint8[1] = 0x01;  
  if      (req_cntr==0)   { outgoing.data.uint8[2] = ENGINE_COOLANT_TEMPERATURE; }  
  else if (req_cntr==1)   { outgoing.data.uint8[2] = ENGINE_OIL_TEMPERATURE; }  
  else if (req_cntr%2==0) { outgoing.data.uint8[2] = VEHICLE_SPEED; }  
  else                    { outgoing.data.uint8[2] = ENGINE_RPM; }  
  outgoing.data.uint8[3] = 0x00;
  outgoing.data.uint8[4] = 0x00;  
  outgoing.data.uint8[5] = 0x00;  
  outgoing.data.uint8[6] = 0x00;  
  outgoing.data.uint8[7] = 0x00;  

  CAN0.sendFrame(outgoing);
  printFrame(outgoing,0);
  req_cntr++;
  if (req_cntr>=100) { req_cntr = 0; }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//   * * * * CALLBACK * * * * 

void fromCar(CAN_FRAME *from_car) {

  if (from_car->data.uint8[2]==ENGINE_RPM) {
    rpmOBDH = from_car->data.uint8[3];
    rpmOBDL = from_car->data.uint8[4];
  }
  else if (from_car->data.uint8[2]==VEHICLE_SPEED)              { kmh = from_car->data.uint8[3]; }
  else if (from_car->data.uint8[2]==ENGINE_COOLANT_TEMPERATURE) { h2o = from_car->data.uint8[3]; }  
  else if (from_car->data.uint8[2]==ENGINE_OIL_TEMPERATURE)     { oil = from_car->data.uint8[3]; }  
  printFrame(*from_car, 1);
                                                          // REPACKAGE SOME OF THE DATA
  rpm = (uint16_t) ((256*rpmOBDH) + rpmOBDL)/(float)4;
  if (kmh<1)           { gear = 0; } 
  else {
    ratio = (int16_t) rpm/kmh;
    if (ratio>100)     { gear = 1; } 
    else if (ratio>58) { gear = 2; } 
    else if (ratio>42) { gear = 3; } 
    else if (ratio>31) { gear = 4; } 
    else if (ratio>25) { gear = 5; } 
    else if (ratio>21) { gear = 6; } 
    else               { gear = 7; }  
  }
  mph = (uint16_t) kmh*0.621371;

  if (displaymode>=DISPLAYMODES) { displaymode = 0; }     // WHAT DATA TO DISPLAY 
#ifdef DISPLAY_MPH
  if (displaymode == 0)      { display = mph; }
#else
  if (displaymode == 0)      { display = kmh; }
#endif                                                
  else if (displaymode == 1) { display = gear; }
  else if (displaymode == 2) { display = h2o-40; }
  else if (displaymode == 3) { display = oil-40; }
  if (display<0 || display>255) display = 0;              // General OBD2 display (=kmh display) can't display negative values like temperatures
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void handleOBDDisplay(void) { 

  CAN_FRAME incoming;       
  CAN_FRAME outgoing;

  if (!CAN1.read(incoming)) {
    return;
  }

  printFrame(incoming, 2);

  outgoing.id = CAN_REP_ID;
  outgoing.length = 8;
  outgoing.extended = 0;
  outgoing.rtr = 0;
  outgoing.data.uint8[1] = 0x41;  
  outgoing.data.uint8[5] = 0xAA;  
  outgoing.data.uint8[6] = 0xAA;  
  outgoing.data.uint8[7] = 0xAA; 

  if (incoming.id == CAN_REQ_ID) {
    if (incoming.data.uint8[2]==VEHICLE_SPEED) {
      outgoing.data.uint8[0] = 0x03;
      outgoing.data.uint8[2] = VEHICLE_SPEED;  
      outgoing.data.uint8[3] = (uint8_t) display;
      outgoing.data.uint8[4] = 0xAA;
    }
    else if (incoming.data.uint8[2]==ENGINE_RPM) {
      outgoing.data.uint8[0] = 0x04;
      outgoing.data.uint8[2] = ENGINE_RPM;  
      outgoing.data.uint8[3] = rpmOBDH; 
      outgoing.data.uint8[4] = rpmOBDL;
    } 
    else if (incoming.data.uint8[2]==ENGINE_COOLANT_TEMPERATURE) {
      outgoing.data.uint8[0] = 0x03;
      outgoing.data.uint8[2] = ENGINE_COOLANT_TEMPERATURE;  
      outgoing.data.uint8[3] = h2o;
      outgoing.data.uint8[4] = 0xAA;
    } 
    else if (incoming.data.uint8[2]==ENGINE_OIL_TEMPERATURE) {
      outgoing.data.uint8[0] = 0x03;
      outgoing.data.uint8[2] = ENGINE_OIL_TEMPERATURE;  
      outgoing.data.uint8[3] = oil;
      outgoing.data.uint8[4] = 0xAA;
    } 
    else  { // SOME UNKNOWN OTHER DATA THE DISPLAY IS REQUESTING 
      outgoing.data.uint8[0] = 0x03;
      outgoing.data.uint8[2] = incoming.data.uint8[2];  
      outgoing.data.uint8[3] = (uint8_t) display;
      outgoing.data.uint8[4] = 0xAA;
    } 
    CAN1.sendFrame(outgoing);
    printFrame(outgoing, 3);
  }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void checkButton (void) {
  
  static uint8_t waspressed = 0;
  if (!digitalRead(BUTTON_GPIO)) {
    if (!waspressed) {
      switchDisplayMode();
    }
    waspressed = 1;
  }
  else {
    waspressed = 0;
  }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void switchDisplayMode(void) {
  displaymode++;
  if (displaymode>=DISPLAYMODES) {
    displaymode = 0;
  }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void printData(void) {

#ifndef DEBUG
  Serial.println(display);
#else  
  Serial.print(display);
  Serial.print("   \tdisplaymode ");
  Serial.print(displaymode);
  Serial.print("   \tkmh ");
  Serial.print(kmh);
  Serial.print("   \tmph ");
  Serial.print(mph);
  Serial.print("   \trpm ");
  Serial.print(rpm);
  Serial.print("   \tratio ");
  Serial.print(ratio);
  Serial.print("   \tgear ");
  Serial.print(gear);
  Serial.print("   \th2o ");
  Serial.print(h2o-40);
  Serial.print("   \toil ");
  Serial.print(oil-40);
  Serial.println();
#endif
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void printFrame(CAN_FRAME &frame, uint8_t msg) {

#ifdef DEBUG
  String info[] = {"           > CAR  ", "< CAR             ", "< DISPL           ", "           > DISPL"};
  Serial.print(info[msg]);
  Serial.print("\t");
  Serial.print(frame.id,HEX);
  Serial.print("  \t");
  for(int i = 0;i < frame.length; i++) {
    Serial.print(frame.data.uint8[i],HEX);
    Serial.print("\t");
  }
  Serial.println();
#endif
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////
