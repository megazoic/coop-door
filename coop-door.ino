// simplifying this code to allow access to pins previously inaccessible due to tone generation
//**********************************************************
#include <avr/interrupt.h>
// pin assignments
const int ledPins[] = {4,7,6}; //3 digit binary and order is dictated by how the LEDs are
							//wired on the Sparkfun Protoboard
const int speakerPin = 9; //speaker
const int doorSwitchesPin = 0; // Interrupt on digital pin 2 has two switches in series
                            // SW1 at door is NC, SW2 at motor/arm linkage is NO
                            // digital pin 2 (Active low) is actually tied to interrupt 0
const int mtrControlPin = 3; //controls MOSFET
const int setTestingPin = 5; // button to set hasBeenNight = T so don't have to wait
                             // for timeToDelay. setTestingPin is pulled high (active low)
const int ldrPin = A0;
// state variables
volatile int doorPinCount = 0; //count SW2 NO closures after door opened (SW1 NC is now closed)
int setTestingState = 0; //used to record state of push button that sets hasBeenNight true
int systemState = 0;//from 0 to 7 and used in binary display on 3 led indicators with the following
//0 NOT IN USE (INITIAL STATE)
//1 HASBEENNIGHT = 1, LDR < LDRTRIGGER
//2 HASBEENNIGHT = 1, LDR > LDRTRIGGER
//3 HASBEENNIGHT = 0, LDR < LDRTRIGGER
//4 HASBEENNIGHT = 0, LDR > LDRTRIGGER
//5 ERROR, DOOR INITIALLY STUCK
//6 ERROR, DOOR TAKING TOO LONG
//7 ERROR, DOOR ALREADY OPEN
boolean hasBeenNight = 0;
boolean isLight = 0;
unsigned long ulLastMTRUpdate = 0; //set to millis() when door is opened
									//used to check for various error conditions
int ldrValue = 0;
int ldrTrigger = 600;
/*
* timeToDelay is designed to be time to wait from door
* opening until close to next dawn (within 2+hr)
* 72 000 seconds is 20 hr so make timeToDelay = 50 400 000
*/
const unsigned long timeToDelay = 46800000; // 13 hr
int timeToRemoveLDRNoise = 300000;//used to allow for ldr to be illuminated by artificial transient light
unsigned long ulLastLDRUpdate = 0;

void setup(){
  for (int i=0; i<3; i++){
    pinMode(ledPins[i], OUTPUT);
  }
  pinMode(speakerPin, OUTPUT);
  pinMode(mtrControlPin, OUTPUT);
  pinMode(setTestingPin, INPUT);
  digitalWrite(mtrControlPin, LOW);
  displayBinary(systemState);  //initially, no info given on the LED indicator
  analogWrite(speakerPin, 20);
  delay(200);
  analogWrite(speakerPin, 0);
  attachInterrupt(doorSwitchesPin, doorPinISR, FALLING);
}
void loop(){
  ldrValue = analogRead(ldrPin); // check if light outside
  if (ldrValue < ldrTrigger){
	  isLight = 1;
  }
  
  // external pushbutton input to allow door to open out of timed day/night sequence
  setTestingState = digitalRead(setTestingPin);
  if (setTestingState == LOW){ // active low Testing is now Active
    hasBeenNight = 1;
    timeToRemoveLDRNoise = 6000;
  }
  if (hasBeenNight == 1 && isLight == 1){
    //double check that dawn and not just random light from car, etc.
    delay(timeToRemoveLDRNoise);
    ldrValue = analogRead(ldrPin);
    if(ldrValue < ldrTrigger){//recheck the LDR without referring to isLight variable
      openDoor();
    }
  }
  if (ulLastLDRUpdate + timeToDelay <= millis()){
    //enough time has passed since last time opened door
    //so OK to open door again
    hasBeenNight = 1;
  }
  if(systemState < 5){//need to set binary readout for non-error state
	  if(hasBeenNight == 1){
		  if(isLight == 1){
			  systemState = 1;//enough time has passed to open door and it's daylight
		  }else{
			  systemState = 2;//enough time has passed to open door and it is dark
		  }
	  }
	  if(isLight == 1){
		  systemState = 3;//not enough time has passed to open door and it's daylight
	  }else{
		  systemState = 4;//not enough time has passed to open door and it is dark
	  }
  }//finished setting binary readout for non-error state
  displayBinary(systemState);
  delay(500);
}//loop
void openDoor(){
    for(int i = 0; i<3; ++i){
		analogWrite(speakerPin, 20);
    	delay(200);
    	analogWrite(speakerPin, 0);
	}
  delay(2000);
  ulLastMTRUpdate = millis();
  digitalWrite(mtrControlPin, HIGH);
  doorPinCount = 0;
  // give door enough time to open far enough for both SW1 and SW2 of doorSwitchesPin 
  // to close and fire interrupt raising doorPinCount > 1. If doorPinCount does become > 0
  // before this time (about 6 sec), door is already open and error condition exists
  //*****************TEST FOR ERROR CONDITIONS*************************
  while (ulLastMTRUpdate + 4000 > millis()){
    if(doorPinCount > 1){//ERROR, door already open
		systemState = 7;
      resetDoor();
      return;
    }
    delay(1000);
  }
  delay(2000); // let door fully clear SW1 so it can close and counter can count SW2 action
  if (doorPinCount == 0){//ERROR, door must be stuck
	  systemState = 5;
    resetDoor();
    return;
  }
  while (doorPinCount < 11){ // wait for door to open
    if (ulLastMTRUpdate + 16000 < millis()){//ERROR, door is stuck
		systemState = 6;
      resetDoor();
      return;
    }
    delay(50);
  }
  //*******************DOOR SUCCESSFULLY OPENED************************
  delay(1000);//bring arm out of contact with SW2 (open SW2)
  resetDoor();
}//openDoor
void resetDoor(){
  digitalWrite(mtrControlPin, LOW);
  ulLastLDRUpdate = millis();
  hasBeenNight = 0;
  timeToRemoveLDRNoise = 300000;
}//resetDoor
void displayBinary(byte numToShow){
  for (int i =0;i<3;i++)
  {
    if (bitRead(numToShow, i)==1)
    {
      digitalWrite(ledPins[i], HIGH);
    }
    else
    {
      digitalWrite(ledPins[i], LOW);
    }
  }

}
void doorPinISR(){ //Interrupt to handle door switch (SW1 and SW2) activity
	// this called when digital pin 2 goes from high to low (FALLING)
  static unsigned long last_millis = 0;
  unsigned long m = millis();
  if (m - last_millis < 200){
    //ignore
  } else {
    ++doorPinCount; // probably need to be sure motor is on before incrementing this
  }
  last_millis = m;
}//doorPinISR
