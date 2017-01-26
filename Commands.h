
// Stored program management

#define EEPROM_SIZE 1000
#define STORED_PROGRAM_OFFSET 20

#define PROGRAM_STATUS_BYTE_OFFSET 0
#define PROGRAM_STORED_VALUE1 0xaa
#define PROGRAM_STORED_VALUE2 0x55

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

#define COMMAND_BUFFER_SIZE 20

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

int remoteLeftSpeed = 10;
int remoteRightSpeed = 10;
int remoteRotateSpeed = 10;

// Stores a program byte into the eeprom at the stated location
// The pos value is the offset in the EEProm into which the program is to be written
// The function returns true if the byte was stored, false if not

bool storeByteIntoEEPROM(char byte, int pos)
{
	if (pos > EEPROM_SIZE)
		return false;
	EEPROM.update(pos, byte);
	return true;
}

// Stores a program into the eeprom at the stated location
// The program is a string of text which is zero terminated
// The EEPromStart value is the offset in the EEProm into which the program is to be written
// The function returns true if the program was loaded, false if not

bool storeProgramIntoEEPROM(char * programStart, int EEPromStart)
{
	while (*programStart)
	{
		if (!storeByteIntoEEPROM(*programStart, EEPromStart))
			return false;
		programStart++;
		EEPromStart++;
	}

	// put the terminator on the end of the program
	storeByteIntoEEPROM(*programStart, EEPromStart);
	return true;
}

// Dumps the program as stored in the EEPROM

void dumpProgramFromEEPROM(int EEPromStart)
{
	Serial.println("Current program: ");

	char byte;
	while (byte = EEPROM.read(EEPromStart++))
	{
		if (byte == STATEMENT_TERMINATOR)
			Serial.println();
		else
			Serial.print(byte);

		if (byte == PROGRAM_TERMINATOR)
		{
			Serial.println("Reached end of program");
			break;
		}

		if (EEPromStart >= EEPROM_SIZE)
		{
			Serial.println("Reached end of eeprom");
			break;
		}
	}
}

void setProgramStored()
{
	storeByteIntoEEPROM(PROGRAM_STORED_VALUE1, PROGRAM_STATUS_BYTE_OFFSET);
	storeByteIntoEEPROM(PROGRAM_STORED_VALUE2, PROGRAM_STATUS_BYTE_OFFSET + 1);
}

void clearProgramStored()
{
	storeByteIntoEEPROM(0, PROGRAM_STATUS_BYTE_OFFSET);
	storeByteIntoEEPROM(0, PROGRAM_STATUS_BYTE_OFFSET + 1);
}

