#include <ClickEncoder.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PinChangeInterrupt.h>
#include <TimerOne.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Clcd.h>
#include <si5351.h>
#include "BitxBoard.h"

// MAIN PROP
#define MY_CALLSIGN "SP7WKY"
#define MIN_FREQ 7000000UL
#define MAX_FREQ 7200000UL
#define BFOFREQ (11998800UL)

#define BFO_OFFSET_USB -2200

#define BB0(x) ((uint8_t)x)  // Bust int32 into Bytes
#define BB1(x) ((uint8_t)(x >> 8))
#define BB2(x) ((uint8_t)(x >> 16))

// LCD PROPS
#define LCD_ADDR 0x3C  // I2C address of Si5351   (typical)

// If using 27mhz crystal, set XTAL=27000000, MSA=33.  Then vco=891mhz
#define SI5351BX_XTAL 25000000L  // Crystal freq in Hz
#define SI5351BX_CAL 16000L
#define BFO_CAL 0

// Inputs
#define PTT_SENSE 7  // Checking if radio is transmiting
#define FBUTTON 4
#define FBUTTON2 3
#define TXRX 5
#define CARRIER 6
#define ENC_BUTTON 9
#define TEMP_SENSOR 2
#define PA_INPUT_RELAY 13
#define KEYBOARD A0
#define PTT_BUTTON A1  // Checking if PTT button is clicked
#define GENERIC_BUTTON A2

OneWire oneWire(TEMP_SENSOR);
DallasTemperature sensors(&oneWire);

// ENCODER
ClickEncoder encoder(10, 11, ENC_BUTTON, 4, 1);
ClickEncoder encoder2(8, 12, -1, 4, 1);
// BUTTONS
DigitalButton button(FBUTTON);
DigitalButton button2(FBUTTON2);
Si5351 si5351;
hd44780_I2Clcd lcd(LCD_ADDR);

// Modes
#define LSB (0)
#define USB (1)
#define LSB_H (2)
#define USB_H (3)

// Operation modes
#define NORMAL (0)
#define INTX (1)
#define SETTINGS (2)

bool inTx = false;
byte mode = LSB_H;
unsigned long bfo_freq = BFOFREQ + BFO_CAL;
unsigned long bfoFreqWithOffset = bfo_freq;
boolean paInputRelayState =
    1;  // HIGH - 1 power source, LOW - 2 power sources 12/24V

int PBT_offset = 0;
int PBT_offset_old = 0;
bool carrierEnabled = 0;
bool txForceEnabled = 0;
bool forceRefresh = false;
bool communicate = false;
bool debounceFlag = false;
unsigned int debounceTimer = 0;
unsigned int communicateTimer = 0;

unsigned long baseTune = 7100000UL;
int RXshift = 0;
unsigned long frequency =
    baseTune;  // the 'dial' frequency as shown on the display
int fine = 0;  // fine tune offset (Hz)
int freqMultip = 10;

// Temperature variables
float paTemp = 0.0;
int tempTimer = 0;
bool tempCheckedInCycle = false;

void resetComunicateView() {
  // Serial.println("resetComunicateView");

  communicate = false;
  communicateTimer = 0;
  forceRefresh = true;
}

void communicateView(String str1, String str2) {
  // Serial.println("communicateView");

  lcd.noBlink();
  lcd.setCursor(0, 0);
  lcd.print(str1);
  lcd.setCursor(0, 1);
  lcd.print(str2);
  communicateTimer = 0;
  communicate = true;
  forceRefresh = true;
}

void setFrequency() {
  // Serial.println("setFrequency");

  switch (mode) {
    case USB_H:
      si5351.set_freq((bfoFreqWithOffset + frequency - RXshift + fine) * 100ULL,
                      SI5351_CLK2);
      break;
    case LSB_H:
      si5351.set_freq((bfoFreqWithOffset + frequency + RXshift + fine) * 100ULL,
                      SI5351_CLK2);
      break;
    case LSB:
      si5351.set_freq((bfoFreqWithOffset - frequency - RXshift - fine) * 100ULL,
                      SI5351_CLK2);
      break;
    case USB:
      si5351.set_freq((bfoFreqWithOffset - frequency + RXshift - fine) * 100ULL,
                      SI5351_CLK2);
      break;
  }
}

