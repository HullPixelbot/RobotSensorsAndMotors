///////////////////////////////////////////////////////////
/// Distance Reading
///////////////////////////////////////////////////////////

const int trigPin = 3;       // trigger pin for distance 
const int echoPin = 2;       // echo pin for distance
volatile long pulseStartTime;
volatile long pulseWidth;

void triggerDistanceSensor()
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);
}

void pulseEvent()
{

#ifdef WEMOS
  if (digitalRead(echoPin)) {
#else
  if (PIND & (1 << echoPin)) {
#endif
    // pulse gone high - record start
    pulseStartTime = micros();
  }
  else
  {
    pulseWidth = micros() - pulseStartTime;
    triggerDistanceSensor();
  }
}

float getDistanceValue()
{
  return pulseWidth / 58.2;
}

void setupDistanceSensor()
{
  pulseWidth = 0;
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(echoPin), pulseEvent, CHANGE);
  triggerDistanceSensor();
}

