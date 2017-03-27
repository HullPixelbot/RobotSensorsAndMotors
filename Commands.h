
// Stored program management

#define STATEMENT_CONFIRMATION 1
#define LINE_NUMBERS 2
#define ECHO_DOWNLOADS 4
#define DUMP_DOWNLOADS 8

enum ProgramState
{
	PROGRAM_STOPPED,
	PROGRAM_PAUSED,
	PROGRAM_ACTIVE,
	PROGRAM_AWAITING_MOVE_COMPLETION,
	PROGRAM_AWAITING_DELAY_COMPLETION,
	SYSTEM_CONFIGURATION_CONNECTION // will never enter this state
};

enum DeviceState
{
	ACCEPTING_COMMANDS,
	DOWNLOADING_CODE
};

ProgramState programState = PROGRAM_STOPPED;
DeviceState deviceState = ACCEPTING_COMMANDS;

byte diagnosticsOutputLevel = 0;

long delayEndTime;

#define COMMAND_BUFFER_SIZE 60

// Set command terminator to CR

#define STATEMENT_TERMINATOR 0x0D

// Set program terminator to string end
// This is the EOT character
#define PROGRAM_TERMINATOR 0x00

char programCommand[COMMAND_BUFFER_SIZE];
char * commandPos;
char * commandLimit;
char * bufferLimit;
char * decodePos;
char * decodeLimit;

char remoteCommand[COMMAND_BUFFER_SIZE];
char * remotePos;
char * remoteLimit;

///////////////////////////////////////////////////////////
/// Serial comms 
///////////////////////////////////////////////////////////

int CharsAvailable()
{
	return Serial.available();
}

byte GetRawCh()
{
	int ch;
	do
	{
		ch = Serial.read();
	} while (ch < 0);

	return (byte)ch;
}

// Current position in the EEPROM of the execution
int programCounter;

// Start position of the code as stored in the EEPROM
int programBase;

// Write position when downloading and storing program code
int programWriteBase;

// Write position for any incoming program code
int bufferWritePosition;

// Checksum for the download
byte downloadChecksum;

// Dumps the program as stored in the EEPROM

void dumpProgramFromEEPROM(int EEPromStart)
{
	Serial.println(F("Current program: "));

	char byte;
	while (byte = EEPROM.read(EEPromStart++))
	{
		if (byte == STATEMENT_TERMINATOR)
			Serial.println();
		else
			Serial.print(byte);

		if (byte == PROGRAM_TERMINATOR)
		{
			Serial.println(F("Reached end of program"));
			break;
		}

		if (EEPromStart >= EEPROM_SIZE)
		{
			Serial.println(F("Reached end of eeprom"));
			break;
		}
	}
}

// Starts a program running at the given position

void startProgramExecution(int programPosition)
{
	if (isProgramStored())
	{

#ifdef PROGRAM_DEBUG
		Serial.print(F(".Starting program execution at: "));
		Serial.println(programPosition);
#endif
		programCounter = programPosition;
		programBase = programPosition;
		programState = PROGRAM_ACTIVE;
	}
}

// RH - remote halt

void haltProgramExecution()
{
#ifdef PROGRAM_DEBUG
	Serial.print(F(".Ending program execution at: "));
	Serial.println(programCounter);
#endif

	programState = PROGRAM_STOPPED;
}


// RP - pause program

void pauseProgramExecution()
{
#ifdef PROGRAM_DEBUG
	Serial.print(".Pausing program execution at: ");
	Serial.println(programCounter);
#endif

	programState = PROGRAM_PAUSED;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("RPOK"));
	}
}

// RR - resume running program

void resumeProgramExecution()
{
#ifdef PROGRAM_DEBUG
	Serial.print(".Resuming program execution at: ");
	Serial.println(programCounter);
#endif

	if (programState == PROGRAM_PAUSED)
	{
		// Can resume the program
		programState = PROGRAM_ACTIVE;
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("RROK"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("RRFail:"));
			Serial.println(programState);
		}
	}
}

enum lineStorageState
{
	LINE_START,
	GOT_R,
	GOT_RX,
	STORING,
	SKIPPING
};

lineStorageState lineStoreState;

void resetLineStorageState()
{
	lineStoreState = LINE_START;
}


// Called to start the download of program code
// each byte that arrives down the serial port is now stored in program memory
//
void startDownloadingCode(int downloadPosition)
{
#ifdef PROGRAM_DEBUG
	Serial.println(".Starting code download");
#endif

	// Stop the current program
	haltProgramExecution();

	deviceState = DOWNLOADING_CODE;

	programWriteBase = downloadPosition;

	resetLineStorageState();

	startBusyPixel(128,128,128);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("RMOK"));
	}
}

// Called when a byte is received from the host when in program storage mode
// Adds it to the stored program, updates the stored position and the counter
// If the byte is the terminator byte (zero) it changes to the "wait for checksum" state
// for the program checksum

