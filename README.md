# RobotSensorsAndMotors

This provides the motor control and sensor integration for an Arduino powered robot. It also contains code to drive a number of Neopixel (WS8212) devices that are attached to a HullPixelbot. 

A programmer can use the motor and sensor API to create a free-standing robot with particular behaviours. This can be achieved by modifying the setup and loop elements of the Arduino applicaton. 

Alternatively this code can serve as the slave component of a dual processor robot. The robot will respond to text based commands which are delivered via the serial port. 