bool isProgramStored()
{
	if ((EEPROM.read(PROGRAM_STATUS_BYTE_OFFSET) == PROGRAM_STORED_VALUE1) &
		(EEPROM.read(PROGRAM_STATUS_BYTE_OFFSET + 1) == PROGRAM_STORED_VALUE2))
		return true;
	else
		return false;
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

// Starts a program running at the given position

void startProgramExecution(int programPosition)
{
	if (isProgramStored())
	{

#ifdef PROGRAM_DEBUG
		Serial.print(".Starting program execution at: ");
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
	Serial.print(".Ending program execution at: ");
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
		Serial.println("RPOK");
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
			Serial.println("RROK");
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print("RRFail:");
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

	startBusyPixel();

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("RMOK");
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
			Serial.println("RXOK");
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

int previousForwardDistanceMove = 0;

// Command MFddd - move distance
// Command MF    - move previous distance, or 0 if no previous move
// Return OK

void remoteMoveForwards()
{
	int forwardMoveDistance;

#ifdef MOVE_FORWARDS_DEBUG
	Serial.println(".**moveForwards");
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
#ifdef MOVE_FORWARDS_DEBUG
		Serial.println(".  Using previous move value");
#endif
		forwardMoveDistance = previousForwardDistanceMove;
	}
	else
	{
		forwardMoveDistance = readInteger();
	}

	previousForwardDistanceMove = forwardMoveDistance;

#ifdef MOVE_FORWARDS_DEBUG
	Serial.print(".  Moving: ");
	Serial.println(forwardMoveDistance);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("MFOK");
	}

	moveRobot(forwardMoveDistance, forwardMoveDistance, remoteLeftSpeed, remoteRightSpeed);
}


#ifdef COMMAND_DEBUG
#define ROTATE_DEBUG
#endif

int previousRotateAngle = 0;

// Command MRddd - rotate distance
// Command MR    - rotate previous distance, or 0 if no previous rotate
// Return OK

void remoteRotateRobot()
{
	int rotateAngle;

#ifdef ROTATE_DEBUG
	Serial.println(".**rotateRobot");
#endif

	if (*decodePos == STATEMENT_TERMINATOR)
	{
#ifdef ROTATE_DEBUG
		Serial.println(".  Using previous rotate angle");
#endif
		rotateAngle = previousRotateAngle;
	}
	else
	{
		rotateAngle = readInteger();
	}

	previousRotateAngle = rotateAngle;

#ifdef MOVE_FORWARDS_DEBUG
	Serial.print(".  Rotating: ");
	Serial.println(rotateAngle);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("MROK");
	}

	rotateRobot(rotateAngle, remoteRotateSpeed);

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
			Serial.println("FAIL: mising colour values in readColor");
		}
		return false;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("FAIL: mising colours after red in readColor");
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
			Serial.println("FAIL: mising colours after green in readColor");
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
			Serial.println("OK");
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
			Serial.println("Fail: mising colours after speed");
		}
		return;
	}

	byte r, g, b;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("PX");
	}

	if (readColour(&r, &g, &b))
	{
		transitionToColor(no,r, g, b);
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("OK");
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
		Serial.println("PFOK");
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
			Serial.println("Fail: mising colours after pixel");
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
		Serial.println("FAIL: mising pixel control command character");
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

void remoteMoveControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: mising move control command character");
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
	case 'F':
	case 'f':
		remoteMoveForwards();
		break;
	case 'R':
	case 'r':
		remoteRotateRobot();
		break;
	case 'C':
	case 'c':
		checkMoving();
		break;
	case 'S':
	case 's':
		remoteStopRobot();
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
#ifdef COMMAND_DELAY_DEBUG
		Serial.println(".  Using previous delay value");
#endif
		delayValueInTenthsIOfASecond = previousRotateAngle;
	}
	else
	{
		delayValueInTenthsIOfASecond = readInteger();
	}

	previousRotateAngle = delayValueInTenthsIOfASecond;

#ifdef COMMAND_DELAY_DEBUG
	Serial.print(".  Delaying: ");
	Serial.println(delayValueInTenthsIOfASecond);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("CDOK");
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
		Serial.println("CAOK");
	}
}

// Command CMddd,ccc
// Jump to label if distance is less than given value

//#define COMMAND_MEASURE_DEBUG

void measureDistanceAndJump()
{

#ifdef COMMAND_MEASURE_DEBUG
	Serial.println(".**measure disance and jump to label");
#endif

	int distance = readInteger();

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print(".  Distance: ");
	Serial.println(distance);
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("CM");
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("FAIL: mising dest");
		}
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("FAIL: mising dest");
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
		Serial.println("FAIL: label not found");
		return;
	}

	int measuredDistance = getDistanceValue();

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print("Measured Distance: ");
	Serial.println(measuredDistance);
#endif

	if (measuredDistance < distance)
	{
#ifdef COMMAND_MEASURE_DEBUG
		Serial.println("Distance smaller - taking jump");
#endif
		// the label has been found - jump to it
		programCounter = labelStatementPos;

		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("jump");
		}
	}
	else
	{
#ifdef COMMAND_MEASURE_DEBUG
		Serial.println("Distance larger - continuing");
#endif
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("continue");
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
	Serial.println(".**jump to label if motors inactive");
#endif

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.print("CI");
	}

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("FAIL: mising dest");
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
			Serial.println("FAIL: label not found");
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
			Serial.println("jump");
		}
	}
	else
	{
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.println("continue");
		}