//#define STORE_RECEIVED_BYTE_DEBUG

void storeReceivedByte(byte b)
{
	// ignore odd characters - except for CR

	if (b < 32 | b>128)
	{
		if (b != STATEMENT_TERMINATOR)
			return;
	}

	switch (lineStoreState)
	{

	case LINE_START:
		// at the start of a line - look for an R command

		if (b == 'r' | b == 'R')
		{
			lineStoreState = GOT_R;
		}
		else
		{
			lineStoreState = STORING;
		}
		break;

	case GOT_R:
		// Last character was an R - is this an X?

		if (b == 'x' | b == 'X')
		{
			lineStoreState = GOT_RX;
		}
		else
		{
			// Not an X - but we never store R commands
			// skip to the next line
			lineStoreState = SKIPPING;
		}
		break;

	case GOT_RX:
		// Got an RX command - we are absorbing the terminator
		// tidy up and start the program

		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("RXOK"));
		}

		stopBusyPixel();

		// enable immediate command receipt

		deviceState = ACCEPTING_COMMANDS;

		setProgramStored();

		// put the terminator on the end

		storeByteIntoEEPROM(PROGRAM_TERMINATOR, programWriteBase++);

		if (diagnosticsOutputLevel & DUMP_DOWNLOADS)
		{
			dumpProgramFromEEPROM(STORED_PROGRAM_OFFSET);
		}

		startProgramExecution(STORED_PROGRAM_OFFSET);

		break;

	case SKIPPING:
		// we are skipping an R command - look for a statement terminator
		if (b == STATEMENT_TERMINATOR)
		{
			// Got a terminator, look for the command character
			lineStoreState = LINE_START;
		}
		break;

	case STORING:
		// break out- storing takes place next
		break;
	}

	if (lineStoreState == STORING)
	{
		// get here if we are storing or just got a line start

		// if we get here we store the byte
		storeByteIntoEEPROM(b, programWriteBase++);

		if (diagnosticsOutputLevel & ECHO_DOWNLOADS)
		{
			Serial.print((char)b);
		}

		if (b == STATEMENT_TERMINATOR)
		{
			// Got a terminator, look for the command character
			if (diagnosticsOutputLevel & ECHO_DOWNLOADS)
			{
				Serial.println();
			}
			lineStoreState = LINE_START;
			// look busy
			updateBusyPixel();
		}
	}

}

#ifdef COMMAND_DEBUG
#define READ_INTEGER_DEBUG
#endif

int readInteger()
{
#ifdef READ_INTEGER_DEBUG
	Serial.println(".**readInteger");
#endif
	int sign = 1;
	int result = 0;

	if (*decodePos == '-')
	{
#ifdef READ_INTEGER_DEBUG
		Serial.println(".  negative number");
#endif
		sign = -1;
		decodePos++;
	}

	if (*decodePos == '+')
	{
#ifdef READ_INTEGER_DEBUG
		Serial.println(".  positive number");
#endif
		decodePos++;
	}

	while (decodePos != decodeLimit)
	{
		char ch = *decodePos;

#ifdef READ_INTEGER_DEBUG
		Serial.print(".  processing: ");
		Serial.println((char)ch);
#endif

		if (ch<'0' | ch>'9')
		{
#ifdef READ_INTEGER_DEBUG
			Serial.println(".  not a digit ");
#endif
			break;
		}

		result = (result * 10) + (ch - '0');

#ifdef READ_INTEGER_DEBUG
		Serial.print(".  result: ");
		Serial.println(result);
#endif

		decodePos++;
	}

	result = result * sign;

#ifdef READ_INTEGER_DEBUG
	Serial.print(".  returning: ");
	Serial.println(result);
#endif

	return result;
}

void resetCommand()
{
#ifdef COMMAND_DEBUG
	Serial.println(".**resetCommand");
#endif
	commandPos = programCommand;
	bufferLimit = commandPos + COMMAND_BUFFER_SIZE;
}

#ifdef COMMAND_DEBUG
#define MOVE_FORWARDS_DEBUG
#endif

// Command MFddd,ttt - move distance ddd over time ttt (ttt expressed in "ticks" - tenths of a second)
// Return OK

void remoteMoveForwards()
{
	int forwardMoveDistance;

#ifdef MOVE_FORWARDS_DEBUG
	Serial.println(".**moveForwards");
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: no dist"));
		}
		return;
	}
		
	forwardMoveDistance = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MFOK"));
		}
		fastMoveDistanceInMM(forwardMoveDistance, forwardMoveDistance);
		return;
	}

	decodePos++; // move past the separator

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: no time"));
		}
		return;
	}

	int forwardMoveTime = readInteger();

	int moveResult = timedMoveDistanceInMM(forwardMoveDistance, forwardMoveDistance, (float)forwardMoveTime/10.0);

	if (moveResult == 0)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MFOK"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MFFail"));
		}
	}
}

