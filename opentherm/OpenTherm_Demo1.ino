/*
OpenTherm Communication Example Code
By: Ihor Melnyk
Date: January 19th, 2018

Uses the OpenTherm library to get/set boiler status and water temperature
Open serial monitor at 115200 baud to see output.

Hardware Connections (OpenTherm Adapter (http://ihormelnyk.com/pages/OpenTherm) to Arduino/ESP8266):
-OT1/OT2 = Boiler X1/X2
-VCC = 5V or 3.3V
-GND = GND
-IN  = Arduino (3) / ESP8266 (5) Output Pin
-OUT = Arduino (2) / ESP8266 (4) Input Pin

Controller(Arduino/ESP8266) input pin should support interrupts.
Arduino digital pins usable for interrupts: Uno, Nano, Mini: 2,3; Mega: 2, 3, 18, 19, 20, 21
ESP8266: Interrupts may be attached to any GPIO pin except GPIO16,
but since GPIO6-GPIO11 are typically used to interface with the flash memory ICs on most esp8266 modules, applying interrupts to these pins are likely to cause problems
*/


#include <Arduino.h>
#include "OpenTherm.h"

const int inPin = 2; //4
const int outPin = 3; //5
OpenTherm ot(inPin, outPin);

void handleInterrupt() {
	ot.handleInterrupt();
}

void setup()
{
	Serial.begin(115200);
	Serial.println("Start");
	
	ot.begin(handleInterrupt);
}

void loop()
{	
	//Set/Get Boiler Status
	bool enableCentralHeating = true;
	bool enableHotWater = true;
	bool enableCooling = false;
	unsigned long response = ot.setBoilerStatus(enableCentralHeating, enableHotWater, enableCooling);
	OpenThermResponseStatus responseStatus = ot.getLastResponseStatus();
	if (responseStatus == OpenThermResponseStatus::SUCCESS) {		
		Serial.println("Central Heating: " + String(ot.isCentralHeatingEnabled(response) ? "on" : "off"));
		Serial.println("Hot Water: " + String(ot.isHotWaterEnabled(response) ? "on" : "off"));
		Serial.println("Flame: " + String(ot.isFlameOn(response) ? "on" : "off"));
    Serial.println("Fault: " + String(ot.isFault(response) ? "Yes" : "No"));
	}
	if (responseStatus == OpenThermResponseStatus::NONE) {
		Serial.println("Error: OpenTherm is not initialized");
	}
	else if (responseStatus == OpenThermResponseStatus::INVALID) {
		Serial.println("Error: Invalid response " + String(response, HEX));
	}
	else if (responseStatus == OpenThermResponseStatus::TIMEOUT) {
		Serial.println("Error: Response timeout");
	}
 

	//Set Boiler Temperature to 64 degrees C
//	ot.setBoilerTemperature(64);

	//Get Boiler Temperature
	float temperature = ot.getBoilerTemperature();
	Serial.print("DHW temperature is ");	
  Serial.println(temperature,DEC);

  float returnTemp = ot.getReturnTemp();
  Serial.print("return temp "); 
  Serial.println(returnTemp,DEC);

  float flowRate = ot.getFlowRate();
  Serial.print("flowrate "); 
  Serial.println(flowRate,DEC);
  
  unsigned int diagcode = ot.getdiagcode();
  Serial.print("OEMdiagnosticcode is "); 
  switch (diagcode)
  {
    case 58:
    Serial.println("L2 ignition fault");
    break;
    case 60:
    Serial.println("F3 fan fault");
    break;
    case 3:
    Serial.println("F5 return thermistor fault");
    default:
    Serial.println(diagcode,DEC);
    break;
  }
  

  

 //# unsigned int diagval = 0xFF;
//  diagval = ot.sendrequest(ot.buildRequest(READ, OEMDiagnosticCode, diagval);
// diagval = ot.sendRequest(115);
 // responseStatus = ot.getLastResponseStatus();
  //if (responseStatus == OpenThermResponseStatus::SUCCESS) {
//    Serial.println("diagnostic code " + String(diagval));
  //}
  
	Serial.println();
	delay(1000);
}