void SetSideBand() {
  // Serial.println("SetSideBand");

  int pbtOffsetLocal = PBT_offset;
  if (inTx) {
    pbtOffsetLocal = 0;
  }
  bfoFreqWithOffset = bfo_freq;
  switch (mode) {
    case USB_H:
      bfoFreqWithOffset = bfoFreqWithOffset - pbtOffsetLocal;
      break;
    case LSB_H:
      bfoFreqWithOffset = bfoFreqWithOffset + BFO_OFFSET_USB + pbtOffsetLocal;
      break;
    case LSB:
      bfoFreqWithOffset = bfoFreqWithOffset - pbtOffsetLocal;
      break;
    case USB:
      bfoFreqWithOffset = bfoFreqWithOffset + BFO_OFFSET_USB + pbtOffsetLocal;
      break;
  }

  si5351.set_freq(bfoFreqWithOffset * 100ULL, SI5351_CLK0);
  setFrequency();
}

void passBandTuning() {
  // Serial.println("passBandTuning");

  int encoderValue = 0;
  encoderValue = encoder2.getValue();
  PBT_offset += encoderValue;
  if (PBT_offset > 4000) {
    PBT_offset = 4000;
  } else if (PBT_offset < -4000) {
    PBT_offset = -4000;
  }

  if (encoderValue != 0) {
    SetSideBand();
    communicateView("BFO Frequency:  ",
                    String(bfo_freq + PBT_offset) + "        ");
    forceRefresh = true;
    PBT_offset_old = PBT_offset;
  }
}

int lastMode = -1;

void modeNormal() {
  // Serial.println("modeNormal");

  long changeInFreq = encoder.getValue() * freqMultip;
  frequency += changeInFreq;
  if (frequency > MAX_FREQ) {
    frequency = MAX_FREQ;
  } else if (frequency < MIN_FREQ) {
    frequency = MIN_FREQ;
  }
  if (lastMode != NORMAL) {
    forceRefresh = true;
  }
  passBandTuning();
  if (changeInFreq != 0) {
    // Serial.println(changeInFreq);
    setFrequency();
    resetComunicateView();
    forceRefresh = true;
  }

  int buttonState = encoder.getButton();

  if (buttonState != 0) {
    switch (buttonState) {
      case ClickEncoder::Held:
        break;
      case ClickEncoder::Clicked:
        if (freqMultip <= 1) {
          freqMultip = 1;
        } else {
          freqMultip /= 10;
        }
        forceRefresh = true;
        break;

      case ClickEncoder::DoubleClicked:
        if (freqMultip >= 100) {
          freqMultip = 100;
        } else {
          freqMultip *= 10;
        }
        forceRefresh = true;
    }
  }
  lastMode = NORMAL;
  if (forceRefresh && !communicate) {
    forceRefresh = false;
    modeNormalView();
  }
}

void modeNormalView() {
  // Serial.println("modeNormalView");

  lcd.blink();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.print("         ");
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB_L ");
  } else if (mode == USB) {
    lcd.print("USB_L ");
  } else if (mode == LSB_H) {
    lcd.print("LSB_H ");
  } else if (mode == USB_H) {
    lcd.print("USB_H ");
  }
  if (paInputRelayState) {
    lcd.print("INT  ");
  } else {
    lcd.print("EXT  ");
  }
  lcd.print("    ");
  lcd.setCursor(12, 1);
  lcd.print(paTemp);
  switch (freqMultip) {
    case 1:
      lcd.setCursor(6, 0);
      break;
    case 10:
      lcd.setCursor(5, 0);
      break;
    case 100:
      lcd.setCursor(4, 0);
      break;
    case 1000:
      lcd.setCursor(3, 0);
      break;
    case 10000:
      lcd.setCursor(2, 0);
      break;
  }
}

void modeOptions() {}

void modeInTX() {
  // Serial.println("modeInTX");

  passBandTuning();
  if (lastMode != INTX) {
    forceRefresh = true;
  }
  lastMode = INTX;
  if (forceRefresh) {
    forceRefresh = false;
    communicate = false;
    communicateTimer = 0;
    modeInTXView();
  }
}

