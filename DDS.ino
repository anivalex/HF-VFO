/*

*/

#define version "20170602"

#define DDS "AD9851"
// #define DDS "SI5351"

#define DISPLAY "OLED"
// #define DISPLAY "LCD"

#include <SPI.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 5
Adafruit_SSD1306 display(OLED_RESET);

#include <rotary.h>
#include <EEPROM.h>
#include <avr/io.h>

#define NUMFLAKES 10
#define XPOS 0
#define YPOS 1
#define DELTAY 2
#define LOGO16_GLCD_HEIGHT 16 e
#define LOGO16_GLCD_WIDTH  16 

static const unsigned char PROGMEM logo16_glcd_bmp[] =
{ B00000000, B11000000,
  B00000001, B11000000,
  B00000001, B11000000,
  B00000011, B11100000,
  B11110011, B11100000,
  B11111110, B11111000,
  B01111110, B11111111,
  B00110011, B10011111,
  B00011111, B11111100,
  B00001101, B01110000,
  B00011011, B10100000,
  B00111111, B11100000,
  B00111111, B11110000,
  B01111100, B11110000,
  B01110000, B01110000,
  B00000000, B00110000 };

#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif
//LiquidCrystal lcd(12, 13, 7, 6, 5, 4); // I used an odd pin combination because I need pin 2 and 3 for the interrupts. for LCD 16x2 - not used now

#define TX_RX (6)   // (2 sided 2 possition relay)
#define TX_ON (7)   // this is for microfone PTT in SSB transceivers (not need for EK1A)
#define CW_KEY (4)   // KEY output pin - in Q7 transistor colector (+5V when keyer down for RF signal modulation) (in Minima to enable sidetone generator on)
#define BAND_HI (6)  // relay for RF output LPF  - (0) < 10 MHz , (1) > 10 MHz (see LPF in EK1A schematic)  
//#define FBUTTON (A3)  // tuning step freq CHANGE from 1Hz to 1MHz step for single rotary encoder possition
#define ANALOG_KEYER (A1)  // KEYER input - for analog straight key
#define BTNDEC (A2)  // BAND CHANGE BUTTON from 1,8 to 29 MHz - 11 bands
Rotary r = Rotary(2,3); // sets the pins for rotary encoder uses.  Must be interrupt pins.

//AD9851 control
#define W_CLK 8   // Pin 8 - connect to AD9851 module word load clock pin (CLK)
#define FQ_UD 9   // Pin 9 - connect to freq update pin (FQ)
#define DATA 10   // Pin 10 - connect to serial data load pin (DATA)
#define RESET 11  // Pin 11 - connect to reset pin (RST) 

char inTx = 0;     // trx in transmit mode temp var
char keyDown = 0;   // keyer down temp vat
//Setup some items
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs
unsigned long cwTimeout = 0;     //keyer var - dead operator control

#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); }

int_fast32_t rx=0; // Starting frequency of VFO
int_fast32_t rx2=1; // temp variable to hold the updated frequency
int_fast32_t rxif=10245000; // IF freq, will be summed with vfo freq - rx variable
int_fast32_t default_freq[] = {1, 1838000, 3580000, 5250000, 7080000, 10142000, 14070000, 18100000, 21080000, 24920000, 28120000, 29100000, 50290000};

int_fast32_t increment = 1000; // starting VFO update increment in HZ. tuning step
int buttonstate = 0;   // temp var
String hertz = "1Khz";
int  hertzPosition = 0;

byte ones,tens,hundreds,thousands,tenthousands,hundredthousands,millions ;  //Placeholders
String freq; // string to hold the frequency
int_fast32_t timepassed = millis(); // int to hold the arduino miilis since startup
int memstatus = 1;  // value to notify if memory is current or old. 0=old, 1=current.
int ForceFreq = 0;  // Change this to 0 after you upload and run a working sketch to activate the EEPROM memory.  YOU MUST PUT THIS BACK TO 1 AND UPLOAD THE SKETCH AGAIN AFTER STARTING FREQUENCY IS SET!
int byteRead = 0;
int i=0;

