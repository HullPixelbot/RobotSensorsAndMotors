
// Stored program management

#define EEPROM_SIZE 1000
#define STORED_PROGRAM_SIZE 500
#define STORED_PROGRAM_OFFSET 20

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

enum ProgramState
{
	PROGRAM_STOPPED,
	PROGRAM_ACTIVE,
	PROGRAM_AWAITING_MOVE_COMPLETION,
	PROGRAM_AWAITING_DELAY_COMPLETION,
};

enum DeviceState
{
	ACCEPTING_COMMANDS,
	DOWNLOADING_CODE,
	DOWNLOADING_CHECKSUM
};

ProgramState programState = PROGRAM_STOPPED;
DeviceState deviceState = ACCEPTING_COMMANDS;

long delayEndTime;

#define COMMAND_BUFFER_SIZE 25

// Set command terminator to CR

#define STATEMENT_TERMINATOR 0x0D

// Set program terminator to string end
// This is the EOT character
#define PROGRAM_TERMINATOR 0x04

char remoteCommand[COMMAND_BUFFER_SIZE];
char * commandPos;
char * commandLimit;
char * bufferLimit;
char * decodePos;
char * decodeLimit;

int remoteLeftSpeed = 10;
int remoteRightSpeed = 10;
int remoteRotateSpeed = 10;

// Starts a program running at the given position

void startProgramExecution(int programPosition)
{

#ifdef PROGRAM_DEBUG
	Serial.print(".Starting program execution at: ");
	Serial.println(programPosition);
#endif

	programCounter = programPosition;
	programBase = programPosition;
	programState = PROGRAM_ACTIVE;
}

void stopProgramExecution()
{
#ifdef PROGRAM_DEBUG
	Serial.print(".Ending program execution at: ");
	Serial.println(programCounter);
#endif

	programState = PROGRAM_STOPPED;
}

// Stores a program byte into the eeprom at the stated location
// The pos value is the offset in the EEProm into which the program is to be written
// The function returns true if the byte was stored, false if not

bool storeByteIntoEEPROM(char byte, int pos)
{
	if (pos > EEPROM_SIZE)
		return false;
	EEPROM.write(pos, byte);
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


// Called to start the download of program code
// each byte that arrives down the serial port is now stored in program memory
//
void startDownloadingCode(int downloadPosition)
{
#ifdef PROGRAM_DEBUG
	Serial.println(".Starting code download");
#endif

	// Stop the current program
	stopProgramExecution();

	deviceState = DOWNLOADING_CODE;

	programWriteBase = downloadPosition;

	downloadChecksum = 0;

	startBusyPixel();

}

// Called when a byte is received from the host when in program storage mode
// Adds it to the stored program, updates the stored position and the counter
// If the byte is the terminator byte (zero) it changes to the "wait for checksum" state
// for the program checksum

void storeReceivedByte(byte b)
{
	// Store the byte - overlong programs will fail silently at the moment.....

	if (storeByteIntoEEPROM(b, programWriteBase++))
	{
		// if the store worked - update the busy pixel
		if (b == STATEMENT_TERMINATOR)
			updateBusyPixel();
	}

	Serial.println(b);

	// Add the value onto the checksum
	downloadChecksum += b;
	
	// Have we reached the end of the program?
	if (b == PROGRAM_TERMINATOR)
	{
		Serial.println("Got the program terminator");
		// end of the code - wait for the checksum
		deviceState = DOWNLOADING_CHECKSUM;
	}
}

// called when a checksum is received for a downloaded program
// Starts the program running it the checksum is valid
void processReceivedChecksum(byte b)
{
	b = downloadChecksum;
	if (b == downloadChecksum)
	{
		// Yay = got a checksum match!
		Serial.println("OK");
		dumpProgramFromEEPROM(STORED_PROGRAM_OFFSET);
		startProgramExecution(STORED_PROGRAM_OFFSET);
		deviceState = ACCEPTING_COMMANDS;
	}
	else
	{
		Serial.println("Bad checksum");
		Serial.print("Calculated: ");
		Serial.print((byte)downloadChecksum);
		Serial.print("Received: ");
		Serial.println((byte)b);
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
	commandPos = remoteCommand;
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

	Serial.println("OK");

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

	Serial.println("OK");

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
		Serial.println("moving");
	}
	else
	{
#ifdef CHECK_MOVING_DEBUG
		Serial.println(".  stopped");
#endif
		Serial.println("stopped");
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
	Serial.println("OK");
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
		Serial.println("FAIL: mising colour values in readColor");
		return false;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: mising colours after red in readColor");
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
		Serial.println("FAIL: mising colours after green in readColor");
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
// If the colour being set is the same as the existing colour the 
// command has no effect
// Return OK

byte oldr=0 , oldg=0, oldb=0;

void remoteColouredCandle()
{
#ifdef PIXEL_COLOUR_DEBUG
	Serial.println(".**remoteColouredCandle: ");
#endif

	byte r, g, b;

	if (readColour(&r, &g, &b))
	{
		if (r != oldr | g != oldg | b != oldb)
		{
			flickeringColouredLights(r, g, b, 0, 200);
			oldr = r;
			oldb = b;
			oldg = g;
		}
		Serial.println("OK");
	}

}

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
}

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

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: mising colours after pos in remoteSetPixelColour");
		return;
	}

	byte r, g, b;

	if (readColour(&r, &g, &b))
		setLightColor(r, g, b, no);
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
		setAllLightsOff();
		break;
	case 'c':
	case 'C':
		remoteColouredCandle();
		break;
	case 'f':
	case 'F':
		remoteSetFlickerSpeed();
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

	Serial.println("OK");

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

	Serial.println("OK");
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
// Return OK if the label is found, error if not. 

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
		Serial.println("OK");
	}
	else
	{
		Serial.println("FAIL: destination of jump not found");
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

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: mising separator in measure distance test");
		return;
	}

	decodePos++;

	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: mising destination in measure distance test");
		return;
	}

	int labelStatementPos = findLabelInProgram(decodePos, programBase);

