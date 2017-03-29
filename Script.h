const char FlashString[] PROGMEM = "This is a string held completely in flash memory.";

#define SCRIPT_OUTPUT_BUFFER_LENGTH 50

char scriptOutputBuffer[SCRIPT_OUTPUT_BUFFER_LENGTH];

enum scriptDecodeResult
{
	SCRIPT_OK,
	SCRIPT_ERROR
};

#define SCRIPT_DEBUG

scriptDecodeResult decodeScript(char * input, char * output)
{
	scriptDecodeResult result = SCRIPT_OK;

#ifdef SCRIPT_DEBUG

	Serial.print("Compiling: ");
	Serial.println(input);

#endif // SCRIPT_DEBUG


	return result;
}

void testScript()
{
	decodeScript("FORWARDS 50", scriptOutputBuffer);
}