// Command MAradius,angle,time - move arc. 
// radius - radius of the arc to move
// angle of the arc to move
// time - time for the move
//
// Return OK

//#define MOVE_ANGLE_DEBUG

void remoteMoveAngle()
{
#ifdef MOVE_ANGLE_DEBUG
	Serial.println(".**moveAngle");
#endif

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MAFail: no radius"));
		}
		return;
	}

	int radius = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MAFail: no angle"));
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MAFail: no angle"));
		}
		return;
	}

	int angle = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MAOK"));
		}
		fastMoveArcRobot(radius, angle);
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MAFail: no time"));
		}
		return;
	}

	int time = readInteger();

#ifdef MOVE_ANGLE_DEBUG
	Serial.print("    radius: ");
	Serial.print(radius);
	Serial.print(" angle: ");
	Serial.print(angle);
	Serial.print(" time: ");
	Serial.println(time);
#endif

	int reply = timedMoveArcRobot(radius, angle, time / 10.0);

	if (reply == 0)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MAOK"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MAFail"));
		}
	}
}


// Command MMld,rd,time - move motors. 
// ld - left distance
// rd - right distance
// time - time for the move
//
// Return OK

//#define MOVE_MOTORS_DEBUG

void remoteMoveMotors()
{
#ifdef MOVE_MOTORS_DEBUG
	Serial.println(".**movemMotors");
#endif

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MMFail: no left distance"));
		}
		return ;
	}

	int leftDistance = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MMFail: no right distance"));
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MMFail: no right distance"));
		}
		return;
	}

	int rightDistance = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MMOK"));
		}
		fastMoveDistanceInMM(leftDistance, rightDistance);
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MMFail: no time"));
		}
		return;
	}

	int time = readInteger();

#ifdef MOVE_MOTORS_DEBUG
	Serial.print("    ld: ");
	Serial.print(leftDistance);
	Serial.print(" rd: ");
	Serial.print(rightDistance);
	Serial.print(" time: ");
	Serial.println(time);
#endif

	int reply = timedMoveDistanceInMM(leftDistance, rightDistance, time / 10.0);

	if (reply == 0)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MMOK"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MMFail: "));
			Serial.println(reply);
		}
	}
}

// Command MWll,rr,ss - wheel configuration
// ll - diameter of left wheel
// rr - diameter of right wheel
// ss - spacing of wheels
//
// all dimensions in mm
#// Return OK

//#define CONFIG_WHEELS_DEBUG

void remoteConfigWheels()
{
#ifdef CONFIG_WHEELS_DEBUG
	Serial.println(F(".**remoteConfigWheels"));
#endif

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MWFail: no left diameter"));
		}
		return;
	}

	int leftDiameter = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MWFail: no right diameter"));
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MWFail: no right diameter"));
		}
		return;
	}

	int rightDiameter = readInteger();

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MWFail: no wheel spacing"));
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MWFail: no wheel spacing"));
		}
		return;
	}

	int spacing = readInteger();

#ifdef CONFIG_WHEELS_DEBUG
	Serial.print("    ld: ");
	Serial.print(leftDiameter);
	Serial.print(" rd: ");
	Serial.print(rightDiameter);
	Serial.print(" separation: ");
	Serial.println(spacing);
#endif

	setActiveWheelSettings(leftDiameter, rightDiameter, spacing);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("MWOK"));
	}
}

void remoteViewWheelConfig()
{
	dumpActiveWheelSettings();
}

#ifdef COMMAND_DEBUG
#define ROTATE_DEBUG
#endif

// Command MRddd,ttt - rotate distance in time ttt (ttt is given in "ticks", where a tick is a tenth of a second
// Command MR    - rotate previous distance, or 0 if no previous rotate
// Return OK

void remoteRotateRobot()
{
	int rotateAngle;

#ifdef ROTATE_DEBUG
	Serial.println(F(".**rotateRobot"));
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MRFail: no angle"));
		}
		return;
	}

	rotateAngle = readInteger();
	
#ifdef ROTATE_DEBUG
	Serial.print(".  Rotating: ");
	Serial.println(rotateAngle);
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MROK"));
		}
		fastRotateRobot(rotateAngle);
		return;
	}

	decodePos++; // move past the separator

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MRFail: no time"));
		}
		return;
	}

	int rotateTimeInTicks = readInteger();

	int moveResult = timedRotateRobot(rotateAngle, rotateTimeInTicks / 10.0);

	if (moveResult == 0)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("MROK"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("MRFail"));
		}
	}
}

#ifdef COMMAND_DEBUG
#define CHECK_MOVING_DEBUG
#endif

// Command MC - check if the robot is still moving
// Return "moving" or "stopped"

