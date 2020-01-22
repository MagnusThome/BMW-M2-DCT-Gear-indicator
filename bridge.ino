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




//      This is just a simple bridge example where all traffic is forwarded in both directions between the two CAN bus subnets.
//      You can watch all packets or add code to create filters, data translations o whatever...





#define CAN1_CS_GPIO  5
#define CAN1_INT_GPIO 26
#define CAN0_RX_GPIO GPIO_NUM_16 
#define CAN0_TX_GPIO GPIO_NUM_17



void setup() {
	Serial.begin(115200);
  Serial.println("\nBooting DUAL CAN Bridge");

  CAN0.setCANPins(CAN0_RX_GPIO, CAN0_TX_GPIO);
  if(CAN0.begin()) { Serial.println("CAN0 builtin Init OK"); } 
  else {             Serial.println("CAN0 builtin Init Failed"); }

  CAN1.setCSPin(CAN1_CS_GPIO);
  CAN1.setINTPin(CAN1_INT_GPIO);
  if(CAN1.begin()) { Serial.println("CAN1 MCP2515 Init OK"); } 
  else {             Serial.println("CAN1 MCP2515 Init Failed"); }
  
  CAN0.watchFor();
  CAN1.watchFor();
}



void loop() {
  
  CAN_FRAME message;
  
  if (CAN0.read(message)) {
    CAN1.sendFrame(message);
    Serial.print("CAN0\t");
		printFrame(message);
	}
	
	if (CAN1.read(message)) {
    CAN0.sendFrame(message);
    Serial.print("CAN1\t");
		printFrame(message);
	}
 
}



void printFrame(CAN_FRAME &frame)
{
  Serial.print(frame.id,HEX);
  Serial.print("      \tfid:");
  Serial.print(frame.fid);
  Serial.print("  \ttime:");
  Serial.print(frame.timestamp);
  Serial.print("  \trtr:");
  Serial.print(frame.rtr);
  Serial.print("  \tpri:");
  Serial.print(frame.priority);
  Serial.print("  \text:");
  Serial.print(frame.extended);
  Serial.print("  \tlen:");
  Serial.print(frame.length,DEC);
  Serial.print("  \tdata:  ");
  for(int i = 0;i < frame.length; i++) {
    Serial.print(frame.data.uint8[i],HEX);
    Serial.print(",");
  }
  Serial.println();
}






////////////////////////////////////////////////////////
