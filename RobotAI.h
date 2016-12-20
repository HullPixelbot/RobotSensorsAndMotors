/////////////////////////////////////////////
//
//  Robot AI
//
/////////////////////////////////////////////

//#define AI_VERBOSE

enum robotState
{
  starting,
  moving,
  turning
};

robotState state = starting;

float turnStartTrigger = 5.0;
float turnEndTrigger = 10.0;
float turnAngle = 20;

void startMoving()
{
#ifdef AI_VERBOSE
  Serial.println(".**startMoving");
#endif
  moveRobotForwards(remoteLeftSpeed, remoteRightSpeed);
  state = moving;
  flickeringColouredLights(green, 0, 200);
}


void updateStarting()
{
  startMoving();
}

void startTurning()
{
  motorStop();
  rotateRobot(turnAngle, remoteRotateSpeed);
  state = turning;
  flickeringColouredLights(red, 0, 200);
}

void updateMoving()
{
  float distance = getDistanceValue();

  if (distance < turnStartTrigger)
  {
#ifdef AI_VERBOSE
    Serial.print(".  obstacle: ");
    Serial.println(distance);
#endif
    startTurning();
  }
}

void updateTurning()
{
  if (!motorsMoving())
  {
    float distance = getDistanceValue();

    if (distance > turnEndTrigger)
    {
#ifdef AI_VERBOSE
      Serial.print(".  clear: ");
      Serial.println(distance);
#endif
      startMoving();
    }
    else
    {
#ifdef AI_VERBOSE
      Serial.print(".  rotate: ");
      Serial.println(distance);
#endif
      rotateRobot(turnAngle, remoteRotateSpeed);
    }
  }
}

void robotAI()
{
#ifdef AI_VERBOSE
  float distance = getDistanceValue();
  Serial.print(".  distance: ");
  Serial.println(distance);
#endif

  switch (state)
  {
  case starting:
    updateStarting();
    break;
  case moving:
    updateMoving();
    break;
  case turning:
    updateTurning();
    break;
  default:
    break;
  }
}

