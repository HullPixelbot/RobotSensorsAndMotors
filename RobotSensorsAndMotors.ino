// HullPixelbot motor controller
// Accepts commands via the serial port and acts on them to control motor movement
// and pixel colours. 
// Command protocol available at https://github.com/HullPixelbot/HullPixelbotCode
// Version 1.5 Rob Miles

// Physical connections for Arduino Pro Mini


// Left motor D4:  Blue   - 28BYJ48 pin 1
// Left motor D5:  Pink   - 28BYJ48 pin 2
// Left motor D6:   Yellow - 28BYJ48 pin 3
// Left motor D7:   Orange - 28BYJ48 pin 4

// Right motor D8:  Blue   - 28BYJ48 pin 1
// Right motor D9:  Pink   - 28BYJ48 pin 2
// Right motor D10:   Yellow - 28BYJ48 pin 3
// Right motor D11:   Orange - 28BYJ48 pin 4

// Neopixel control D12

// Distance sensor trigger 3, echo 2

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <TimerOne.h>

//#define VERBOSE
//#define COMMAND_DEBUG


// Define if driving a WEMOS board (not fully tested)
//#define WEMOS

#include "Storage.h"

#include "PixelControl.h"

#include "MotorControl.h"

#include "DistanceSensor.h"

#include "Commands.h"

void setup() {
	Serial.begin(1200);
	Serial.println(version);

	// Uncomment to test the distance sensor
	// Repeatedly sends readings
	//testDistanceSensor();

	Serial.println(F("Starting"));
	setupMotors();
	setupDistanceSensor(25);
	setupRemoteControl();
	startLights();
	displayBusyPixelWait(6, 0, 255, 255);
	startProgramExecution(STORED_PROGRAM_OFFSET);
}


void loop() {
	updateProgramExcecution();
	updateDistanceSensor();
//	Serial.println(".");
//	int x = getDistanceValueInt();
//	Serial.println(x);
	updateLightsAndDelay(!commandsNeedFullSpeed());
}