const int colums = 10; /// have to be 16 or 20 - in LCD 16x2 - 16, or other , see LCD spec.
const int rows = 2;  /// have to be 2 or 4 - in LCD 16x2 - 2, or other , see LCD spec.
int lcdindex = 0;
int line1[colums];
int line2[colums];

// buttons temp var
int BTNdecodeON = 0;   
int BTNlaststate = 1;
int BTNcheck = 0;
int BTNcheck2 = 0;
int BAND = 0; // set number of default band minus 1 ---> (for 3.5MHz = 1)

void reset_EEPROM()
{
  Serial.println("Reseting EEPROM....");
  for(BAND=1;BAND < 13;BAND++)
  {
    Serial.println(BAND);
    rx=default_freq[BAND];
    showFreq();
    delay(1000);
    storeMEM();
  }
  
}

void checkTX(){   // this is stopped now, but if you need to use mike for SSB PTT button, start in main loop function - not fully tested after last changes
  //we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;

  if (digitalRead(TX_ON) == 0 && inTx == 0){
      //put the  TX_RX line to transmit
	  inTx = 1;
  }

  if (digitalRead(TX_ON) == 1 && inTx == 1){
      //put the  TX_RX line to transmit
      inTx = 0;
  }
  //put the  TX_RX line to transmit
  digitalWrite(TX_RX, inTx);
  //give the relays a few ms to settle the T/R relays
  delay(50);
}

/*CW is generated by keying the bias of a side-tone oscillator.
nonzero cwTimeout denotes that we are in cw transmit mode.
*/

