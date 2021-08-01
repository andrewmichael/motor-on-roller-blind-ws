/*
Extends the Serial class to encode SLIP over serial
*/

#ifndef AB_Stepper_28BYJ_48_h
#define AB_Stepper_28BYJ_48_h


#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif


class AB_Stepper_28BYJ_48 {
	

public:
	

	AB_Stepper_28BYJ_48(int pin_1n1, int pin_1n2, int pin_1n3, int pin_1n4);
    void step(int);
   

private:
	void setOutput(int out);
  int motorSpeed = 2;  //variable to set stepper speed
  int pin_1n1; int pin_1n2; int pin_1n3; int pin_1n4;

};


#endif
