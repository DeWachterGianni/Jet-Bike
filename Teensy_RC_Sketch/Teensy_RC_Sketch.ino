#include <PWMServo.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include <XPT2046_Touchscreen.h>

//  Tft pins
#define TFT_DC 9
#define TFT_CS 10
//  Input serial from ECU
#define edtSerial Serial1
//  Gereral input
#define thumbPin1 5 // Rocker swtich on jet bike controler
#define thumbPin2 4
#define throttleInputPin 23 // Pin for thumb throttle
#define DEG2RAD 0.0174532925

// My Veriables
const unsigned int MAX_MESSAGE_LENGTH = 16; //  Length of each line send by ECU
int EGT = 0; // External gas temperature
String TH = ""; //  This is send by ECU, can be 2 things, throttle applied in % or a message like, 'run-', 'rel-', 'glow'...
double RPM = 0; //  Engine RPM
double UB = 0.0; // Batt voltage

PWMServo RcSend; // PWM value to send to the ecu
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

int pwmRangeLow = 1000; //  Lowest the pwm can be
int pwmRangeHigh = 1000; // Highest the pwm can be
int pwmToSend = 1000; //  Pwm that is send to the ECU

int buttonTouchedId = -1;
int currentToggleSwitch = -1;
int lastToggleSwitch = -1;
int lastThrottlePerc = 0;
int prevThr;
int lastRPMDeg = -1;
int a = 10000; // Times the code has looped, this is used to not update the screen every time, nice and clear var name I must add ;) in UpdateRPM
int b = 10000;  // Times the code has looped, this is used to not update the screen every time, nice and clear var name I must add ;) in UpdateEGT
double lastEGT = -1;
int lastEGTDeg = -1;

double lastRPM = -1;

bool useThumbSwitch = true;
bool sendPwm = true;
bool forcePwmUpdate = false;

extern uint8_t epd_bitmap_gauge[];
extern uint8_t epd_bitmap_info[];

uint16_t blueIsh = tft.color565(1, 253, 242); //Yeah this is my blue-ish color --'

void drawCentreString(const String &buf, int x, int y, int color, int textSize)
{
    int16_t x1, y1;
    uint16_t w, h;
    tft.setTextSize(textSize);
    tft.setTextWrap(false);
    tft.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
    tft.setCursor(x - w / 2, y - h / 2);
    tft.print(buf);
}

class CustomButton{
  private:
    int x;
    int y;
    int w;
    int h;
    int color;
    String text;
    bool avail;
    bool visib;
    int textSize;
   
   public:CustomButton(int x, int y, int w, int h, int color, String text, bool avail, bool visib, int textSize){
      this->x = x;
      this->y = y;
      this->w = w;
      this->h = h;
      this->color = color;
      this->text = text;
      this->avail = avail;
      this->visib = visib;
      this->textSize = textSize;
    }

    public:void Draw(){
      if(!this->visib)
        return;
      
      tft.fillRect(this->x, this->y, this->w, this->h, ILI9341_BLACK);
      tft.drawRect(this->x, this->y, this->w, this->h, ILI9341_WHITE);
      int tX = this->x + this->w / 2;
      int tY = this->y + this->h / 2;
      drawCentreString(this->text, tX, tY, ILI9341_WHITE, this->textSize);
    }

    public:void SetVisible(bool visib){
      this->visib = visib;

      if(visib){
        this->avail = true;
        Draw();       
      }
      else
        blackout();
    }

    private:void blackout(){
      this->avail = false;
      tft.fillRect(this->x, this->y, this->w, this->h, ILI9341_BLACK);
    }

    public:void SetText(String text){
      if(this->text == text)
        return;
      
      this->text = text;
      UpdateButton();
    }

