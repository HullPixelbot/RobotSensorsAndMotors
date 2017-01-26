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

int getDistanceValue()
{
  return (int) (pulseWidth / 58);
}

void setupDistanceSensor()
{
  pulseWidth = 0;
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(echoPin), pulseEvent, CHANGE);
  triggerDistanceSensor();
}

void directDistanceReadTest()
{
	pinMode(trigPin, OUTPUT);
	pinMode(echoPin, INPUT);
	long duration, distance;
	while (true)
	{
		digitalWrite(trigPin, LOW);
		delayMicroseconds(2); 
		digitalWrite(trigPin, HIGH);
		delayMicroseconds(10);
		digitalWrite(trigPin, LOW);
		duration = pulseIn(echoPin, HIGH);
		distance = (duration / 2) / 29;
		Serial.println(distance);
		delay(500);
	}
}

