///////////////////////////////////////////////////////////
/// Motor control
///////////////////////////////////////////////////////////

char lmotorPos = 0;
char lmotorDelta = 1;

char rmotorPos = 0;
char rmotorDelta = 1;

static const long motorStepTime = 120;

const static float leftWheelRadius = 72.0 / 2.0;
float leftWheelCircumference;
const static float rightWheelRadius = 72.0 / 2.0;
float rightWheelCircumference;
const static float wheelSpacing = 105.0;
float turningCircle;
double ticksPerSecond;

const int countsperrev = 512 * 8; // number of microsteps per full revolution

volatile long leftStepCounter = 0;
volatile long leftStepLimit = 1000;

volatile long rightStepCounter = 0;
volatile long rightStepLimit = 1000;

volatile bool leftMoving = false;
volatile bool rightMoving = false;

volatile bool leftCountingSteps = false;
volatile bool rightCountingSteps = false;

volatile bool leftTimedOut = true;
volatile bool rightTimedOut = true;

const static long motorTimeoutLimit = 20;
long lmotorTimeoutCounter;
long rmotorTimeoutCounter;

const byte leftMotorLookup[8] = { B10000000, B11000000, B01000000, B01100000, B00100000, B00110000, B00010000, B10010000 };
const byte rightMotorLookup[8] = { B01000, B01100, B00100, B00110, B00010, B00011, B00001, B01001 };

#ifdef WEMOS

//declare variables for the motor pins
int rmotorPin1 = 16;    // Blue   - 28BYJ48 pin 1
int rmotorPin2 = 5;    // Pink   - 28BYJ48 pin 2
int rmotorPin3 = 4;   // Yellow - 28BYJ48 pin 3
int rmotorPin4 = 0;   // Orange - 28BYJ48 pin 4
             // Red    - 28BYJ48 pin 5 (VCC)

int lmotorPin1 = 2;    // Blue   - 28BYJ48 pin 1
int lmotorPin2 = 14;    // Pink   - 28BYJ48 pin 2
int lmotorPin3 = 12;   // Yellow - 28BYJ48 pin 3
int lmotorPin4 = 13;   // Orange - 28BYJ48 pin 4
             // Red    - 28BYJ48 pin 5 (VCC)


int motorSpeed = 1200;  //variable to set stepper speed
int count = 0;          // count of steps made
int countsperrev = 512; // number of steps per full revolution
int lookup[8] = { B01000, B01100, B00100, B00110, B00010, B00011, B00001, B01001 };


//////////////////////////////////////////////////////////////////////////////
void setupWemos() {
  //declare the motor pins as outputs
  pinMode(lmotorPin1, OUTPUT);
  pinMode(lmotorPin2, OUTPUT);
  pinMode(lmotorPin3, OUTPUT);
  pinMode(lmotorPin4, OUTPUT);
  pinMode(rmotorPin1, OUTPUT);
  pinMode(rmotorPin2, OUTPUT);
  pinMode(rmotorPin3, OUTPUT);
  pinMode(rmotorPin4, OUTPUT);
}


void setLeft(int leftOut)
{
  digitalWrite(lmotorPin1, bitRead(lookup[leftOut], 0));
  digitalWrite(lmotorPin2, bitRead(lookup[leftOut], 1));
  digitalWrite(lmotorPin3, bitRead(lookup[leftOut], 2));
  digitalWrite(lmotorPin4, bitRead(lookup[leftOut], 3));
}

void setRight(int rightOut)
{
  digitalWrite(rmotorPin1, bitRead(lookup[rightOut], 0));
  digitalWrite(rmotorPin2, bitRead(lookup[rightOut], 1));
  digitalWrite(rmotorPin3, bitRead(lookup[rightOut], 2));
  digitalWrite(rmotorPin4, bitRead(lookup[rightOut], 3));
}

#endif


void leftStep()
{
  // If we are not moving, don't do anything
  if (!leftMoving)return;

  // Move the motor one step

#ifdef WEMOS
  setLeft(lmotorPos);
#else
  PORTD = (PORTD & 0x0F) + leftMotorLookup[lmotorPos];
#endif

  // Update and wrap the waveform position
  lmotorPos += lmotorDelta;
  if (lmotorPos == 8) lmotorPos = 0;
  if (lmotorPos < 0) lmotorPos = 7;

  // If we are not counting steps - just return

  if (!leftCountingSteps)
    return;

  // Check for end of move
  if (++leftStepCounter >= leftStepLimit)
  {
    leftMoving = false;
    lmotorTimeoutCounter = 0;
  }
}

void rightStep()
{
  if (!rightMoving)return;

#ifdef WEMOS
  setRight(rmotorPos);
#else
  PORTB = (PORTB & 0xF0) + rightMotorLookup[rmotorPos];
#endif

  rmotorPos -= rmotorDelta;
  if (rmotorPos == 8) rmotorPos = 0;
  if (rmotorPos < 0) rmotorPos = 7;

  if (!rightCountingSteps)
    return;

  if (++rightStepCounter >= rightStepLimit)
  {
    rightMoving = false;
    rmotorTimeoutCounter = 0;
  }
}

void rightStop()
{
  rightMoving = false;
  PORTB = (PORTB & 0xF0);
  rmotorPos = 0;
  rightStepCounter = 0;
}

void leftStop()
{
  leftMoving = false;
  PORTD = (PORTD & 0x0F);
  lmotorPos = 0;
  leftStepCounter = 0;
}