void checkMoving()
{
#ifdef CHECK_MOVING_DEBUG
	Serial.println(".**CheckMoving: ");
#endif

	if (motorsMoving())
	{
#ifdef CHECK_MOVING_DEBUG
		Serial.println(".  moving");
#endif
		Serial.println("MCMove");
	}
	else
	{
#ifdef CHECK_MOVING_DEBUG
		Serial.println(".  stopped");
#endif
		Serial.println("MCstopped");
	}
}

#ifdef COMMAND_DEBUG
#define REMOTE_STOP_DEBUG
#endif

// Command MS - stops the robot
// Return OK
void remoteStopRobot()
{
#ifdef REMOTE_STOP_DEBUG
	Serial.println(".**remoteStopRobot: ");
#endif

	motorStop();

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("MSOK");
	}
}


void remoteMoveControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println(F("FAIL: mising move control command character"));
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(".**remoteMoveControl: ");
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(".  Move Command code : ");
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case 'A':
	case 'a':
		remoteMoveAngle();
		break;
	case 'F':
	case 'f':
		remoteMoveForwards();
		break;
	case 'R':
	case 'r':
		remoteRotateRobot();
		break;
	case 'M':
	case 'm':
		remoteMoveMotors();
		break;
	case 'C':
	case 'c':
		checkMoving();
		break;
	case 'S':
	case 's':
		remoteStopRobot();
		break;
	case 'V':
	case 'v':
		remoteViewWheelConfig();
		break;
	case 'W':
	case 'w':
		remoteConfigWheels();
		break;
	}
}

#ifdef COMMAND_DEBUG
#define PIXEL_COLOUR_DEBUG
#endif

bool readColour(byte *r, byte *g, byte*b)
{
	*r = readInteger();
#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Red: ");
	Serial.println(*r);
#endif

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising colour values in readColor"));
		}
		return false;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising colours after red in readColor"));
		}
		return false;
		}

	*g = readInteger();
#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Green: ");
	Serial.println(*g);
#endif

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising colours after green in readColor"));
		}
		return false;
	}

	*b = readInteger();
#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Blue: ");
	Serial.println(*b);
#endif

	return true;
}

// Command PCrrr,ggg,bbb - set a coloured candle with the red, green 
// and blue components as given
// Return OK

void remoteColouredCandle()
{
#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remoteColouredCandle: ");
#endif

	byte r, g, b;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("PC");
	}

	if (readColour(&r, &g, &b))
	{
		flickeringColouredLights(r, g, b, 0, 200);
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("OK"));
		}
	}
}

//#define REMOTE_PIXEL_COLOR_FADE_DEBUG

void remoteFadeToColor()
{
#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remoteFadeToColour: ");
#endif

	byte no = readInteger();
	if (no < 1)no = 1;
	if (no > 20)no = 20;

	no = 21 - no;

#ifdef REMOTE_PIXEL_COLOR_FADE_DEBUG
	Serial.print(".  Setting: ");
	Serial.println(no);
#endif

	decodePos++;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("PX");
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("Fail: mising colours after speed"));
		}
		return;
	}

	byte r, g, b;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("PX"));
	}

	if (readColour(&r, &g, &b))
	{
		transitionToColor(no,r, g, b);
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("OK"));
		}
	}
}

// PFddd - set flicker speed to value given

void remoteSetFlickerSpeed()
{
#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remoteSetFlickerSpeed: ");
#endif

	byte no = readInteger();

#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Setting: ");
	Serial.println(no);
#endif

	setFlickerUpdateSpeed(no);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("PFOK"));
	}
}

// PIppp,rrr,ggg,bbb
// Set individual pixel colour

void remoteSetIndividualPixel()
{

#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remoteSetIndividualPixel: ");
#endif

	byte no = readInteger();
#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Setting: ");
	Serial.println(no);
#endif

	decodePos++;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("PI");
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("Fail: mising colours after pixel"));
		}
		return;
	}

	byte r, g, b;

	if (readColour(&r, &g, &b))
	{
		setLightColor(r, g, b, no);
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("OK");
		}
	}
}

void remoteSetPixelsOff()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("POOK");
	}

	setAllLightsOff();
}

void remoteSetRandomColors()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("PROK");
	}

	randomiseLights();
}


void remotePixelControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println(F("FAIL: mising pixel control command character"));
		return;
	}

#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remotePixelControl: ");
#endif

	char commandCh = *decodePos;

#ifdef PIXEL_COLOUR_DEBUG
	Serial.print(".  Pixel Command code : ");
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case 'i':
	case 'I':
		remoteSetIndividualPixel();
		break;
	case 'o':
	case 'O':
		remoteSetPixelsOff();
		break;
	case 'c':
	case 'C':
		remoteColouredCandle();
		break;
	case 'f':
	case 'F':
		remoteSetFlickerSpeed();
		break;
	case 'x':
	case 'X':
		remoteFadeToColor();
		break;
	case 'r':
	case 'R':
		remoteSetRandomColors();
		break;
	}
}