void modeInTXView() {
  // Serial.println("modeInTXView");

  // lcd.clear();
  lcd.noBlink();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.print("         ");
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB_L ");
  } else if (mode == USB) {
    lcd.print("USB_L ");
  } else if (mode == LSB_H) {
    lcd.print("LSB_H ");
  } else if (mode == USB_H) {
    lcd.print("USB_H ");
  }
  if (carrierEnabled) {
    lcd.print("TUNE");
  } else {
    
    lcd.print(" TX ");
  }
  lcd.setCursor(12, 1);
  lcd.print(paTemp);
}

void checkTX() {
  // Serial.println("checkTX");

  if (carrierEnabled || !digitalRead(PTT_BUTTON)) {
    digitalWrite(TXRX, HIGH);
  } else {
    digitalWrite(TXRX, LOW);
  }
  if (digitalRead(PTT_SENSE)) {
    inTx = true;
  } else {
    inTx = false;
  }
}

void checkCarrier() {
  // Serial.println("checkCarrier");
  int buttonState = button.getButton();
  if (buttonState == 5) {
    carrierEnabled = !carrierEnabled;
    digitalWrite(TXRX, carrierEnabled);
    if (carrierEnabled) {
      delay(100);
    }
    digitalWrite(CARRIER, carrierEnabled);
    forceRefresh = true;
  }
}

void changeMode(byte newMode) {
  // Serial.println("changeMode");
  mode = newMode;
  if (mode >= 4) {
    mode = 0;
  }
  if (mode == LSB) {
    communicateView("Mode:           ", "LSB     LOW VFO ");
  } else if (mode == USB) {
    communicateView("Mode:           ", "USB     LOW VFO ");
  } else if (mode == LSB_H) {
    communicateView("Mode:           ", "LSB     HIGH VFO");
  } else if (mode == USB_H) {
    communicateView("Mode:           ", "USB     HIGH VFO");
  }
  SetSideBand();
}

void changePAPowerSource(bool internal) {
  // Serial.println("changePAPowerSource");

  if (internal) {
    paInputRelayState = 1;
    digitalWrite(PA_INPUT_RELAY, paInputRelayState);
    communicateView("PA Power Source:", "INTERNAL 12V    ");
  } else {
    paInputRelayState = 0;
    digitalWrite(PA_INPUT_RELAY, paInputRelayState);
    communicateView("PA Power Source:", "EXTERNAL        ");
  }
  forceRefresh = true;
}

void checkModeChange() {
  // Serial.println("checkModeChange");

  int buttonState = button2.getButton();
  if (buttonState == 6) {
    changeMode(mode + 1);
  } else if (buttonState == 5) {
    changePAPowerSource(!paInputRelayState);
  }
}

void checkTemperature() {
  // Serial.println("checkTemperature()");
  if (tempTimer >= 5000 && !tempCheckedInCycle) {
    paTemp = sensors.getTempCByIndex(0);
    tempCheckedInCycle = true;
    if (carrierEnabled && paTemp > 70.0) {
      digitalWrite(CARRIER, LOW);
      digitalWrite(TXRX, LOW);
    }
    forceRefresh = true;
  }
  if (tempTimer >= 10000) {
    sensors.requestTemperatures();
    tempTimer = 0;
    tempCheckedInCycle = false;
  }
}

void setup() {
  Serial.begin(9600);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  lcd.begin(16, 2);

  analogReference(DEFAULT);

  encoder.setAccelerationEnabled(true);
  encoder2.setAccelerationEnabled(true);

  si5351.init(SI5351_CRYSTAL_LOAD_8PF, SI5351BX_XTAL, SI5351BX_CAL);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLB);

  si5351.output_enable(SI5351_CLK0, 1);
  si5351.output_enable(SI5351_CLK1, 0);
  si5351.output_enable(SI5351_CLK2, 1);

  SetSideBand();

  pinMode(PTT_SENSE, INPUT);
  pinMode(TXRX, OUTPUT);
  digitalWrite(TXRX, LOW);
  pinMode(CARRIER, OUTPUT);
  pinMode(FBUTTON, INPUT_PULLUP);
  pinMode(FBUTTON2, INPUT_PULLUP);
  pinMode(ENC_BUTTON, INPUT_PULLUP);
  pinMode(PA_INPUT_RELAY, OUTPUT);
  pinMode(PTT_BUTTON, INPUT_PULLUP);
  pinMode(GENERIC_BUTTON, INPUT_PULLUP);
  digitalWrite(PA_INPUT_RELAY, HIGH);

  digitalWrite(TXRX, 0);
  digitalWrite(CARRIER, 0);
  sensors.begin();
  sensors.setWaitForConversion(false);
}

