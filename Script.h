


// Commands are separated by a # character. Always lower case
// stuff.

// command numbers:                      0    1    2    3     4     5
const char commandNames[] PROGMEM = "forward#back#left#right#angry#happy";

#define SCRIPT_OUTPUT_BUFFER_LENGTH 50

char scriptOutputBuffer[SCRIPT_OUTPUT_BUFFER_LENGTH];

enum scriptDecodeResult
{
	SCRIPT_OK,
	SCRIPT_ERROR
};

#define SCRIPT_DEBUG


// finds the offset of the next keyword - returns -1 if the keyword was not found

int spinToCommandEnd(int position)
{
	while (true)
	{
		char ch = pgm_read_byte_near(commandNames+position);
		if (ch == 0)
			return -1;

		if (ch == '#')
			return position +1;

		position++;
	}
}

enum ScriptCompareCommandResult
{
	END_OF_COMMANDS,
	COMMAND_MATCHED,
	COMMAND_NOT_MATCHED
};

ScriptCompareCommandResult compareCommand(char * input, int position)
{
#ifdef SCRIPT_DEBUG

	Serial.print("Comparing: ");
	Serial.println(input);
	Serial.print("test bytes: ");

#endif // SCRIPT_DEBUG

	while (true)
	{
		char ch = pgm_read_byte_near(commandNames+position);

#ifdef SCRIPT_DEBUG

		Serial.print(ch);

#endif // SCRIPT_DEBUG

		if (ch == '#')
			return COMMAND_MATCHED;

		if (ch == 0)
			return END_OF_COMMANDS;

		char inputCh = toLowerCase(*input);

		if (ch != inputCh)
			return COMMAND_NOT_MATCHED;

		position++;
		input++;
	}
}

int decodeCommandName(char * input)
{
	int commandPos = 0;

	int commandNumber = 0;

	while (true)
	{
		ScriptCompareCommandResult result = compareCommand(input, commandPos);

		switch (result)
		{
		case COMMAND_MATCHED:
			return commandNumber;

		case COMMAND_NOT_MATCHED:
			commandPos = spinToCommandEnd(commandPos);
			commandNumber++;
			if (commandPos == -1)
				return -1;
			break;

		case END_OF_COMMANDS:
			return -1;
			break;
		}

	}
}

void dumpByte(byte b)
{
	if (b == 13)
	{
		Serial.println();
	}
	else
	{
		Serial.print((char)b);
	}
}


// Decodes 
scriptDecodeResult decodeScriptLine(char * input, void (*outputFunction) (byte))
{
	scriptDecodeResult result = SCRIPT_OK;

	int commandNo = 

#ifdef SCRIPT_DEBUG

	Serial.print("Compiling: ");
	Serial.println(input);

#endif // SCRIPT_DEBUG

	return result;
}

void testScript()
{
#ifdef SCRIPT_DEBUG

	Serial.print("Script test");

#endif // SCRIPT_DEBUG

	Serial.println(decodeCommandName("forward"));
	Serial.println(decodeCommandName("back"));
	Serial.println(decodeCommandName("wallaby"));
	decodeScriptLine("FORWARDS 50", dumpByte);
}