// Command CDddd - delay time
// Command CD    - previous delay
// Return OK

#ifdef COMMAND_DEBUG
#define COMMAND_DELAY_DEBUG
#endif

void remoteDelay()
{
	int delayValueInTenthsIOfASecond;

#ifdef COMMAND_DELAY_DEBUG
	Serial.println(".**remoteDelay");
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("CDFail: no delay"));
		}
		return;
	}

	delayValueInTenthsIOfASecond = readInteger();

#ifdef COMMAND_DELAY_DEBUG
	Serial.print(".  Delaying: ");
	Serial.println(delayValueInTenthsIOfASecond);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("CDOK"));
	}

	delayEndTime = millis() + delayValueInTenthsIOfASecond * 100;

	programState = PROGRAM_AWAITING_DELAY_COMPLETION;
}

// Command CLxxxx - program label
// Ignored at execution, specifies the destination of a branch
// Return OK

void declareLabel()
{
#ifdef COMMAND_DELAY_DEBUG
	Serial.println(".**declareLabel");
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("CLOK");
	}
}

int findNextStatement(int programPosition)
{

	while (true)
	{
		char ch = EEPROM.read(programPosition);

		if (ch == PROGRAM_TERMINATOR | programPosition == EEPROM_SIZE)
			return -1;

		if (ch == STATEMENT_TERMINATOR)
		{
			programPosition++;
			if (programPosition == EEPROM_SIZE)
				return -1;
			else
				return programPosition;
		}
		programPosition++;
	}
}

// Find a label in the program
// Returns the offset into the program where the label is declared
// The first parameter is the first character of the label 
// (i.e. the character after the instruction code that specifies the destination)
// This might not always be the same command (it might be a branch or a subroutine call)
// The second parameter is the start position of the search in the program. 
// This is always the start of a statement, and usually the start of the program, to allow
// branches up the code. 

//#define FIND_LABEL_IN_PROGRAM_DEBUG

int findLabelInProgram(char * label, int programPosition)
{
	// Assume we are starting at the beginning of the program

	while (true)
	{
		// Spin down the statements

		int statementStart = programPosition;

		char programByte = EEPROM.read(programPosition++);

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
		Serial.print("Statement at: ");
		Serial.print(statementStart);
		Serial.print(" starting: ");
		Serial.println(programByte);
#endif
		if (programByte != 'C' & programByte != 'c')
		{

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.println("Not a statement that starts with C");
#endif

			programPosition = findNextStatement(programPosition);

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.print("Spin to statement at: ");
			Serial.println(programPosition);
#endif

			// Check to see if we have reached the end of the program in EEPROM
			if (programPosition == -1)
			{
				// Give up if the end of the code has been reached
				return -1;
			}
			else
			{
				// Check this statement
				continue;
			}
		}

		// If we get here we have found a C

		programByte = EEPROM.read(programPosition++);

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG

		Serial.print("Second statement character: ");
		Serial.println(programByte);

#endif

		// if we get here we have a control command - see if the command is a label
		if (programByte != 'L' & programByte != 'l')
		{

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.println("Not a loop command");
#endif

			programPosition = findNextStatement(programPosition);
			if (programPosition == -1)
			{
				return -1;
			}
			else
			{
				continue;
			}
		}

		//if we get here we have a CL command

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
		Serial.println("Got a CL command");
#endif

		// Set start position for label comparison
		char * labelTest = label;

		// Now spin down the label looking for a match

		while (*labelTest != STATEMENT_TERMINATOR & programPosition < EEPROM_SIZE)
		{
			programByte = EEPROM.read(programPosition);

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.print("Destination byte: ");
			Serial.print(*labelTest);
			Serial.print(" Program byte: ");
			Serial.println(programByte);
#endif

			if (*labelTest == programByte)
			{

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
				Serial.println("Got a match");
#endif
				// Move on to the next byte
				labelTest++;
				programPosition++;
			}
			else
			{
#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
				Serial.println("Fail");
#endif
				break;
			}
		}

		// get here when we reach the end of the statement or we have a mismatch

		// Get the byte at the end of the destination statement

		programByte = EEPROM.read(programPosition);

		if (*labelTest == programByte)
		{
			// If the end of the label matches the end of the statement code we have a match
			// Note that this means that if the last line of the program is a label we can only 
			// find this line if it has a statement terminator on the end. 
			// Which is fine by me. 

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.println("label match");
#endif
			return statementStart;
		}
		else
		{
			programPosition = findNextStatement(programPosition);

#ifdef FIND_LABEL_IN_PROGRAM_DEBUG
			Serial.print("Spin to statement at: ");
			Serial.println(programPosition);
#endif

			// Check to see if we have reached the end of the program in EEPROM
			if (programPosition == -1)
			{
				// Give up if the end of the code has been reached
				return -1;
			}
			else
			{
				// Check this statement
				continue;
			}
		}
	}
}

