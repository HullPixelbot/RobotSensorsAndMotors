// Performs the variable management 
// Variables can be given names, stored and evaluated
// Simple two operand expressions only

#define NUMBER_OF_VARIABLES 10
#define MAX_VARIABLE_NAME_LENGTH 5

struct variable
{
	bool empty;
	// add one to the end for the terminating zero
	char name[MAX_VARIABLE_NAME_LENGTH+1];
	int value;
};

variable variables [NUMBER_OF_VARIABLES];

#define VAR_DEBUG

void clearVariableSlot(int position)
{
	variables[position].empty = true;
	variables[position].value = 0;
	variables[position].name[0] = 0;
}

void clearVariables()
{
	// If the initial value of the variable name is zero, the store is empty

	for (int i = 0; i < NUMBER_OF_VARIABLES; i++)
	{
		clearVariableSlot(i);
	}
}

#define INVALID_VARIABLE_NAME -1
#define NO_ROOM_FOR_VARIABLE -2
#define VARIABLE_NOT_FOUND -3
#define VARIABLE_NAME_TOO_LONG -4

inline bool isVariableNameStart(char * ch)
{
	return (isalpha(*ch));
}

inline bool isVariableNameChar(char * ch)
{
	return (isAlphaNumeric(*ch));
}

inline bool variableSlotEmpty(int position)
{
	return variables[position].empty;
}

bool matchVariable(int position, char * text)
{
#ifdef VAR_DEBUG
	Serial.print(F("Match variable: "));
	Serial.println(position);
#endif

	if (variableSlotEmpty(position))
	{
		// position is empty - not a match
#ifdef VAR_DEBUG
		Serial.print(F("    Empty slot"));
		Serial.println(position);
#endif

		return false;
	}

#ifdef VAR_DEBUG
	Serial.print(F("    Matching: "));
	Serial.println(position);
#endif

	for (int i = 0; i < MAX_VARIABLE_NAME_LENGTH; i++)
	{
#ifdef VAR_DEBUG
		Serial.print(variables[position].name[i]);
		Serial.print(F(":"));
		Serial.print(*text);
		Serial.print(F("  "));
#endif
		if ((variables[position].name[i] == 0) & !isVariableNameChar(text))
		{
			// variable table has ended at the same time as the variable
			// we have a match
			return true;
		}

		// See if we have failed to match
		if (variables[position].name[i] != *text)
		{
			return false;
		}

		// Move on to next character

		text++;
	}
}

// returns the length of the variable name at the given position in the variable store
// used for calculating pointer updates

int getVariableNameLength(int position)
{
	if (variables[position].empty)
		return 0;

	return strlen(variables[position].name);
}

// A variable name must start with a letter and then contain letters and digits only
// This method searches the variable store for a variable of the given name and then 
// returns the variable store offset for that variable, or -1 if the variable name is invalid
// The parameter points to the area of memory holding the variable name. The variable name is judged to 
// have ended when a non-text/digit character is found
//

int findVariable(char * name)
{
#ifdef VAR_DEBUG
	Serial.println(F("Finding variable"));
#endif

	if (!isVariableNameStart(name))
	{
#ifdef VAR_DEBUG
		Serial.println(F("    Invalid variable name"));
#endif
		return INVALID_VARIABLE_NAME;
	}

	for (int i = 0; i < NUMBER_OF_VARIABLES; i++)
	{
#ifdef VAR_DEBUG
		Serial.print(F("    Checking variable: "));
		Serial.println(F("i"));
#endif
		if (matchVariable(i, name))
		{
			return i;
		}
	}
	return VARIABLE_NOT_FOUND;
}

// finds an empty location in the variable table and returns the offset into that table
// returns NO_ROOM_FOR_VARIABLE if the table is full

int findVariableSlot()
{
	for (int i = 0; i < NUMBER_OF_VARIABLES; i++)
	{
		if (variableSlotEmpty(i))
			return i;
	}

	return NO_ROOM_FOR_VARIABLE;
}

// returns INVALID_VARIABLE_NAME if the name is invalid 
// returns NO_ROOM_FOR_VARIABLE if the variable cannot be stored
// returns VARIABLE_NAME_TOO_LONG if the name of the variable is longer than the store length

int createVariable()
{

#ifdef VAR_DEBUG
	Serial.println(F("Creating variable"));
#endif

	int position = findVariableSlot();

	if (position == NO_ROOM_FOR_VARIABLE)
	{

#ifdef VAR_DEBUG
		Serial.println(F("   no room for variable"));
#endif

		return NO_ROOM_FOR_VARIABLE;
	}

	// Need a valid variable name start - must be a letter
	if (!isVariableNameStart(decodePos))
	{
#ifdef VAR_DEBUG
		Serial.println(F("   invalid variable name"));
#endif
		return INVALID_VARIABLE_NAME;
	}

	int i;

	for (i = 0; i < MAX_VARIABLE_NAME_LENGTH; i++)
	{
		// store the variable name
		variables[position].name[i] = *decodePos;

		decodePos++;

#ifdef VAR_DEBUG
		Serial.print(variables[position].name[i]);
		Serial.print(F(":"));
		Serial.print(*decodePos);
		Serial.print(F("  "));
#endif

		if (!isVariableNameChar(decodePos))
		{
			// If we are 
			if(i<(MAX_VARIABLE_NAME_LENGTH-1))
			// end of variable name
			// end the name string
			// Note that we declared this one element larger to make room 
			// for the zero
			variables[position].name[i+1] = 0;
			break;
		}
	}

	if (i == MAX_VARIABLE_NAME_LENGTH)
	{
		// Reached the end of the store without reaching the end of the variable
		clearVariableSlot(position);
		return VARIABLE_NAME_TOO_LONG;
	}

	variables[position].empty = false;

	return position;
}

// called from the command processor
// the global variable decodePos holds the position in the decode array (first character
// of the variable name) and the global variable decodeLimit the end of the array

void setVariable() 
{

#ifdef VAR_DEBUG
	Serial.println(F("setting variable"));

#endif

	char * variableStart = decodePos;

	// First see if we can find the variable in the store

	int position = findVariable(decodePos);

	if (position == INVALID_VARIABLE_NAME)
	{
		Serial.println(F("Invalid variable name"));
		return;
	}

	if (position == VARIABLE_NOT_FOUND)
	{
#ifdef VAR_DEBUG
		Serial.println(F("Variable not found"));
#endif
		createVariable();
	}
	else
	{
#ifdef VAR_DEBUG
		Serial.println(F("Found variable"));
#endif
		// we have the variable
		// move down to the end of the name
		decodePos = decodePos + getVariableNameLength(position);
		Serial.println(variables[position].name);
	}
}