    //Dont redraw white line, just fill black, redraw text
    public:void UpdateButton(){
      if(!this->visib)
        return;
      
      tft.fillRect(this->x+2, this->y+2, this->w-4, this->h-4, ILI9341_BLACK);
      int tX = this->x + this->w / 2;
      int tY = this->y + this->h / 2;
      drawCentreString(this->text, tX, tY, ILI9341_WHITE, this->textSize);
    }
  
    public:void DrawTouched(){
      tft.fillRect(this->x, this->y, this->w, this->h, ILI9341_BLACK);
      tft.drawRect(this->x, this->y, this->w, this->h, ILI9341_GREEN);
      int tX = this->x + this->w / 2;
      int tY = this->y + this->h / 2;
      drawCentreString(this->text, tX, tY, ILI9341_GREEN, this->textSize);
    }

    public:boolean isTouched(int xP, int yP){
      //map press to acutal x y
      if(!this->avail)
        return false;
        
      if(xP >= this->x && xP <= this->x + this->w && yP >= this->y && yP <= this->y + this->h){       
        return true;
      }
      return false;
    }
};

CustomButton buttons[] = {
  CustomButton(110, 70, 100, 30, ILI9341_WHITE, "Go Manual", false, false, 1), //Not needed RN
  CustomButton(50, 15, 220, 40, ILI9341_WHITE, "", false, false, 1), //remove only for testing needed
  CustomButton(50, 160, 220, 40, ILI9341_WHITE, "- DATA UNAVAIL -", false, true, 2),
  CustomButton(50, 200, 220, 40, ILI9341_WHITE, "- DATA UNAVAIL -", false, true, 2)
};

void setup() {
  Serial.begin(9600);
  edtSerial.begin(9600);
  
  pinMode(thumbPin1, INPUT_PULLUP);
  pinMode(thumbPin2, INPUT_PULLUP);

  RcSend.attach(7, 1000, 2000);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);

  //Draw buttons
  for (int i=0; i<sizeof buttons/sizeof buttons[0]; i++) {
    buttons[i].Draw();
  }

  //Draw toggle state
  tft.drawRect(0, 15 + (75 * 0), 20, 60, ILI9341_GREEN);
  tft.drawRect(0, 15 + (75 * 1), 20, 60, ILI9341_ORANGE);
  tft.drawRect(0, 15 + (75 * 2), 20, 60, ILI9341_RED);

  //Throttle state
  tft.drawRect(320 - 20, 15, 20, 240 - 15 - 15, ILI9341_WHITE);

  
   //fillArc(100, 100, -90, 30, 1, 1, 50, ILI9341_WHITE);
   //tft.fillRect(50, 60, 100, 100, ILI9341_GREEN);
   tft.drawBitmap(40, 60, epd_bitmap_gauge, 115, 100, ILI9341_WHITE);
   tft.drawBitmap(165, 60, epd_bitmap_gauge, 115, 100, ILI9341_WHITE);
   tft.drawBitmap(40, 0, epd_bitmap_info, 240, 60, blueIsh);

   Serial.println("Start");
}