#ifdef JUMP_MOTORS_INACTIVE_DEBUG
		Serial.println("Motors running - continuing");
#endif
	}

	// otherwise do nothing
}

void programControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: missing program control command character");
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(".**remoteProgramControl: ");
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(".   Program command : ");
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
	}
}

//#define REMOTE_DOWNLOAD_DEBUG

// RM - start remote download

void remoteDownload()
{

#ifdef REMOTE_DOWNLOAD_DEBUG
	Serial.println(".**remote download");
#endif

	if (deviceState != ACCEPTING_COMMANDS)
	{
		Serial.println("RMFAIL: not accepting commands");
		return;
	}

	startDownloadingCode(STORED_PROGRAM_OFFSET);
}

void startProgramCommand()
{
	startProgramExecution(STORED_PROGRAM_OFFSET);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("RSOK");
	}
}


void haltProgramExecutionCommand()
{
	haltProgramExecution();

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("RHOK");
	}
}

void remoteManagement()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: missing remote control command character");
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(".**remoteProgramDownload: ");
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(".   Download command : ");
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

const String version = "Version 1.0";

// IV - information display version

void displayVersion()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("IVOK");
	}

	Serial.println(version);
}

void displayDistance()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("IDOK");
	}
	Serial.println(getDistanceValue());
}

void printStatus()
{
	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("ISOK");
	}
	Serial.print(programState);
	Serial.println(diagnosticsOutputLevel);

}


// IMddd - set the debugging diagnostics level

//#define SET_MESSAGING_DEBUG 

void setMessaging()
{

#ifdef SET_MESSAGING_DEBUG
		Serial.println(".**informationlevelset: ");
#endif

	byte no = readInteger();

#ifdef SET_MESSAGING_DEBUG
	Serial.print(".  Setting: ");
	Serial.println(no);
#endif

	diagnosticsOutputLevel = no;

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("IMOK");
	}
}

void printProgram()
{
	dumpProgramFromEEPROM(STORED_PROGRAM_OFFSET);

	if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
	{
		Serial.println("IPOK");
	}
}

void information()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: missing information command character");
		return;
	}

#ifdef COMMAND_DEBUG
	Serial.println(".**remoteProgramDownload: ");
#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(".   Download command : ");
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
	}
}

void processCommand(char * commandDecodePos, char * comandDecodeLimit)
{
	decodePos = commandDecodePos;
	decodeLimit = comandDecodeLimit;

	*decodeLimit = 0;

#ifdef COMMAND_DEBUG
	Serial.print(".**processCommand:");
	Serial.println((char *)decodePos);

#endif

	char commandCh = *decodePos;

#ifdef COMMAND_DEBUG
	Serial.print(".  Command code : ");
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
		Serial.println(".  Invalid command : ");
#endif
		if (diagnosticsOutputLevel & STATEMENT_CONFIRMATION)
		{
			Serial.print("Invalid Command: ");
			Serial.print(commandCh);
			Serial.print(" code: ");
			Serial.println((int)commandCh);
		}
	}
}

void interpretCommandByte(byte b)
{
	if (commandPos == bufferLimit)
	{
#ifdef COMMAND_DEBUG
		Serial.println(".  Command buffer full - resetting");
#endif
		resetCommand();
		return;
	}

	*commandPos = b;

	commandPos++;

	if (b == STATEMENT_TERMINATOR)
	{
#ifdef COMMAND_DEBUG
		Serial.println(".  Command end");
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
		Serial.println(".  Command end");
#endif
		processCommand(remoteCommand, remotePos);
		resetSerialBuffer();
		return;
	}
}

void processSerialByte(byte b)
{
#ifdef COMMAND_DEBUG
	Serial.print(".**processSerialByte: ");
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
	Serial.println(".**setupRemoteControl");
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
	Serial.println(".Executing statement");
#endif

	if (diagnosticsOutputLevel & LINE_NUMBERS)
	{
		Serial.print("Offset: ");
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
		Serial.print(".    program byte: ");
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