// Command CJxxxx - jump to label
// Jumps to the specified label 
// Return CJOK if the label is found, error if not. 

void jumpToLabel()
{
#ifdef JUMP_TO_LABEL_DEBUG
	Serial.println(".**jump to label");
#endif

	char * labelPos = decodePos;
	char * labelSearch = decodePos;

	int labelStatementPos = findLabelInProgram(decodePos, programBase);

#ifdef JUMP_TO_LABEL_DEBUG
	Serial.print("Label statement pos: ");
	Serial.println(labelStatementPos);
#endif

	if (labelStatementPos >= 0)
	{
		// the label has been found - jump to it
		programCounter = labelStatementPos;

#ifdef JUMP_TO_LABEL_DEBUG
		Serial.print("New Program Counter: ");
		Serial.println(programCounter);
#endif
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("CJOK");
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("CJFAIL: no dest");
		}
	}
}

//#define JUMP_TO_LABEL_COIN_DEBUG

// Command CTxxxx - jump to label on a coin toss
// Jumps to the specified label 
// Return CTOK if the label is found, error if not. 

void jumpToLabelCoinToss()
{
#ifdef JUMP_TO_LABEL_COIN_DEBUG
	Serial.println(F(".**jump to label coin toss"));send

#endif

	char * labelPos = decodePos;
	char * labelSearch = decodePos;

	int labelStatementPos = findLabelInProgram(decodePos, programBase);

#ifdef JUMP_TO_LABEL_COIN_DEBUG
	Serial.print("  Label statement pos: ");
	Serial.println(labelStatementPos);
#endif

	if (labelStatementPos >= 0)
	{
		// the label has been found - jump to it

		if (random(0, 2) == 0)
		{
			programCounter = labelStatementPos;
			if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
			{
				Serial.print(F("CTjump"));
			}
		}
		else
		{
			if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
			{
				Serial.print(F("CTcontinue"));
			}
		}

#ifdef JUMP_TO_LABEL_COIN_DEBUG
		Serial.print(F("New Program Counter: "));
		Serial.println(programCounter);
#endif
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("CTFail: no dest"));
		}
	}
}



// Command CA - pause when motors active
// Return CAOK when the pause is started

void pauseWhenMotorsActive()
{
#ifdef PAUSE_MOTORS_ACTIVE_DEBUG
	Serial.println(".**pause while the motors are active");
#endif
	programState = PROGRAM_AWAITING_MOVE_COMPLETION;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("CAOK"));
	}
}

// Command CMddd,ccc
// Jump to label if distance is less than given value

//#define COMMAND_MEASURE_DEBUG

void measureDistanceAndJump()
{

#ifdef COMMAND_MEASURE_DEBUG
	Serial.println(F(".**measure disance and jump to label"));
#endif

	int distance = readInteger();

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print(F(".  Distance: "));
	Serial.println(distance);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("CM"));
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising dest"));
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising dest"));
		}
		return;
	}

	int labelStatementPos = findLabelInProgram(decodePos, programBase);

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print("Label statement pos: ");
	Serial.println(labelStatementPos);
#endif

	if(labelStatementPos < 0)
	{
		Serial.println(F("FAIL: label not found"));
		return;
	}

	int measuredDistance = getDistanceValueInt();

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print(F("Measured Distance: "));
	Serial.println(measuredDistance);
#endif

	if (measuredDistance < distance)
	{
#ifdef COMMAND_MEASURE_DEBUG
		Serial.println(F("Distance smaller - taking jump"));
#endif
		// the label has been found - jump to it
		programCounter = labelStatementPos;

		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("jump"));
		}
	}
	else
	{
#ifdef COMMAND_MEASURE_DEBUG
		Serial.println("Distance larger - continuing");
#endif
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("continue"));
		}
	}

	// otherwise do nothing
}

// Command CIccc
// Jump to label if the motors are not running

//#define JUMP_MOTORS_INACTIVE_DEBUG

void jumpWhenMotorsInactive()
{

#ifdef JUMP_MOTORS_INACTIVE_DEBUG
	Serial.println(F(".**jump to label if motors inactive"));
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print(F("CI"));
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: mising dest"));
		}
		return;
	}

	int labelStatementPos = findLabelInProgram(decodePos, programBase);

#ifdef JUMP_MOTORS_INACTIVE_DEBUG
	Serial.print("Label statement pos: ");
	Serial.println(labelStatementPos);