void loop() { 
  //Toggle switch state
  if(useThumbSwitch){
    if(!digitalRead(thumbPin1)){
        currentToggleSwitch = 0;       
    }else if(!digitalRead(thumbPin2)){
        currentToggleSwitch = 2;        
    }else {
        currentToggleSwitch = 1;     
    }

    //Toggle state is changed
    if(lastToggleSwitch != currentToggleSwitch || forcePwmUpdate){
      forcePwmUpdate = false;
      switch (currentToggleSwitch) {
        case 2:
          setPwmRange(1000, 1000);
          break;
        case 1:
          setPwmRange(1000, 1250);
          break;
        default:
          setPwmRange(1250, 2000);
          break;
      }
      drawToggleState(currentToggleSwitch, lastToggleSwitch);
      lastToggleSwitch = currentToggleSwitch;    
    }
  }

  //Throttle
  int thr = analogRead(throttleInputPin);

  //Try to ignore thr + 1 - 1 switching, happens because of noise I think? idk
  if(thr <= prevThr + 1 && thr >= prevThr - 1)
    thr = prevThr;
  else
    prevThr = thr;
  
  int thrPerc = map(thr, 260, 795, 0, 100); //Adjusting throttle percentage to the screen position needed to display
  thrPerc = max(thrPerc, 0);
  thrPerc = min(thrPerc, 100);
  
  drawThrottleState(thrPerc, lastThrottlePerc);
  lastThrottlePerc = thrPerc;
  
  if(useThumbSwitch){
    pwmToSend = map(thr, 260, 795, pwmRangeLow, pwmRangeHigh);
    pwmToSend = max(pwmToSend, pwmRangeLow);
    pwmToSend = min(pwmToSend, pwmRangeHigh);
  }else{    
    //Use buttons to adjust - maybe in future?
  }

  //Sending out pwm
  if(sendPwm){
    RcSend.write(map(pwmToSend, 1000, 2000, 0, 179)); // 0-179 is the number received from the thumb throttle input I think? idk
  }else{
    RcSend.write(0); // Failsafe not in use ofcours haha, but hey it's there
  }
    getEDTData();   
    updateRPM();
    updateEGT();
    
    //Button 1 is not in use now so this is useless, leaving it there just in case
    String pwmInfo = "L: " + String(pwmRangeLow) + ", H: " + String(pwmRangeHigh) + ", C: " + String(pwmToSend);
    drawInfo(1, pwmInfo);
}

void getEDTData(){
    while (edtSerial.available() > 0)
    {
      //Create a place to hold the incoming message
      static char message[MAX_MESSAGE_LENGTH];
      static unsigned int message_pos = 0;
  
      //Read the next available byte in the serial receive buffer
      char inByte = edtSerial.read();

      //Just for debugging
      //Serial.println(inByte);
      
      //Message coming in (check not terminating character) and guard for over message size
      if ((message_pos <= MAX_MESSAGE_LENGTH - 1) && inByte != 27 && inByte != 128 && inByte != 192 && inByte != 255) //  If the line send by the ECU contains one of these bytes it is NOT a propper message
      {
        //Add the incoming byte to our message
        message[message_pos] = inByte;
        message_pos++;
      }
      //Full message received...
      else
      {
         //Add null character to string
         message[message_pos] = '\0';     
         //Serial.println(message);
  
         //Decode message
         //Array to string to use substring
         String temp;
         temp = message;
  
          if(temp.length() > 8){
             String s = "";
             //  Display data from the first line from the screen
             if(temp.substring(0, 3) == "EGT"){ // This is explained by a picture in the repo under 'Messages possible by the ECU' in the wiki
                EGT = temp.substring(3, 6).toInt(); 
                TH = temp.substring(12, 16); //The screen (EDT engine data terminal ) has 16 chars per line, and has 2 lines 
                Serial.println(TH);
                 if (TH == "lock") 
                    s = "Remote Locked";
                 else if(TH == "stop")
                    s = "Engine Off";
                 else if(TH == "run-")
                    s = "Continue";
                 else if(TH == "rel-" || TH == "rel") //Sometimes is rel, this is a fault from the manufacturer I think haha 
                    s = "Engine Armed";
                 else if(TH == "glow")
                    s = "Preheat Igniter";
                 else if(TH == "spin")
                    s = "Spin-up";
                 else if(TH == "heat")
                    s = "Engine Heating";
                 else if(TH == "acce")
                    s = "Engine Accel";
                 else if(TH == "cal.")
                    s = "Cal Fuel System";
                 else if(TH == "idle")
                    s = "Engine Available";
                 else if(TH == "-off")
                    s = "- Trim shut down";
                 else if(TH == "cool")
                    s = "Cool-down";
                 else
                    s = "Unrec: " + temp.substring(12, 15); // To find faults in the reading
                             
                drawInfo(2, s);
             }else if(temp.substring(5, 8) == "rpm"){ //  Display data from the second line from the screen
                RPM = temp.substring(0, 5).toFloat();
                UB = temp.substring(11, 15).toFloat();
                s = "Batt Volt: " + String(UB) + "V";
                drawInfo(3, s);
             }else{
              //Data comming from another screen, Not implemented yet
             }
          }
         //Reset for the next message
         message_pos = 0;
  
         return;
       }
   }
}