void loop() {
  // Serial.println("loop");
  if (inTx) {
    modeInTX();
  } else {
    modeNormal();
  }
  checkTX();
  checkCarrier();
  checkModeChange();
  checkTemperature();
  if (debounceFlag) {
    checkKeyboard();
  }
}

char convertKeyboardKeyFromRead(int keyboardValue) {
  if (65 <= keyboardValue && keyboardValue <= 67) {  // 1
    return '1';
  } else if (60 <= keyboardValue && keyboardValue <= 63) {  // 2
    return '2';
  } else if (57 <= keyboardValue && keyboardValue <= 58) {  // 3
    return '3';
  } else if (95 <= keyboardValue && keyboardValue <= 100) {  // 4
    return '4';
  } else if (86 <= keyboardValue && keyboardValue <= 90) {  // 5
    return '5';
  } else if (79 <= keyboardValue && keyboardValue <= 83) {  // 6
    return '6';
  } else if (175 <= keyboardValue && keyboardValue <= 182) {  // 7
    return '7';
  } else if (148 <= keyboardValue && keyboardValue <= 153) {  // 8
    return '8';
  } else if (128 <= keyboardValue && keyboardValue <= 135) {  // 9
    return '9';
  } else if (505 <= keyboardValue && keyboardValue <= 515) {  // 0
    return '0';
  } else if (1008 <= keyboardValue && keyboardValue <= 1017) {  // *
    return '*';
  } else if (335 <= keyboardValue && keyboardValue <= 343) {  // #
    return '#';
  } else if (53 <= keyboardValue && keyboardValue <= 55) {  // A
    return 'A';
  } else if (73 <= keyboardValue && keyboardValue <= 77) {  // B
    return 'B';
  } else if (111 <= keyboardValue && keyboardValue <= 119) {  // C
    return 'C';
  } else if (250 <= keyboardValue && keyboardValue <= 260) {  // D
    return 'D';
  }
  return 0;
}

void checkKeyboard() {
  // Serial.println("checkKeyboard");

  char keyboardValue = convertKeyboardKeyFromRead(analogRead(KEYBOARD));
  char keyboardValue2 = convertKeyboardKeyFromRead(analogRead(KEYBOARD));
  char keyboardValue3 = convertKeyboardKeyFromRead(analogRead(KEYBOARD));

  if (keyboardValue == keyboardValue2 && keyboardValue2 == keyboardValue3 &&
      keyboardValue == '1') {  // 1
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '2') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '3') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '4') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '5') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '6') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '7') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '8') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '9') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '0') {
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '*') {
    debounceFlag = false;
    carrierEnabled = !carrierEnabled;
    digitalWrite(TXRX, carrierEnabled);
    if (carrierEnabled) {
      delay(100);
    }
    digitalWrite(CARRIER, carrierEnabled);
    forceRefresh = true;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == '#') {
    changePAPowerSource(!paInputRelayState);
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == 'A') {
    changeMode(LSB_H);
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == 'B') {
    changeMode(USB_H);
    SetSideBand();
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == 'C') {
    changeMode(LSB);
    debounceFlag = false;
    // Serial.println(keyboardValue);
  } else if (keyboardValue == keyboardValue2 &&
             keyboardValue2 == keyboardValue3 && keyboardValue == 'D') {
    changeMode(USB);
    debounceFlag = false;
    // Serial.println(keyboardValue);
  }
}

void timerIsr() {
  encoder.service();
  encoder2.service();
  button.service();
  button2.service();
  tempTimer++;
  if (communicate) {
    communicateTimer++;
  }
  if (!debounceFlag) {
    debounceTimer++;
    if (debounceTimer >= 200) {
      debounceFlag = true;
      debounceTimer = 0;
    }
  }
  if (communicateTimer >= 2000) {
    resetComunicateView();
  }
}
