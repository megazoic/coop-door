// simplifying this code to allow access to pins previously inaccessible due to tone generation
//**********************************************************
#include <avr/interrupt.h>
// pin assignments
const int firstLEDPin = 7; //both LEDs are two digit binary (this is first digit)
const int secondLEDPin = 6; //second digit
const int speakerPin = 9; //speaker
const int doorSwitchesPin = 0; // Interrupt on digital pin 2 has two switches in series
                            // SW1 at door is NC, SW2 at motor/arm linkage is NO
                            // digital pin 2 (Active low) is actually tied to interrupt 0
const int mtrControlPin = 3; //controls MOSFET
const int setTestingPin = 5; // button to set hasBeenNight = T so don't have to wait
                             // for timeToDelay. setTestingPin is pulled high (active low)
const int ldrPin = A0;
const int outerDoorPin = 13; // for MOSFET controlling relay
// state variables
volatile int doorPinCount = 0; //count SW2 NO closures after door opened (SW1 NC is now closed)
int setTestingState = 0; //used to record state of push button that sets hasBeenNight true
int errorState = 0; //0 none; 1 door already open; 2 door initially stuck; 3 door stuck after
                    //some travel; 4 LDR not over threshold despite 24hr passing
int nonErrorState = 0; // 0 hasBeenNight false and LDR false (below threshold);
//                        1 hasBeenNight true, LDR false;
//                        2 hasBeenNight false, LDR true;
//                        3 hasBeenNight true, LDR true.
boolean hasBeenNight = 0;
unsigned long ulLastMTRUpdate = 0; //set to millis() when door is opened
int ldrValue = 0;
int ldrTrigger = 600;
/*
* timeToDelay is designed to be time to wait from door
* opening until close to next dawn (within 2+hr)
* 72,000 seconds is 20 hr so make timeToDelay = 50 400 000
*/
const unsigned long timeToDelay = 46800000; // 13 hr
int timeToRemoveLDRNoise = 300000;
                                          
unsigned long ulLastLDRUpdate = 0;

void setup(){
  pinMode(firstLEDPin, OUTPUT);
  pinMode(secondLEDPin, OUTPUT);
  pinMode(speakerPin, OUTPUT);
  pinMode(mtrControlPin, OUTPUT);
  pinMode(outerDoorPin, OUTPUT);
  pinMode(setTestingPin, INPUT);
  digitalWrite(mtrControlPin, LOW);
  digitalWrite(outerDoorPin, LOW);
  LEDIndicator(0,0);  //initially, LDR is low and hasBeenNight is false
  //**************** this needs to change *******************
  // Should be RISING or FALLING
  //*********************************************************
  attachInterrupt(doorSwitchesPin, doorPinISR, LOW);
}
void loop(){
  ldrValue = analogRead(ldrPin); // check if light outside
  // external pushbutton input to allow door to open out of timed day/night sequence
  setTestingState = digitalRead(setTestingPin);
  if (setTestingState == LOW){ // active low Testing is now Active
    hasBeenNight = 1;
    errorState = 0;
    timeToRemoveLDRNoise = 6000;
	//**********************setting status needs revision ****************************
    //digitalWrite(hasBeenNightLEDPin, HIGH); //Indicate on LED
  }
  if (hasBeenNight == 1 && (ldrValue < ldrTrigger)){
	//**********************setting status needs revision ****************************
    //double check that dawn and not just random light from car, etc.
    delay(timeToRemoveLDRNoise);
    ldrValue = analogRead(ldrPin);
    if(ldrValue < ldrTrigger){
      openDoor();
    }
  }
  if (ulLastLDRUpdate + timeToDelay <= millis()){
    //enough time has passed since last time opened door
    //so OK to open door again
    hasBeenNight = 1;
    digitalWrite(hasBeenNightLEDPin, HIGH);
	//**********************setting status needs revision ****************************
  }
  if (errorState == 1){
    errorCode();
  }
  delay(500);
}//loop
void openDoor(){
  delay(2000);
  ulLastMTRUpdate = millis();
  digitalWrite(mtrControlPin, HIGH);
  digitalWrite(mtrLedPin, HIGH);
  doorPinCount = 0;
  // give door enough time to open far enough for both SW1 and SW2 of doorSwitchesPin 
  // to close and fire interrupt raising doorPinCount > 1. If doorPinCount does become > 0
  // before this time (about 6 sec), door is already open and error condition exists
  //*****************TEST FOR ERROR CONDITIONS*************************
  while (ulLastMTRUpdate + 4000 > millis()){
    if(doorPinCount > 1){
      errorState = 1; // door is already open
      resetDoor();
      return;
    }
    delay(1000);
  }
  delay(2000); // let door fully clear SW1 so it can close and counter can count SW2 action
  if (doorPinCount == 0){
    errorState = 1; // door is stuck
    resetDoor();
    return;
  }
  while (doorPinCount < 11){ // wait for door to open
    if (ulLastMTRUpdate + 16000 < millis()){
      errorState = 1; // has been enoughtime and door has not fully opened
      resetDoor();
      return;
    }
    delay(50);
  }
  //*******************DOOR SUCCESSFULLY OPENED************************
  delay(1000);//bring arm out of contact with SW2 (open SW2)
  resetDoor();
  // open outer door after delay
  delay(30000);
  digitalWrite(outerDoorPin, HIGH);
  delay(2000);
  digitalWrite(outerDoorPin, LOW);
}//openDoor
void resetDoor(){
  digitalWrite(mtrControlPin, LOW);
  digitalWrite(mtrLedPin, LOW);
  digitalWrite(hasBeenNightLEDPin, LOW);
  ulLastLDRUpdate = millis();
  hasBeenNight = 0;
  timeToRemoveLDRNoise = 300000;
}//resetDoor
//*************************Needs to be reworked so that this routine doesn't burn up
//cycles****************************************************************************
void errorCode(){// need to have a way to break out of this after some period of time
                 // in addition to the push button (after a certain amount of time)
  while(errorState == 1){
    digitalWrite(mtrLedPin, HIGH);
    delay(500);
    digitalWrite(mtrLedPin, LOW);
    delay(500);
    errorState = digitalRead(setTestingPin);
    if (ulLastLDRUpdate + 3600000 < millis()){
      errorState = 0; // reset after 30 min
    }
  }
  delay(1000);//put this here to avoid the problem of resetting
  //hasBeenNight to 1 with the same btn press that cancels errorState
}//errorCode
void LEDIndicator(int digit2, int digit1){
	//set the led pattern
	digitalWrite(firstLEDPin, digit1);
	digitalWrite(secondLEDPin, digit2);
}
void doorPinISR(){ //Interrupt to handle door switch (SW1 and SW2) activity
	// this called when digital pin 2 is low
  static unsigned long last_millis = 0;
  unsigned long m = millis();
  if (m - last_millis < 200){
    //ignore
  } else {
    ++doorPinCount; // probably need to be sure motor is on before incrementing this
  }
  last_millis = m;
}//doorPinISR