int leftCountsPerStep = 10;
int rightCountsPerStep = 10;
int leftCounter = 0;
int rightCounter = 0;

void motorUpdate()
{
  if (!leftTimedOut)
  {
    if (!leftMoving)
    {
      if (++lmotorTimeoutCounter > motorTimeoutLimit)
      {
        leftStop();
        leftTimedOut = true;
      }
    }
    else {
      if (++leftCounter >= leftCountsPerStep)
      {
        leftStep();
        leftCounter = 0;
      }
    }
  }

  if (!rightTimedOut)
  {
    if (!rightMoving)
    {
      if (++rmotorTimeoutCounter > motorTimeoutLimit)
      {
        rightStop();
        rightTimedOut = true;
      }
    }
    else
    {
      if (++rightCounter >= rightCountsPerStep)
      {
        rightStep();
        rightCounter = 0;
      }
    }
  }
}

void leftDriveOn(bool countingSteps)
{
  leftCountingSteps = countingSteps;
  lmotorPos = 0;
  leftCounter = 0;
  leftStepCounter = 0;
  leftTimedOut = false;
  lmotorTimeoutCounter = 0;
  leftMoving = true;
}

void leftForwards(long countsPerStep)
{
  leftStepLimit = 0;
  leftCountsPerStep = countsPerStep;
  lmotorDelta = 1;
  leftDriveOn(false);
}

void leftBackwards(long countsPerStep)
{
  leftCountingSteps = false;
  leftStepLimit = 0;
  leftCountsPerStep = countsPerStep;
  lmotorDelta = -1;
  leftDriveOn(false);
}

void rightDriveOn(bool countingSteps)
{
  rightCountingSteps = countingSteps;
  rmotorPos = 0;
  rightCounter = 0;
  rightStepCounter = 0;
  rightTimedOut = false;
  rmotorTimeoutCounter = 0;
  rightMoving = true;
}

void rightForwards(long countsPerStep)
{
  rightStepLimit = 0;
  rightCountsPerStep = countsPerStep;
  rmotorDelta = 1;
  rightDriveOn(false);
}

void rightBackwards(long countsPerStep)
{
  rightStepLimit = 0;
  rightCountsPerStep = countsPerStep;
  rmotorDelta = -1;
  rightDriveOn(false);
}

void motorStop()
{
  leftStop();
  rightStop();
}

bool motorsMoving()
{
  if (rightMoving)return true;
  if (leftMoving)return true;
  return false;
}

void waitForMotorsStop()
{
  while (motorsMoving())
    delay(1);
}

void calculateSteps(float moveDistance, float wheelcircumference, volatile long *moveSteps, volatile char *moveDelta)
{
  if (moveDistance < 0)
  {
    *moveDelta = -1;
    moveDistance = moveDistance *-1;
  }
  else
  {
    *moveDelta = 1;
  }

  float turns = moveDistance / wheelcircumference;
  *moveSteps = (long)(turns*countsperrev);

#ifdef VERBOSE
  Serial.print("calculateMove Turns: ");
  Serial.println(turns);
#endif
}

void moveRobot(int leftDistance, int rightDistance, int leftSpeed, int rightSpeed)
{
  calculateSteps(leftDistance, leftWheelCircumference, &leftStepLimit, &lmotorDelta);
  calculateSteps(rightDistance, rightWheelCircumference, &rightStepLimit, &rmotorDelta);
  leftDriveOn(true);
  rightDriveOn(true);
}


void moveRobotForwards(int leftSpeed, int rightSpeed)
{
  leftForwards(leftSpeed);
  rightForwards(rightSpeed);
}


void rotateRobot(float angle, int rotateSpeed)
{
  float noOfTurns = angle / 360.0;
  float distanceToRotate = noOfTurns*turningCircle;
  moveRobot(distanceToRotate, -distanceToRotate, rotateSpeed, rotateSpeed);

#ifdef VERBOSE
  Serial.print(". angle: ");
  Serial.print(angle);
  Serial.print(" rotateSpeed: ");
  Serial.print(rotateSpeed);
  Serial.print(" noOfTurns: ");
  Serial.print(noOfTurns);
  Serial.print(" distanceToRotate: ");
  Serial.println(distanceToRotate);
#endif
}


///////////////////////////////////////////////////////////
/// Light sensor
///////////////////////////////////////////////////////////

int getLightLevel()
{
  return analogRead(A0);
}

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

void setupRobotNotors()
{
#ifdef WEMOS

  setupWemos();

#else

  DDRD = 0xFF;
  DDRB = 0x0F;

#endif

  leftWheelCircumference = 2 * PI * leftWheelRadius;
  rightWheelCircumference = 2 * PI * rightWheelRadius;
  turningCircle = wheelSpacing * PI;
  ticksPerSecond = 1000000 / motorStepTime;
  Timer1.initialize(motorStepTime);
  Timer1.attachInterrupt(motorUpdate);

#ifdef VERBOSE
  Serial.print(".leftWheelCircumference: ");
  Serial.print(leftWheelCircumference);
  Serial.print(" rightWheelCircumference: ");
  Serial.print(rightWheelCircumference);
  Serial.print(" turningCircle: ");
  Serial.print(turningCircle);
  Serial.print(" ticksPerSecond: ");
  Serial.println(ticksPerSecond);
#endif

}



