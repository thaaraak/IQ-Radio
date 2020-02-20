
#include <si5351.h>
#include <LiquidCrystal.h>

static int pinA = 2; // Our first hardware interrupt pin is digital pin 2
static int pinB = 3; // Our second hardware interrupt pin is digital pin 3
static int pinModeChange = 9;

volatile byte aFlag = 0; // let's us know when we're expecting a rising edge on pinA to signal that the encoder has arrived at a detent
volatile byte bFlag = 0; // let's us know when we're expecting a rising edge on pinB to signal that the encoder has arrived at a detent (opposite direction to when aFlag is set)
volatile int encoderPos = 0; //this variable stores our current value of encoder position. Change to int or uin16_t instead of byte if you want to record a larger range than 0-255
volatile byte oldEncPos = 0; //stores the last encoder position value so we can compare to the current reading and see if it has changed (so we know when to print to the serial monitor)
volatile byte reading = 0; //somewhere to store the direct values we read from our interrupt pins before checking to see if we have moved a whole detent


// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 0, en = 1, d4 = PD5, d5 = PD6, d6 = PD7, d7 = 8;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


int lastModeChange = -1;

#define DISPLAY_MODE_FREQ 1
#define DISPLAY_MODE_CURSOR 2
int displayMode = DISPLAY_MODE_FREQ;
long lastTimeFreqChanged = 0;

#define BAND_START 7000000
#define BAND_END 7200000
volatile long currentFrequency = BAND_START;
long oldFrequency = -1;

#define CURSOR_START 9
int cursorPosition = CURSOR_START;
long radix = 1;

Si5351 si5351;

void setup() {
  pinMode(pinModeChange, INPUT);
  pinMode(pinA, INPUT); // set pinA as an input, pulled HIGH to the logic voltage (5V or 3.3V for most cases)
  pinMode(pinB, INPUT); // set pinB as an input, pulled HIGH to the logic voltage (5V or 3.3V for most cases)
  attachInterrupt(0,PinA,RISING); // set an interrupt on PinA, looking for a rising edge signal and executing the "PinA" Interrupt Service Routine (below)
  attachInterrupt(1,PinB,RISING); // set an interrupt on PinB, looking for a rising edge signal and executing the "PinB" Interrupt Service Routine (below)

/*
  Serial.begin(9600); // start the serial monitor link
  Serial.println("Starting...");
*/

  bool i2c_found = si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_2MA);

  checkFrequencyChange();
  
  lcd.begin(16, 2);
  lcd.display();
  changeDisplay( 0 );

}

void changeDisplay( int direction )
{
  if (displayMode == DISPLAY_MODE_FREQ )
    changeFrequency( direction );
  else
    changeCursor( direction );
}

void changeFrequency( int direction )
{
  lcd.noCursor();

  long newFrequency = currentFrequency + direction * radix;

  if ( newFrequency < BAND_START || newFrequency > BAND_END )
    return;

  currentFrequency = newFrequency;

  char cFrequency[11];
  int mhz = currentFrequency / 1000000;
  int khz = ( currentFrequency - mhz * 1000000 ) / 1000;
  int hz = currentFrequency % 1000;
  sprintf( cFrequency, "%2d,%03d.%03d", mhz, khz, hz );
  lcd.setCursor( 0, 0 );
  lcd.print( cFrequency );

/*
  lcd.setCursor( 0, 1 );
  lcd.print( currentFrequency );
  lcd.setCursor( 10, 1 );
  lcd.print( "       " );
  lcd.setCursor( 10, 1 );
  lcd.print( radix );
*/
}

void changeCursor( int direction )
{
  int oldCursor = cursorPosition;
  
  cursorPosition += direction;
  if ( cursorPosition == 6 || cursorPosition == 2 )
    cursorPosition += direction;

  if (cursorPosition < 1 )
    cursorPosition = 1;
  if (cursorPosition > CURSOR_START )
    cursorPosition = CURSOR_START;

  lcd.setCursor( cursorPosition, 0 );
  lcd.cursor();

  if ( oldCursor != cursorPosition )
  {
    oldCursor = cursorPosition;
    if ( direction > 0 )
      radix /= 10;
    else
      radix *= 10;
  }
}

void PinA(){
  cli(); //stop interrupts happening before we read pin values
  reading = PIND & 0xC; // read all eight pin values then strip away all but pinA and pinB's values
  if(reading == B00001100 && aFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    changeDisplay( -1 );
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00000100) bFlag = 1; //signal that we're expecting pinB to signal the transition to detent from free rotation
  sei(); //restart interrupts
}

void PinB(){
  cli(); //stop interrupts happening before we read pin values
  reading = PIND & 0xC; //read all eight pin values then strip away all but pinA and pinB's values
  if (reading == B00001100 && bFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    changeDisplay( 1 );
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00001000) aFlag = 1; //signal that we're expecting pinA to signal the transition to detent from free rotation
  sei(); //restart interrupts
}

void changeDisplayMode()
{
  if (displayMode == DISPLAY_MODE_FREQ )
    displayMode = DISPLAY_MODE_CURSOR;
  else
    displayMode = DISPLAY_MODE_FREQ;
  changeDisplay( 0 );
}    

void checkModeChange()
{
  int modeChange = digitalRead( pinModeChange );
  if ( modeChange != lastModeChange )
  {
    lastModeChange = modeChange;    
    if ( !modeChange )
      changeDisplayMode();
  }
}

void checkFrequencyChange()
{
  long currentTime = millis();

  if ( currentFrequency == oldFrequency )
    return;

  oldFrequency = currentFrequency;
  uint64_t freq = currentFrequency * 100ULL;
  uint64_t pllFreq = freq * 100ULL;

  si5351.set_freq_manual(freq, pllFreq, SI5351_CLK1);
  si5351.set_freq_manual(freq, pllFreq, SI5351_CLK2);
  si5351.set_phase(SI5351_CLK1, 0);
  si5351.set_phase(SI5351_CLK2, 100);
  si5351.pll_reset(SI5351_PLLA);

}

void loop()
{
  checkModeChange();
  checkFrequencyChange();
}
