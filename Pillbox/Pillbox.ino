#include <SPI.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiSoftSpi.h"
#include "BluefruitConfig.h"
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for SSD1306 display connected using software SPI (default case):
#define OLED_DATA   9
#define OLED_CLK   10
#define OLED_DC    11
#define OLED_CS    12
#define OLED_RESET 13

#define BUTTON1LED A1
#define BUTTON1PIN 15

#define BUTTON2LED A4
#define BUTTON2PIN A5

#define BUTTON3LED A2
#define BUTTON3PIN 3

//battery pin
#define VBATPIN A0

   
SSD1306AsciiSoftSpi oled;

//BLE Object
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

#define FACTORYRESET_ENABLE      1

//variable to hold timer for device going to sleep
long batteryUpdate; 
long activityTimer;

//Pill information
typedef struct {
  bool timeToTake = false; //Is it time for user to take pill
  bool flag = false; //This flag is to show if pill struc is being used
  bool displayState = false; //Whether to display pill info
  uint8_t amount; //How many pills to take per dosage;
  long displayTimer;//Timer for pill info displlay
  long pillTimer;
  String pillName; //Pill Name
} Pill;

Pill pills[3];

//System state variable

enum State{Standby, Asleep, Alarming};
enum State s_state; 


// This Button library code detects single clicks,double clicks,and holds
// from :  https://forum.arduino.cc/index.php?topic=14479.0
//=================================================
//  MULTI-CLICK:  One Button, Multiple Events
 

//Need 3 instances of butotn variables for each button
typedef struct
{ 
  // Button variables
  boolean buttonVal = HIGH;   // value read from button
  boolean buttonLast = HIGH;  // buffered value of the button's previous state
  boolean DCwaiting = false;  // whether we're waiting for a double click (down)
  boolean DConUp = false;     // whether to register a double click on next release, or whether to wait and click
  boolean singleOK = true;    // whether it's OK to do a single click
  long downTime = -1;         // time the button was pressed down
  long upTime = -1;           // time the button was released
  boolean ignoreUp = false;   // whether to ignore the button release because the click+hold was triggered
  boolean waitForUp = false;        // when held, whether to wait for the up event
  boolean holdEventPast = false;    // whether or not the hold event happened already
  boolean longHoldEventPast = false;// whether or not the long hold event happened already
}Button;

Button button[3];

uint8_t checkButton(uint8_t buttonPin , uint8_t buttonNum) {    
  uint8_t debounce = 20;          // ms debounce period to prevent flickering when pressing or releasing the button
  int DCgap = 250;            // max ms between clicks for a double click event
    int holdTime = 1000;        // ms hold period: how long to wait for press+hold event
    int longHoldTime = 3000;    // ms long hold period: how long to wait for press+hold event
   uint8_t event = 0;
   button[buttonNum].buttonVal = digitalRead(buttonPin);
   // Button pressed down
   if (button[buttonNum].buttonVal == LOW && button[buttonNum].buttonLast == HIGH && (millis() - button[buttonNum].upTime) > debounce)
   {
       //Serial.println("test");
       button[buttonNum].downTime = millis();
       button[buttonNum].ignoreUp = false;
       button[buttonNum].waitForUp = false;
       button[buttonNum].singleOK = true;
       button[buttonNum].holdEventPast = false;
       button[buttonNum].longHoldEventPast = false;
       if ((millis()-button[buttonNum].upTime) < DCgap && button[buttonNum].DConUp == false && button[buttonNum].DCwaiting == true)  button[buttonNum].DConUp = true;
       else  button[buttonNum].DConUp = false;
       button[buttonNum].DCwaiting = false;
   }
   // Button released
   else if (button[buttonNum].buttonVal == HIGH && button[buttonNum].buttonLast == LOW && (millis() - button[buttonNum].downTime) > debounce)
   {        
       if (not button[buttonNum].ignoreUp)
       {
           button[buttonNum].upTime = millis();
           if (button[buttonNum].DConUp == false) button[buttonNum].DCwaiting = true;
           else
           {
               event = 2;
               button[buttonNum].DConUp = false;
               button[buttonNum].DCwaiting = false;
               button[buttonNum].singleOK = false;
           }
       }
   }
   // Test for normal click event: DCgap expired
   if ( button[buttonNum].buttonVal == HIGH && (millis()-button[buttonNum].upTime) >= DCgap && button[buttonNum].DCwaiting == true && button[buttonNum].DConUp == false && button[buttonNum].singleOK == true && event != 2)
   {
       event = 1;
       button[buttonNum].DCwaiting = false;
   }
   // Test for hold
   if (button[buttonNum].buttonVal == LOW && (millis() - button[buttonNum].downTime) >= holdTime) {
       // Trigger "normal" hold
       if (not button[buttonNum].holdEventPast)
       {
           event = 3;
           button[buttonNum].waitForUp = true;
           button[buttonNum].ignoreUp = true;
           button[buttonNum].DConUp = false;
           button[buttonNum].DCwaiting = false;
           //downTime = millis();
           button[buttonNum].holdEventPast = true;
       }
       // Trigger "long" hold
       if ((millis() - button[buttonNum].downTime) >= longHoldTime)
       {
           if (not button[buttonNum].longHoldEventPast)
           {
               event = 4;
               button[buttonNum].longHoldEventPast = true;
           }
       }
   }
   button[buttonNum].buttonLast = button[buttonNum].buttonVal;
   return event;
}

