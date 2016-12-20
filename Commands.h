#define COMMAND_BUFFER_SIZE 25

// Set command terminator to CR

#define COMMAND_TERMINATOR 0x0D

char remoteCommand[COMMAND_BUFFER_SIZE];
char * commandPos;
char * commandLimit;
char * bufferLimit;
char * decodePos;
char * decodeLimit;

int remoteLeftSpeed = 10;
int remoteRightSpeed = 10;
int remoteRotateSpeed = 10;

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

  if (*decodePos == COMMAND_TERMINATOR)
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

  if (*decodePos == COMMAND_TERMINATOR)
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

  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
  {
    Serial.println("FAIL: mising colour values in readColor");
    return false;
  }

  decodePos++;

  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
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

  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
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
// Return OK


void remoteColouredCandle()
{
#ifdef PIXEL_COLOUR_DEBUG
  Serial.println(".**remoteColouredCandle: ");
#endif

  byte r, g, b;

  if (readColour(&r, &g, &b))
  {
    flickeringColouredLights(r, g, b, 0, 200);
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

void remoteSetIndividualPixel ()
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

  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
  {
    Serial.println("FAIL: mising colours after pos in remoteSetPixelColour");
    return;
  }

  byte r, g, b;

  if(readColour(&r,&g,&b))
    setLightColor(r, g, b, no);
}

void remotePixelControl()
{
  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
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
  if (*decodePos == COMMAND_TERMINATOR | decodePos == decodeLimit)
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
  default:
#ifdef COMMAND_DEBUG
    Serial.println(".  Invalid command : ");
#endif
    Serial.print("Invalid Command: ");
    Serial.println(commandCh);
  }
  resetCommand();
}

void processCommandByte(byte b)
{
#ifdef COMMAND_DEBUG
  Serial.print(".**processCommandByte: ");
  Serial.println((char)b);
#endif

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

  if (b == COMMAND_TERMINATOR)
  {
#ifdef COMMAND_DEBUG
    Serial.println(".  Command end");
#endif
    processCommand(remoteCommand, commandPos);
    return;
  }
}

void setupRemoteControl()
{
#ifdef COMMAND_DEBUG
  Serial.println(".**setupRemoteControl");
#endif
  resetCommand();
}