#ifdef COMMAND_MEASURE_DEBUG
	Serial.print("Label statement pos: ");
	Serial.println(labelStatementPos);
#endif

	if(labelStatementPos < 0)
	{
		Serial.println("FAIL: label not found in measure distance test");
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
	}
	else
	{
#ifdef COMMAND_MEASURE_DEBUG
		Serial.println("Distance larger - continuing");
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

#define REMOTE_DOWNLOAD_DEBUG

void remoteDownload()
{

#ifdef REMOTE_DOWNLOAD_DEBUG
	Serial.println(".**remote download");
#endif

	startDownloadingCode(STORED_PROGRAM_OFFSET);
}

void remoteDownloadControl()
{
	if (*decodePos == STATEMENT_TERMINATOR | decodePos == decodeLimit)
	{
		Serial.println("FAIL: missing remove download control command character");
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
		remoteDownloadControl();
		break;
	default:
#ifdef COMMAND_DEBUG
		Serial.println(".  Invalid command : ");
#endif
		Serial.print("Invalid Command: ");
		Serial.println(commandCh);
	}
	resetCommand();
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
		processCommand(remoteCommand, commandPos);
		return;
	}
}

void processCommandByte(byte b)
{ 
#ifdef COMMAND_DEBUG
	Serial.print(".**processCommandByte: ");
	Serial.println((char)b);
#endif

	switch (deviceState)
	{
	case ACCEPTING_COMMANDS:
		interpretCommandByte(b);
		break;
	case DOWNLOADING_CODE:
		storeReceivedByte(b);
		break;
	case DOWNLOADING_CHECKSUM:
		processReceivedChecksum(b);
		break;
	}
}

void setupRemoteControl()
{
#ifdef COMMAND_DEBUG
	Serial.println(".**setupRemoteControl");
#endif
	resetCommand();
}


// Executes the statement at the current program counter

bool exeuteProgramStatement()
{
	char programByte;

#ifdef PROGRAM_DEBUG
	Serial.println(".Executing statement");
#endif

	while (true)
	{
		programByte = EEPROM.read(programCounter++);

		if (programCounter >= EEPROM_SIZE || programByte == PROGRAM_TERMINATOR)
		{
			stopProgramExecution();
			return false;
		}

#ifdef PROGRAM_DEBUG
		Serial.print(".    program byte: ");
		Serial.println(programByte);
#endif

		processCommandByte(programByte);

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
	switch (programState)
	{
	case PROGRAM_STOPPED:
		break;
	case PROGRAM_ACTIVE:
		exeuteProgramStatement();
		break;
	case PROGRAM_AWAITING_MOVE_COMPLETION:
		break;
	case PROGRAM_AWAITING_DELAY_COMPLETION:
		if (millis() > delayEndTime)
		{
			programState = PROGRAM_ACTIVE;
		}
		break;
	}
}