void BeginAlarm(uint8_t pillNum)
{
  s_state = Alarming;
  pills[pillNum].timeToTake = true;
  pills[pillNum].pillTimer = millis();
  DisplayPill(pillNum + 1 , true);
  
}

void FailureToTake(uint8_t pillNum)
{
  //send ble message to 
    //ble.print("AT+BLEUARTTX=");
    //ble.println("3");
    SleepDevice();
}

void TurnOffAlarm(uint8_t pillNum)
{
  //change state back to standby and reset alarm variables
  pills[pillNum].timeToTake = false;
  //This is the only blocking wait in prgram to prevent state change
      oled.clear();
      oled.set2X();
      oled.setCursor(32,12);
      oled.println("THANKS!");
      delay(2000);
      oled.clear();                 
  
  WakeUp();
  //ble.print("AT+BLEUARTTX=");
  //ble.println("3");
  //SEND BLE confirmation here
  
}
//******************************************************
//        This is a String Library function 
//        implemented from :
//        https://arduino.stackexchange.com/questions/1013/how-do-i-split-an-incoming-string
//******************************************************

String getValue(String data, char separator, uint8_t index)
{
    uint8_t found = 0;
    int strIndex[] = { 0, -1 };
    uint8_t maxIndex = data.length() - 1;

    for (uint8_t i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

//******************************************************
//        This is the Button input Handler
//        - int button action , int button pin
//        - based on current s_state determines action
//******************************************************

void InputButton(uint8_t action , uint8_t b_pin)
{
  // First check system state
  switch(s_state)
  {
    case Standby :
      if(action == 1)
      {
        DisplayPill(b_pin ,false);
        activityTimer = millis();
      }
      else if(action == 4)
      {
        BeginAlarm(b_pin - 1);
        activityTimer = millis();
      }
      break;    
    case Asleep :
      WakeUp();
      //DisplayPill(b_pin ,false);
      break;
    case Alarming :
      if(action == 2)
      {
        TurnOffAlarm(b_pin-1);
      }
      break;
  }
}

//******************************************************
//        This is the BLE input Handler
//        - String BLE.buffer() input
//        - parses string and determines action+values
//******************************************************

void InputBLE(String inputstream)
{
  //input parseing
  uint8_t action      = (getValue(inputstream , ',' , 0)).toInt();
  uint8_t pillNum     = (getValue(inputstream , ',' , 1)).toInt();
  String pillName = (getValue(inputstream , ',' , 2));
  uint8_t pillAmnt    = (getValue(inputstream , ',' , 3)).toInt();

  switch(action)
  {
    case 0 :  
      //failed message
      break;
    case 1 :
      //pill addition
      addPill(pillNum , pillName , pillAmnt);
      break;
    case 2 :
      deletePill(pillNum);
    case 3 : 
      //pill taking time!!!!!
    case 4 :
      break;  
  }   
        
}

//******************************************************
//        This is the deletePill function
//        
//******************************************************

void deletePill(uint8_t pillNum)
{
  pills[pillNum - 1].pillName = "";
  pills[pillNum - 1].amount = 0;
  pills[pillNum - 1].flag = false;
}

//******************************************************
//        This is the addPill function
//        
//******************************************************

void addPill(uint8_t pillNum , String pillName , uint8_t pillAmnt)
{
  pills[pillNum - 1].pillName = pillName;
  pills[pillNum - 1].amount = pillAmnt;
  pills[pillNum - 1].flag = true;
}

//******************************************************
//        This is the Pill Info Display function
//        - Int display# input
//******************************************************

void DisplayPill(uint8_t displayNum , bool TimeToTake)
{
      oled.clear();
      oled.setFont(System5x7);
      if(TimeToTake)
      {
         oled.set1X();//font size
         oled.setCursor(22,12);
         oled.print("  TAKE ");
         oled.print(pills[displayNum - 1].amount);
         oled.print(" ");
         oled.print(pills[displayNum - 1].pillName);
         oled.println("!");
         oled.println("     ******************");
      }
      else
      {
        oled.set2X();//font size
        oled.setCursor(12,12);
        oled.println(pills[displayNum - 1].pillName);
      }
      pills[displayNum - 1].displayTimer = millis();
      pills[displayNum - 1].displayState = true;
}


void WakeUp()
{
  Serial.println("awake");
  s_state = Standby;

  batteryUpdate = millis();
  activityTimer = millis();

  DisplayBattery();
  digitalWrite(BUTTON1LED, HIGH);
  digitalWrite(BUTTON2LED, HIGH);
  digitalWrite(BUTTON3LED, HIGH);
  
}

//******************************************************
//          This is a test sleep function
//          - Turns off OLED's & LED's
//          - TODO: Lowpower mode(not breaking board)
//******************************************************

void SleepDevice()
{
  s_state = Asleep;

  //Turn off displays to lower power
  oled.clear();

  //Turn off Led's
  digitalWrite(BUTTON1LED, LOW);
  digitalWrite(BUTTON2LED, LOW);
  digitalWrite(BUTTON3LED, LOW);

  //Todo: Not screw up the low power mode/??????
  //LowPower.powerDown(SLEEP_8S, ADC_OFF , BOD_OFF);
}


//*****************************************************
//        This func measures devices battery voltage
//        - w/voltage divider measured from vbat pin
//        - Then displays battery on display3
//*****************************************************

void DisplayBattery()
{
  //delay(2000);
  oled.clear();
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  oled.set1X();
  //oled.setCursor(0,0);
  //oled.println(measuredvbat);
  
  oled.setCursor(99 ,0);
  oled.print((int)min(((measuredvbat - 3.2)/(4.27 -3.2) * 100), 100));
  oled.println("%");
  oled.println();
  oled.setCursor(99, 26);
  //oled.println("BLE >})");
  
}


//**************************************************
//        This is the setup function for setting up
//        - I/O , BLE , Displays , Timers
//**************************************************

void setup() {
  // put your setup code here, to run once:

  

  //pinmod

 Serial.begin(9600);
 delay(5000);

 pinMode(BUTTON1LED , OUTPUT);
 pinMode(BUTTON1PIN , INPUT_PULLUP);
 pinMode(BUTTON2LED , OUTPUT);
 pinMode(BUTTON2PIN , INPUT_PULLUP);
 pinMode(BUTTON3LED , OUTPUT);
 pinMode(BUTTON3PIN , INPUT_PULLUP);

 //turn on button led's
 digitalWrite(BUTTON1LED, HIGH);
 digitalWrite(BUTTON2LED, HIGH);
 digitalWrite(BUTTON3LED, HIGH);

  oled.begin(&Adafruit128x32, OLED_CS, OLED_DC, OLED_CLK, OLED_DATA, OLED_RESET);
  oled.setFont(Adafruit5x7);  
  oled.set1X();
  oled.clear();
  oled.println("Hello world!");
  
  batteryUpdate = millis();
  activityTimer = millis();
  DisplayBattery();
  s_state = Standby;
  //test add pills
  InputBLE(String("1,1,Tylenol,2"));
  InputBLE(String("1,2,Lipitor,2"));
  InputBLE(String("1,3,Zoloft,2"));

  //initallize BLE
  
if ( !ble.begin(VERBOSE_MODE) )
  {
    Serial.println(("Couldn't find Bluefruit"));
  }
  Serial.println(("OK!") );

    Serial.print(("Initialising"));
  
  if ( FACTORYRESET_ENABLE )
  {

    Serial.println(("Performing a factory reset: "));
    if ( ! ble.factoryReset() ){ 
      delay(200);
      //try again
      if(!ble.factoryReset()){
        Serial.println(("Couldn't factory reset"));
      }
    }
  }

  ble.sendCommandCheckOK(( "AT+GAPDEVNAME=PillBox" ));

  ble.echo(false);

 
  ble.info();

  ble.verbose(false);
  

  while (! ble.isConnected()) {
  }

  oled.clear();
  oled.println("Conected");
}

void loop() {

  float timeLoop = millis();
  
  //Standy State
  if(s_state == Standby)
  {  // Current time
    //check if device is ready to "sleep"
    if((timeLoop - activityTimer) > 30000)
    {
      SleepDevice();
      //batteryUpdate = millis();
    } 
    //check for pill info display timer
    for(uint8_t i = 0; i < 2; i++)
    {
      if(pills[i].displayState && (timeLoop - pills[i].displayTimer > 5000))
      {
         oled.clear();
         pills[i].displayState = false;
         DisplayBattery();
      }
    }
    //check to update battery
    if((timeLoop - batteryUpdate) > 10000  && !pills[0].displayState)
    {
      DisplayBattery();
      batteryUpdate = millis();
    }
  }


  //IT IS TIME TO TAKE A PILL
  //********************8*****
  if(s_state == Alarming)
  {
    for(uint8_t i = 0; i < 3; i++)
    {
      if(pills[i].timeToTake)
      {
        if(timeLoop - pills[i].pillTimer > 600000)
        {
          //failed to take pills
          FailureToTake(i);
          
        }
        else
        {
          if(timeLoop - pills[i].displayTimer > 200 && timeLoop - pills[i].displayTimer < 400)
          {
            switch(i)
            {
              case 0 :
                 digitalWrite(BUTTON1LED, HIGH);
                 break;
              case 1 :
                digitalWrite(BUTTON2LED, HIGH); 
                break;  
              case 2 :
                digitalWrite(BUTTON3LED , HIGH);  
            }
          }
          else if(timeLoop - pills[i].displayTimer > 400)
          {
            digitalWrite(BUTTON1LED, LOW); 
            digitalWrite(BUTTON2LED, LOW);
            pills[i].displayTimer = timeLoop;
          }
        }
      }
    }
  }

  //Button readings
  uint8_t Button_1_Reading = checkButton(BUTTON1PIN , 0);
  if(Button_1_Reading)
  {  
      InputButton(Button_1_Reading , 1);
  }
  uint8_t Button_2_Reading = checkButton(BUTTON2PIN , 1);
  if(Button_2_Reading)
  {  
     InputButton(Button_2_Reading , 2);
  }

  uint8_t Button_3_Reading = checkButton(BUTTON3PIN , 2);
  if(Button_3_Reading)
  {  
     InputButton(Button_3_Reading , 3);
  }

  // Check for incoming characters from Bluefruit
  
  ble.println("AT+BLEUARTRX");
  ble.readline();
  String bleBuff = ble.buffer();
  if(bleBuff.length() > 2)
  {
     InputBLE(bleBuff);
  }
  
}