#endif

	if (labelStatementPos < 0)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("FAIL: label not found"));
		}
		return;
	}

	if (!motorsMoving())
	{
#ifdef JUMP_MOTORS_INACTIVE_DEBUG
		Serial.println("Motors inactive - taking jump");
#endif
		// the label has been found - jump to it
		programCounter = labelStatementPos;
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("jump"));
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println(F("continue"));
		}
#ifdef JUMP_MOTORS_INACTIVE_DEBUG
		Serial.println(F("Motors running - continuing"));
#endif
	}

	// otherwise do nothing
}

void programControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println(F("FAIL: missing program control command character"));
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(F(".**remoteProgramControl: "));
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(F(".   Program command : "));
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case 'I':
	case 'i':
		jumpWhenMotorsInactive();
		break;
	case 'A':
	case 'a': 
		pauseWhenMotorsActive();
		break;
	case 'D':
	case 'd':
		remoteDelay();
		break;
	case 'L':
	case 'l':
		declareLabel();
		break;
	case 'J':
	case 'j':
		jumpToLabel();
		break;
	case 'M':
	case 'm':
		measureDistanceAndJump();
		break;
	case 'T':
	case 't':
		jumpToLabelCoinToss();
		break;
	}
}

//#define REMOTE_DOWNLOAD_DEBUG

// RM - start remote download

void remoteDownload()
{

#ifdef REMOTE_DOWNLOAD_DEBUG
	Serial.println(F(".**remote download"));
#endif

	if (deviceState != ACCEPTING_COMMANDS)
	{
		Serial.println(F("RMFAIL: not accepting commands"));
		return;
	}

	startDownloadingCode(STORED_PROGRAM_OFFSET);
}

void startProgramCommand()
{
	startProgramExecution(STORED_PROGRAM_OFFSET);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("RSOK"));
	}
}


void haltProgramExecutionCommand()
{
	haltProgramExecution();

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("RHOK"));
	}
}

void remoteManagement()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println(F("FAIL: missing remote control command character"));
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(F(".**remoteProgramDownload: "));
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(F(".   Download command : "));
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case 'M':
	case 'm':
		remoteDownload();
		break;
	case 'S':
	case 's':
		startProgramCommand();
		break;
	case 'H':
	case 'h':
		haltProgramExecutionCommand();
		break;
	case 'P':
	case 'p':
		pauseProgramExecution();
		break;
	case 'R':
	case 'r':
		resumeProgramExecution();
		break;
	}
}

const String version = "Version 3.0";

// IV - information display version

void displayVersion()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("IVOK"));
	}

	Serial.println(version);
}

void displayDistance()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("IDOK"));
	}
	Serial.println(getDistanceValueFloat());
	Serial.println(getDistanceValueInt());
}

void printStatus()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("ISOK"));
	}
	Serial.print(programState);
	Serial.println(diagnosticsOutputLevel);

}


// IMddd - set the debugging diagnostics level

//#define SET_MESSAGING_DEBUG 

void setMessaging()
{

#ifdef SET_MESSAGING_DEBUG
		Serial.println(F(".**informationlevelset: "));
#endif

	byte no = readInteger();

#ifdef SET_MESSAGING_DEBUG
	Serial.print(F(".  Setting: "));
	Serial.println(no);
#endif

	diagnosticsOutputLevel = no;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("IMOK"));
	}
}

void printProgram()
{
	dumpProgramFromEEPROM(STORED_PROGRAM_OFFSET);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println(F("IPOK"));
	}
}

void sendSensorReadings()
{
	char buffer[100];

	sprintf(buffer, "{\"version\":%d,\"distance\":[%d],\"lightLevel\":[%d,%d,%d]}\r",
		1, // version 1
		getDistanceValueInt(), analogRead(0), analogRead(1), analogRead(2));

	Serial.println(buffer);
}

void information()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println(F("FAIL: missing information command character"));
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(F(".**remoteProgramDownload: "));
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(F(".   Download command : "));
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case 'V':
	case 'v':
		displayVersion();
		break;
	case 'D':
	case 'd':
		displayDistance();
		break;
	case 'S':
	case 's':
		printStatus();
		break;
	case 'M':
	case 'm':
		setMessaging();
		break;
	case 'P':
	case 'p':
		printProgram();
		break;
	case 'R':
	case 'r':
		sendSensorReadings();
		break;
	}
}

void processCommand(char * commandDecodePos, char * comandDecodeLimit)
{
	decodePos = commandDecodePos;
	decodeLimit = comandDecodeLimit;

	*decodeLimit = 0;

#ifdef COMMAND_DEBUG
	Serial.print(F(".**processCommand:"));
	Serial.println((char *)decodePos);

#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(F(".  Command code : "));
	Serial.println(commandCh);
#endif

	decodePos++;

	switch (commandCh)
	{
	case '#':
		// Ignore comments
		break;
	case 'I':
	case 'i':
		information();
		break;
	case 'M':
	case 'm':
		remoteMoveControl();
		break;
	case 'P':
	case 'p':
		remotePixelControl();
		break;
	case 'C':
	case 'c':
		programControl();
		break;
	case 'R':
	case 'r':
		remoteManagement();
		break;
	default:
#ifdef COMMAND_DEBUG
		Serial.println(F(".  Invalid command : "));
#endif
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print(F("Invalid Command: "));
			Serial.print(commandCh);
			Serial.print(F(" code: "));
			Serial.println((int)commandCh);
		}
	}
}

