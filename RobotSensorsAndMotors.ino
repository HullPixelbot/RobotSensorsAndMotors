// HullPixelbot motor controller
// Accepts commands via the serial port and acts on them to control motor movement
// and pixel colours. 
// Command protocol available at http:\\HullPixelbot.com
// Version 0.5 Rob Miles

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

#include <EEPROM.h>
#include <TimerOne.h>

//#define VERBOSE
//#define COMMAND_DEBUG


// Define if driving a WEMOS board
//#define WEMOS

#include "PixelControl.h"

#include "MotorControl.h"

#include "DistanceSensor.h"

#include "Commands.h"

void setup() {
	Serial.begin(9600);
	Serial.println(version);

	// Uncomment to test the distance sensor
//	directDistanceReadTest();

	Serial.println("Starting");
	setupRobotNotors();
	setupDistanceSensor();
	setupRemoteControl();
	startLights();
	flickeringColouredLights(220, 208, 255, 0, 200);
	transitionToRandomColor();
	randomiseLights();
	renderLights();
	startProgramExecution(STORED_PROGRAM_OFFSET);
}


void loop() {
	updateProgramExcecution();
	updateLightsAndDelay();

}