//  What if a certain button is pressed
void onReleaseButton(int id){
  //Manual | throttle mode
  if(id == 0){
    useThumbSwitch = !useThumbSwitch; 
    if(useThumbSwitch){
      drawInfo(0, "Go Manual");
    }else{
      drawInfo(0, "Go Throttle");
      setPwmRange(1000, 2000);
      forcePwmUpdate = true;
    }
  }
}

void drawInfo(int buttonId, String text){
  buttons[buttonId].SetText(text);
}

void drawThrottleState(int thPerc, int lastThPerc){
  if(thPerc < lastThPerc){
    //Remove white
    int p = 100 - thPerc;
    int y = map(p, 0, 100, 16, 208 - 2);
    tft.fillRect(286 + 15, 16, 18, y, ILI9341_BLACK);
  }else if(thPerc > lastThPerc){
    //Add white
    int y = map(thPerc, 0, 100, 208, 16);
    tft.fillRect(286 + 15, y, 18, 208 - y + 16, ILI9341_WHITE);
  }
}

void drawToggleState(int current, int last){
  //Fill last with black
  tft.fillRect(0, 15 + (75 * last), 20, 60, ILI9341_BLACK);

  int color;
  switch(last) {
    case 0:
      color = ILI9341_GREEN;
      break;
    case 1:
      color = ILI9341_ORANGE;
      break;
    case 2:
      color = ILI9341_RED;
      break; 
  }

  tft.drawRect(0, 15 + (75 * last), 20, 60, color);
  
  switch(current) {
    case 0:
      color = ILI9341_GREEN;
      break;
    case 1:
      color = ILI9341_ORANGE;
      break;
    case 2:
      color = ILI9341_RED;
      break; 
  }
  
 tft.fillRect(0, 15 + (75 * current), 20, 60, color);
}

void setPwmRange(int low, int high){
  pwmRangeLow = low;
  pwmRangeHigh = high;
}

void fillArc(int x, int y, int start_angle, int seg_count, int w, unsigned int colour)
{
  int rx = 1;
  int ry = 1;
  byte seg = 3; // Segments are 3 degrees wide = 120 segments for 360 degrees
  byte inc = 3; // Draw segments every 3 degrees, increase to 6 for segmented ring

    // Calculate first pair of coordinates for segment start
    float sx = cos((start_angle - 90) * DEG2RAD);
    float sy = sin((start_angle - 90) * DEG2RAD);
    uint16_t x0 = sx * (rx - w) + x;
    uint16_t y0 = sy * (ry - w) + y;
    uint16_t x1 = sx * rx + x;
    uint16_t y1 = sy * ry + y;

  // Draw colour blocks every inc degrees
  for (int i = start_angle; i < start_angle + seg * seg_count; i += inc) {

    // Calculate pair of coordinates for segment end
    float sx2 = cos((i + seg - 90) * DEG2RAD);
    float sy2 = sin((i + seg - 90) * DEG2RAD);
    int x2 = sx2 * (rx - w) + x;
    int y2 = sy2 * (ry - w) + y;
    int x3 = sx2 * rx + x;
    int y3 = sy2 * ry + y;

    tft.fillTriangle(x0, y0, x1, y1, x2, y2, colour);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, colour);

    // Copy segment end to sgement start for next segment
    x0 = x2;
    y0 = y2;
    x1 = x3;
    y1 = y3;
  }
}

