// testing button control of motor
// run motor for 2 sec then check if door has opened (if not stop)
// else continue to run motor until button presses after light
// then stop
#include <avr/interrupt.h>
/********* sine wave parameters ************/
#define PI2     6.283185 // 2 * PI - saves calculating it later
#define AMP     127      // Multiplication factor for the sine wave
#define OFFSET  128      // Offset shifts wave to just positive values

/******** Lookup table ********/
#define LENGTH  256  // The length of the waveform lookup table
byte waveBase[LENGTH];   // Storage for the waveform
byte wave[LENGTH];

// pin assignments
const int hasBeenNightLEDPin = 7; //LED to indicate this condition is true
const int speakerPin = 9; //speaker
const int doorSwitchesPin = 0; // Interrupt on digital pin 2 has two switches in series
                            // SW1 at door is NC, SW2 at motor/arm linkage is NO
const int mtrControlPin = 3; //controls MOSFET
const int setTestingPin = 5; // button to set hasBeenNight = T so don't have to wait
                             // for timeToDelay. setTestingPin is pulled high (active low)
const int mtrLedPin = 6;
const int ldrPin = A0;
const int outerDoorPin = 13; // for MOSFET controlling relay
// state variables
volatile int doorPinCount = 0; //count SW2 NO closures after door opened (SW1 NC is now closed)
int setTestingState = 0; //used to record state of push button that sets hasBeenNight true
int errorState = 0;

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
  /******** Populate the waveform lookup table with a sine wave ********/
  for (int i=0; i<LENGTH; i++) {
    float v = (AMP*sin((PI2/LENGTH)*i));  // Calculate current entry
    waveBase[i] = int(v+OFFSET);              // Store value as integer
  }
  pinMode(hasBeenNightLEDPin, OUTPUT);
  pinMode(speakerPin, OUTPUT);
  pinMode(mtrControlPin, OUTPUT);
  pinMode(mtrLedPin, OUTPUT);
  pinMode(outerDoorPin, OUTPUT);
  pinMode(setTestingPin, INPUT);
  digitalWrite(hasBeenNightLEDPin, LOW);
  digitalWrite(mtrControlPin, LOW);
  digitalWrite(outerDoorPin, LOW);
  attachInterrupt(doorSwitchesPin, doorPinISR, LOW);
  
  /******** Set timer1 for 8-bit fast PWM output to speakerPin ********/
  TCCR1B  = (1 << CS10);    // Set prescaler to full 16MHz
  TCCR1A |= (1 << COM1A1);  // PWM pin to go low when TCNT1=OCR1A
  TCCR1A |= (1 << WGM10);   // Put timer into 8-bit fast PWM mode
  TCCR1B |= (1 << WGM12); 

  /******** Set up timer 2 to call ISR to modulate PWM output ********/
  TCCR2A = 0;               // We need no options in control register A
  TCCR2B = (1 << CS21);     // Set prescaller to divide by 8
  //TIMSK2 = (1 << OCIE2A);   // Set timer to call ISR when TCNT2 = OCRA2
  OCR2A = 37;               // sets the frequency of the generated wave
  sei();                    // Enable interrupts to generate waveform!
}
void loop(){
  ldrValue = analogRead(ldrPin); // check if light outside
  // external pushbutton input to allow door to open out of timed day/night sequence
  setTestingState = digitalRead(setTestingPin);
  if (setTestingState == LOW){ // active low
    hasBeenNight = 1;
    errorState = 0;
    timeToRemoveLDRNoise = 6000;
    digitalWrite(hasBeenNightLEDPin, HIGH); //Indicate on LED
  }
  if (hasBeenNight == 1 && (ldrValue < ldrTrigger)){
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
  }
  if (errorState == 1){
    errorCode();
  }
  delay(500);
}//loop
void openDoor(){
  soundAlarm();
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
void doorPinISR(){ //Interrupt to handle door switch (SW1 and SW2) activity
  static unsigned long last_millis = 0;
  unsigned long m = millis();
  if (m - last_millis < 200){
    //ignore
  } else {
    ++doorPinCount; // probably need to be sure motor is on before incrementing this
  }
  last_millis = m;
}//doorPinISR
//******************* SOUND GENERATION FOR ALARM ************************//
void soundAlarm(){
  TIMSK2 = (1 << OCIE2A); // set TIMER2_COMPA_vect interrupt enable
  delay(10);
  for(int i=0; i<3; ++i){
    decay();
    delay(1000);
  }
  TIMSK2 &= ~(1 << OCIE2A); // clear TIMER2_COMPA_vect interrupt enable 
}
  
void decay(void) {
  memcpy(wave, waveBase, LENGTH); // copy the waveform storage to modifiable
  // array then modify wave[] by bringing values closer to 127.
  // This creates the fade out sound
  int i, j;
  for (j=0; j<128; j++) {
    for (i=0; i<LENGTH; i++) {
      if (wave[i]<127) {
        wave[i]++;
      }
      if (wave[i]>127) {
        wave[i]--;
      }
    }
    delay(15);
  }
}
/******** Used to modulate tone for alarm ********/
ISR(TIMER2_COMPA_vect) {  // Called each time TCNT2 == OCR2A
  static byte index=0;    // Points to successive entries in the wavetable
  OCR1AL = wave[index++]; // Update the PWM output
  asm("NOP;NOP");         // Fine tuning
  TCNT2 = 6;              // Timing to compensate for time spent in ISR
}