void interpretCommandByte(byte b)
{
	if (commandPos == bufferLimit)
	{
#ifdef COMMAND_DEBUG
		Serial.println(F(".  Command buffer full - resetting"));
#endif
		resetCommand();
		return;
	}

	*commandPos = b;

	commandPos++;

	if (b == STATEMENT_TERMINATOR)
	{
#ifdef COMMAND_DEBUG
		Serial.println(F(".  Command end"));
#endif
		processCommand(programCommand, commandPos);
		resetCommand();
		return;
	}
}

void resetSerialBuffer()
{
	remotePos = remoteCommand;
	remoteLimit = remoteCommand + COMMAND_BUFFER_SIZE;
}

void interpretSerialByte(byte b)
{
	if (remotePos == remoteLimit)
	{
		resetSerialBuffer();
		return;
	}

	*remotePos = b;
	remotePos++;

	if (b == STATEMENT_TERMINATOR)
	{
#ifdef COMMAND_DEBUG
		Serial.println(F(".  Command end"));
#endif
		processCommand(remoteCommand, remotePos);
		resetSerialBuffer();
		return;
	}
}

void processSerialByte(byte b)
{
#ifdef COMMAND_DEBUG
	Serial.print(F(".**processSerialByte: "));
	Serial.println((char)b);
#endif

	switch (deviceState)
	{
	case ACCEPTING_COMMANDS:
		interpretSerialByte(b);
		break;
	case DOWNLOADING_CODE:
		storeReceivedByte(b);
		break;
	}
}


void setupRemoteControl()
{
#ifdef COMMAND_DEBUG
	Serial.println(F(".**setupRemoteControl"));
#endif
	resetCommand();
	resetSerialBuffer();
}

// Executes the statement in the EEPROM at the current program counter
// The statement is assembled into a buffer by interpretCommandByte

bool exeuteProgramStatement()
{
	char programByte;

#ifdef PROGRAM_DEBUG
	Serial.println(F(".Executing statement"));
#endif

	if (diagnosticsOutputLevel & LINE_NUMBERS)
	{
		Serial.print(F("Offset: "));
		Serial.println((int)programCounter);
	}

	while (true)
	{
		programByte = EEPROM.read(programCounter++);

		if (programCounter >= EEPROM_SIZE || programByte == PROGRAM_TERMINATOR)
		{
			haltProgramExecution();
			return false;
		}

#ifdef PROGRAM_DEBUG
		Serial.print(F(".    program byte: "));
		Serial.println(programByte);
#endif

		interpretCommandByte(programByte);

		if (programByte == STATEMENT_TERMINATOR)
			return true;

	}
}

//const char SAMPLE_CODE[] PROGMEM = { "PC255,0,0\rCD5\rCLtop\rPC0,0,255\rCD5\rPC0,255,255\rCD5\rPC255,0,255\rCD5\rCJtop\r" };
const char SAMPLE_CODE[] PROGMEM = { "CLtop\rCM10,close\rPC255,0,0\rCJtop\rCLclose\rPC0,0,255\rCJtop\r" };

void loadTestProgram(int offset)
{
	int inPos = 0;
	int outPos = offset;

	int len = strlen_P(SAMPLE_CODE);
	int i;
	char myChar;

	for (i = 0; i < len; i++)
	{
		myChar = pgm_read_byte_near(SAMPLE_CODE + i);
		EEPROM.write(outPos++, myChar);
	}

	EEPROM.write(outPos, 0);

	dumpProgramFromEEPROM(offset);
}

void updateProgramExcecution()
{

	// If we recieve serial data the program that is running
	// must stop. This is to allow 
	while (CharsAvailable())
	{
		byte b = GetRawCh();
		processSerialByte(b);
	}

	switch (programState)
	{
	case PROGRAM_STOPPED:
	case PROGRAM_PAUSED:
		break;
	case PROGRAM_ACTIVE:
		exeuteProgramStatement();
		break;
	case PROGRAM_AWAITING_MOVE_COMPLETION:
		if (!motorsMoving())
			programState = PROGRAM_ACTIVE;
		break;
	case PROGRAM_AWAITING_DELAY_COMPLETION:
		if (millis() > delayEndTime)
		{
			programState = PROGRAM_ACTIVE;
		}
		break;
	}
}

bool commandsNeedFullSpeed()
{
	return deviceState != ACCEPTING_COMMANDS;
}
