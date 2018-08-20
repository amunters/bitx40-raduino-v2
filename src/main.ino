#include <ClickEncoder.h>
#include <PinChangeInterrupt.h>
#include <TimerOne.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Clcd.h>
#include <si5351.h>

#define MY_CALLSIGN "SP7WKY"
#define MIN_FREQ 7000000UL
#define MAX_FREQ 7200000UL
#define BFOFREQ (11998800UL)

#define BFO_OFFSET_USB -2200

#define BB0(x) ((uint8_t)x)  // Bust int32 into Bytes
#define BB1(x) ((uint8_t)(x >> 8))
#define BB2(x) ((uint8_t)(x >> 16))

#define LCD_ADDR 0x3C  // I2C address of Si5351   (typical)

// If using 27mhz crystal, set XTAL=27000000, MSA=33.  Then vco=891mhz
#define SI5351BX_XTAL 25000000L  // Crystal freq in Hz
#define SI5351BX_CAL 16000L
#define BFO_CAL 0

// Inputs
#define PTT_SENSE 7
#define FBUTTON (A0)
#define FBUTTON2 (A1)
#define TXRX 5
#define CARRIER 6
#define ENC_BUTTON 9

ClickEncoder encoder(10, 11, ENC_BUTTON, 4, 1);
ClickEncoder encoder2(8, 12, -1, 4, 1);

DigitalButton button(FBUTTON);
DigitalButton button2(FBUTTON2);
DigitalButton ptt(PTT_SENSE);
Si5351 si5351;
hd44780_I2Clcd lcd(LCD_ADDR);

// Modes
#define LSB (0)
#define USB (1)

// Operation modes
#define NORMAL (0)
#define INTX (1)
#define SETTINGS (2)

bool inTx = false;
byte mode = LSB;                             // mode of the currently active VFO
unsigned long bfo_freq = BFOFREQ + BFO_CAL;  //  base bfo freq
unsigned long bfoFreqWithOffset = bfo_freq;  // actual

bool vfo_high = true;
int PBT_offset = 0;
int PBT_offset_old = 0;
bool carrierEnabled = 0;
bool txForceEnabled = 0;
bool forceRefresh = false;

unsigned long baseTune = 7100000UL;
int RXshift = 0;
unsigned long frequency =
    baseTune;  // the 'dial' frequency as shown on the display
int fine = 0;  // fine tune offset (Hz)
int freqMultip = 10;

void setFrequency() {
  if (vfo_high) {
    if (mode & 1)  // if we are in UPPER side band mode
      si5351.set_freq((bfoFreqWithOffset + frequency - RXshift + fine) * 100ULL,
                      SI5351_CLK2);
    else  // if we are in LOWER side band mode
      si5351.set_freq((bfoFreqWithOffset + frequency + RXshift + fine) * 100ULL,
                      SI5351_CLK2);
  } else {
    if (mode & 1)  // if we are in UPPER side band mode
      si5351.set_freq((bfoFreqWithOffset - frequency + RXshift - fine) * 100ULL,
                      SI5351_CLK2);
    else  // if we are in LOWER side band mode
      si5351.set_freq((bfoFreqWithOffset - frequency - RXshift - fine) * 100ULL,
                      SI5351_CLK2);
  }
}

void SetSideBand() {
  int pbtOffsetLocal = PBT_offset;
  if(inTx){
    pbtOffsetLocal = 0;
  }
  bfoFreqWithOffset = bfo_freq;
  if (vfo_high) {
    switch (mode) {
      case USB:
        bfoFreqWithOffset = bfoFreqWithOffset - pbtOffsetLocal;
        break;
      case LSB:
        bfoFreqWithOffset = bfoFreqWithOffset + BFO_OFFSET_USB + pbtOffsetLocal;
        break;
    }
  } else {
    switch (mode) {
      case LSB:
        bfoFreqWithOffset = bfoFreqWithOffset - pbtOffsetLocal;
        break;
      case USB:
        bfoFreqWithOffset = bfoFreqWithOffset + BFO_OFFSET_USB + pbtOffsetLocal;
        break;
    }
  }
  si5351.set_freq(bfoFreqWithOffset * 100ULL, SI5351_CLK0);
  setFrequency();
}

void passBandTuning() {
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
    forceRefresh = true;
    PBT_offset_old = PBT_offset;
  }
}

unsigned long old_freq;
int lastMode = -1;

void modeNormal() {
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
  if (frequency != old_freq) {
    setFrequency();
    forceRefresh = true;
    old_freq = frequency;
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
  if (forceRefresh) {
    forceRefresh = false;
    modeNormalView();
  }
}

void modeNormalView() {
  lcd.blink_on();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.print(" ");
  lcd.print(bfo_freq + PBT_offset);
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB             ");
  } else if (mode == USB) {
    lcd.print("USB             ");
  }
  if (carrierEnabled) {
    lcd.setCursor(12, 1);
    lcd.print("TUNE");
  }
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
  passBandTuning();
  if (lastMode != INTX) {
    forceRefresh = true;
  }
  lastMode = INTX;
  if (forceRefresh) {
    forceRefresh = false;
    modeInTXView();
  }
}

void modeInTXView() {
  lcd.blink_off();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.print("         ");
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB   TX        ");
  } else if (mode == USB) {
    lcd.print("USB   TX        ");
  }
  if (carrierEnabled) {
    lcd.setCursor(12, 1);
    lcd.print("TUNE");
  }
}

void checkTX() {
  int buttonState = ptt.getButton();
  if (buttonState == 0) {
    inTx = true;
  } else {
    inTx = false;
  }
}

void checkCarrier() {
  int buttonState = button.getButton();
  if(buttonState != 0){
    Serial.print("Button state: ");
    Serial.println(buttonState);
  }
  Serial.print("Button: ");
  Serial.println(buttonState);
  if (buttonState == 5) {
    carrierEnabled = !carrierEnabled;
    digitalWrite(CARRIER, carrierEnabled);
    digitalWrite(TXRX, carrierEnabled);
    forceRefresh = true;
  }
}

void setup() {
  Serial.begin(9600);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  int status;
  lcd.begin(16, 2);

  analogReference(DEFAULT);

  encoder.setAccelerationEnabled(true);
  encoder2.setAccelerationEnabled(true);
  old_freq = 0;

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

  digitalWrite(TXRX, 0);
  digitalWrite(CARRIER, 0);
}

void loop() {
  if (inTx) {
    modeInTX();
  } else {
    modeNormal();
  }
  checkTX();
  checkCarrier();
}

void timerIsr() {
  encoder.service();
  encoder2.service();
  button.service();
  button2.service();
  ptt.service();
}