void updateRPM() {
  a++;
 
  if(lastRPM == RPM)
    return;

  if(a > 10){
    //Fill in black to redraw string
    tft.fillRect(85, 67, 68, 35, ILI9341_BLACK);
    tft.setCursor(90, 76);
    tft.setTextSize(2);
    if(RPM < 99.9)
      tft.print(" " + String(RPM).substring(0, String(RPM).length() - 1));
    else
      tft.print(String(RPM).substring(0, String(RPM).length() - 1));

    a = 0;
  }


  int deg = map(RPM, 0, 140, 0, 208);
  float sx = 44 * cos(deg * DEG2RAD);
  float sy = 44 * sin(deg * DEG2RAD);

  if(RPM > lastRPM){
    //ADD
    for(int i = lastRPMDeg; i <= deg; i++){
      sx = 44 * cos(i * DEG2RAD);
      sy = 44 * sin(i * DEG2RAD);

      if(i <= 178)
        tft.fillCircle(90 + sx, 110 + sy, 2, ILI9341_GREEN);
      else if(i < 185)
        tft.fillCircle(90 + sx, 110 + sy, 2, ILI9341_ORANGE);
      else
        tft.fillCircle(90 + sx, 110 + sy, 2, ILI9341_RED);
    }
  }else{
    for(int i = lastRPMDeg; i >= deg; i--){
      sx = 44 * cos(i * DEG2RAD);
      sy = 44 * sin(i * DEG2RAD);
      tft.fillCircle(90 + sx, 110 + sy, 2, ILI9341_BLACK);
    }
  }
  
  lastRPMDeg = deg;
  tft.setTextSize(1);
  lastRPM = RPM;
}

void updateEGT(){
  b++;
  
  if(lastEGT == EGT)
    return;

  if(b > 10){
    //Fill in black to redraw string
    tft.fillRect(85 + 125, 67, 68, 35, ILI9341_BLACK);
    tft.setCursor(90 + 125, 76);
    tft.setTextSize(2);
    if(EGT < 100)
      tft.print(" " + String(EGT) + char(247) + "C"); //Or 9 as °
    else
      tft.print(String(EGT)+ char(247) + "C"); //Or 9 as °

    b = 0;
  }


  int deg = map(EGT, 0, 860, 0, 208);
  float sx = 44 * cos(deg * DEG2RAD);
  float sy = 44 * sin(deg * DEG2RAD);

  if(EGT > lastEGT){
    //ADD
    for(int i = lastEGTDeg; i <= deg; i++){
      sx = 44 * cos(i * DEG2RAD);
      sy = 44 * sin(i * DEG2RAD);
      int o = map(i, 0, 208, 0, 860);
      if(o < 790)
        tft.fillCircle(90 + sx + 125, 110 + sy, 2, ILI9341_GREEN);
      else if(o < 840)
        tft.fillCircle(90 + sx + 125, 110 + sy, 2, ILI9341_ORANGE);
      else
        tft.fillCircle(90 + sx + 125, 110 + sy, 2, ILI9341_RED);
    }
  }else{
    for(int i = lastEGTDeg; i >= deg; i--){
      sx = 44 * cos(i * DEG2RAD);
      sy = 44 * sin(i * DEG2RAD);
      tft.fillCircle(90 + sx + 125, 110 + sy, 2, ILI9341_BLACK);
    }
  }
  
  lastEGTDeg = deg;
  tft.setTextSize(1);
  lastEGT = EGT;
}

// x,y == coords of centre of arc
// start_angle = 0 - 359
// seg_count = number of 3 degree segments to draw (120 => 360 degree arc)
// rx = x axis radius
// yx = y axis radius
// w  = width (thickness) of arc in pixels
// colour = 16 bit colour value
// Note if rx and ry are the same an arc of a circle is drawn
//int x, int y, int start_angle, int seg_count, int rx, int ry, int w, unsigned int colour