void checkCW(){
  if (keyDown == 0 && analogRead(ANALOG_KEYER) < 50){
    //switch to transmit mode if we are not already in it
    if (inTx == 0){
      //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 1);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    }
    inTx = 1;
    keyDown = 1;
    digitalWrite(CW_KEY, 1); //start the side-tone
  }

  //reset the timer as long as the key is down
  if (keyDown == 1){
     cwTimeout = CW_TIMEOUT + millis();
  }

  //if we have a keyup
  if (keyDown == 1 && analogRead(ANALOG_KEYER) > 150){
    keyDown = 0;
    digitalWrite(CW_KEY, 0);  // stop the side-tone
    cwTimeout = millis() + CW_TIMEOUT;
  }

  //if we have keyuup for a longish time while in cw rx mode
  if (inTx == 1 && cwTimeout < millis()){
    //move the radio back to receive
    digitalWrite(TX_RX, 0);
	digitalWrite(CW_KEY, 0);
    inTx = 0;
    cwTimeout = 0;
  }
}

void setup() {

//set up the pins in/out and logic levels
pinMode(TX_RX, OUTPUT);
digitalWrite(TX_RX, LOW);
  
//pinMode(FBUTTON, INPUT);  
//digitalWrite(FBUTTON, 1);
  
pinMode(TX_ON, INPUT);    // need pullup resistor see Minima schematic
digitalWrite(TX_ON, LOW);
  
pinMode(CW_KEY, OUTPUT);
digitalWrite(CW_KEY, LOW);
  

// Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);
  Serial.print("Start VFO version ");
  Serial.println(version);

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C address 0x3C (for oled 128x32)
  display.display();
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Version:");
  display.setCursor(0,18);
  display.println(version);
  display.display();
  delay(1000);
 
  pinMode(BTNDEC,INPUT);		// band change button
  digitalWrite(BTNDEC,HIGH);    // level
  pinMode(A0,INPUT); // Connect to a button that goes to GND on push - rotary encoder push button - for FREQ STEP change
  digitalWrite(A0,HIGH);  //level
//  lcd.begin(16, 2);  // for LCD

  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();

// AD9851
  pinMode(FQ_UD, OUTPUT);
  pinMode(W_CLK, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(RESET, OUTPUT); 
  pulseHigh(RESET);
  pulseHigh(W_CLK);
  pulseHigh(FQ_UD);  // this pulse enables serial mode on the AD9851 - see datasheet
  BAND = int(EEPROM.read(0));
  freq = String(EEPROM.read((BAND*7)+1))+String(EEPROM.read((BAND*7)+2))+String(EEPROM.read((BAND*7)+3))+String(EEPROM.read((BAND*7)+4))+String(EEPROM.read((BAND*7)+5))+String(EEPROM.read((BAND*7)+6))+String(EEPROM.read((BAND*7)+7));
  rx = freq.toInt();
  Serial.print("Band from EEPROM: ");
  Serial.println(BAND);
 
 for (int index = 0; index < colums; index++){
    line1[index] = 32;
	line2[index] = 32;
 }
}

///// START LOOP - MAIN LOOP

void loop() {
	checkCW();   // when pres keyer
	checkTX();   // microphone PTT
	checkBTNdecode();  // BAND change

	
// freq change 
  if (rx != rx2){
		BTNcheck = 0;   
		if (BTNcheck == 0) {
			showFreq();
		}
        sendFrequency(rx);
        rx2 = rx;
      }

//  step freq change     
  buttonstate = digitalRead(A0);
  if(buttonstate == LOW) {
        setincrement();        
    };

  // Write the frequency to memory if not stored and 2 seconds have passed since the last frequency change.
    if(memstatus == 0){   
      if(timepassed+2000 < millis()){
        storeMEM();
        }
      }   

// LPF band switch relay	  
	  
	if(rx < 10000000){
		digitalWrite(BAND_HI, 0);
	    }
	if(rx > 10000000){
		digitalWrite(BAND_HI, 1);
		}
		  
///	  SERIAL COMMUNICATION - remote computer control for DDS - worked but not finishet yet - 1, 2, 3, 4 - worked 
   /*  check if data has been sent from the computer: */
  if (Serial.available()) {
    /* read the most recent byte */
    byteRead = Serial.read();
	if(byteRead == 49){     // 1 - up freq
		rx = rx + increment;
    Serial.println(rx);
		}
	if(byteRead == 50){		// 2 - down freq
		rx = rx - increment;
    Serial.println(rx);
		}
	if(byteRead == 51){		// 3 - up increment
		setincrement();
    Serial.println(increment);
		}

	if(byteRead == 52){		// 4 - print VFO state in serial console
		Serial.print("VFO_VERSION ");
                Serial.println(version);
		Serial.println(rx);
		Serial.println(rxif);
		Serial.println(increment);
		Serial.println(hertz);
		}

        if(byteRead == 53){		// 5 - scan freq from 7000 to 7050 and back to 7000
             for (int i=0; i=500; (i=i+100))
                rx = rx + i;
                sendFrequency(rx);
                Serial.println(rx);
                showFreq();
                delay(250);
                }
                
        if(byteRead == 48){		// 0 - Reset EEPROM memory
                reset_EEPROM();
	}
	}
}	  
/// END of main loop ///
/// ===================================================== END ============================================


/// START EXTERNAL FUNCTIONS

ISR(PCINT2_vect) {
  unsigned char result = r.process();
  if (result) {    
    if (result == DIR_CW){rx=rx+increment;}
    else {rx=rx-increment;};       
      if (rx >=160000000){rx=rx2;}; // UPPER VFO LIMIT 
      if (rx <=100000){rx=rx2;}; // LOWER VFO LIMIT
  }
}

// frequency calc from datasheet page 8 = <sys clock> * <frequency tuning word>/2^32
void sendFrequency(double frequency) {  
  int32_t freq = (frequency + rxif) * 4294967296./180000000;  // note 180 MHz clock on 9851. also note slight adjustment of this can be made to correct for frequency error of onboard crystal
    Serial.println(frequency);   // for serial console debuging
    Serial.println(frequency + rxif);
  for (int b=0; b<4; b++, freq>>=8) {
    tfr_byte(freq & 0xFF);
  }
  tfr_byte(0x001);   // Final control byte, LSB 1 to enable 6 x xtal multiplier on 9851 set to 0x000 for 9850
  pulseHigh(FQ_UD);  // Done!  Should see output
  

}

// transfers a byte, a bit at a time, LSB first to the 9851 via serial DATA line
void tfr_byte(byte data){
  for (int i=0; i<8; i++, data>>=1){
    digitalWrite(DATA, data & 0x01);
    pulseHigh(W_CLK);   //after each bit sent, CLK is pulsed high
  }
}

// step increments for rotary encoder button
void setincrement(){
  if(increment == 1){increment = 10; hertz = "10Hz"; hertzPosition=0;} 
  else if(increment == 10){increment = 50; hertz = "50Hz"; hertzPosition=0;}
  else if (increment == 50){increment = 100;  hertz = "100Hz"; hertzPosition=0;}
  else if (increment == 100){increment = 500; hertz="500Hz"; hertzPosition=0;}
  else if (increment == 500){increment = 1000; hertz="1Khz"; hertzPosition=0;}
  else if (increment == 1000){increment = 2500; hertz="2.5Khz"; hertzPosition=0;}
  else if (increment == 2500){increment = 5000; hertz="5Khz"; hertzPosition=0;}
  else if (increment == 5000){increment = 10000; hertz="10Khz"; hertzPosition=0;}
  else if (increment == 10000){increment = 100000; hertz="100Khz"; hertzPosition=0;}
  else if (increment == 100000){increment = 1000000; hertz="1Mhz"; hertzPosition=0;} 
  else{increment = 1; hertz = "1Hz"; hertzPosition=0;};  
  showFreq();
  delay(250); // Adjust this delay to speed up/slow down the button menu scroll speed.
};

// oled display functions
void showFreq(){
    millions = int(rx/1000000);
    hundredthousands = ((rx/100000)%10);
    tenthousands = ((rx/10000)%10);
    thousands = ((rx/1000)%10);
    hundreds = ((rx/100)%10);
    tens = ((rx/10)%10);
    ones = ((rx/1)%10);

	display.clearDisplay();	
	display.setCursor(0,0);
        display.print(millions);
        display.print(".");
        display.print(hundredthousands);
        display.print(tenthousands);
        display.print(thousands);
        display.print(".");
        display.print(hundreds);
        display.print(tens);
	display.println(ones);
	display.setCursor(0,18);
	if(rx < 10000000){
	  display.print("LSB ");
        }
        else{
          display.print("USB ");
        }
	display.println(hertz);
	display.display();

	timepassed = millis();
    memstatus = 0; // Trigger memory write
};

void storeMEM(){
  //Write each frequency section to a EPROM slot.  Yes, it's cheating but it works!
   EEPROM.write(0,BAND);
   EEPROM.write((BAND*7)+1,millions);
   EEPROM.write((BAND*7)+2,hundredthousands);
   EEPROM.write((BAND*7)+3,tenthousands);
   EEPROM.write((BAND*7)+4,thousands);
   EEPROM.write((BAND*7)+5,hundreds);       
   EEPROM.write((BAND*7)+6,tens);
   EEPROM.write((BAND*7)+7,ones);
   memstatus = 1;  // Let program know memory has been written
};


void checkBTNdecode(){

//  BAND CHANGE !!! band plan - change if need
  
BTNdecodeON = digitalRead(BTNDEC);
if(BTNdecodeON != BTNlaststate){
    if(BTNdecodeON == HIGH){
    delay(100);
    BTNcheck2 = 1;
    BAND = BAND + 1;
    if(BAND > 12){
       BAND = 1;
    }
    freq = String(EEPROM.read((BAND*7)+1))+String(EEPROM.read((BAND*7)+2))+String(EEPROM.read((BAND*7)+3))+String(EEPROM.read((BAND*7)+4))+String(EEPROM.read((BAND*7)+5))+String(EEPROM.read((BAND*7)+6))+String(EEPROM.read((BAND*7)+7));
    rx = freq.toInt(); 
}

if(BTNdecodeON == LOW){
    BTNcheck2 = 0;
	}
    BTNlaststate = BTNcheck2;
  }
}
