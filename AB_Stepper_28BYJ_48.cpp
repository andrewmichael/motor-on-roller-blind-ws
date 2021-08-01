#include "AB_Stepper_28BYJ_48.h"



AB_Stepper_28BYJ_48::AB_Stepper_28BYJ_48(int _pin_1n1, int _pin_1n2, int _pin_1n3, int _pin_1n4) {
  
  pin_1n1 = _pin_1n1;
  pin_1n2 = _pin_1n2;
  pin_1n3 = _pin_1n3;
  pin_1n4 = _pin_1n4;

  pinMode(pin_1n1, OUTPUT);
  pinMode(pin_1n2 , OUTPUT);
  pinMode(pin_1n3, OUTPUT);
  pinMode(pin_1n4, OUTPUT);

  digitalWrite(pin_1n1, LOW);
  digitalWrite(pin_1n2, LOW);
  digitalWrite(pin_1n3, LOW);
  digitalWrite(pin_1n4, LOW);
};

void AB_Stepper_28BYJ_48::step( int count) {
  while ( count > 0 ) {
    for (int i = 0; i < 4; i++)
    {
      setOutput(i);
      delay(motorSpeed);
    }
    count--;
  }
  
  while ( count < 0 ) {
    for (int i = 3; i >= 0; i--)
    {
      setOutput(i);
      delay(motorSpeed);
    }
    count++;
  }
};

 void AB_Stepper_28BYJ_48::setOutput(int out) {
  switch(out){
    case 0:
      digitalWrite(pin_1n1, HIGH);
      digitalWrite(pin_1n2, HIGH);
      digitalWrite(pin_1n3, LOW);
      digitalWrite(pin_1n4, LOW);
      break;
    case 1:
      digitalWrite(pin_1n1, LOW);
      digitalWrite(pin_1n2, HIGH);
      digitalWrite(pin_1n3, HIGH);
      digitalWrite(pin_1n4, LOW);
      break;
    case 2:
      digitalWrite(pin_1n1, LOW);
      digitalWrite(pin_1n2, LOW);
      digitalWrite(pin_1n3, HIGH);
      digitalWrite(pin_1n4, HIGH);
      break;
    case 3:
      digitalWrite(pin_1n1, HIGH);
      digitalWrite(pin_1n2, LOW);
      digitalWrite(pin_1n3, LOW);
      digitalWrite(pin_1n4, HIGH);
      break;
  }
};