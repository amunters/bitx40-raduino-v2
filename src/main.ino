#include <ClickEncoder.h>
#include <LiquidCrystal.h>
#include <PinChangeInterrupt.h>  // https://github.com/NicoHood/PinChangeInterrupt
#include <TimerOne.h>
#include <Wire.h>

#define MY_CALLSIGN "SP7WKY"
#define MIN_FREQ 7000000UL
#define MAX_FREQ 7200000UL
#define BFOFREQ (11998800UL)

#define BFO_OFFSET_USB -2200

#define BB0(x) ((uint8_t)x)  // Bust int32 into Bytes
#define BB1(x) ((uint8_t)(x >> 8))
#define BB2(x) ((uint8_t)(x >> 16))

#define SI5351BX_ADDR 0x60  // I2C address of Si5351   (typical)
#define SI5351BX_XTALPF 2   // 1:6pf  2:8pf  3:10pf

// If using 27mhz crystal, set XTAL=27000000, MSA=33.  Then vco=891mhz
#define SI5351BX_XTAL 25003900  // Crystal freq in Hz
#define SI5351BX_MSA 35         // VCOA is at 25mhz*35 = 875mhz

// User program may have reason to poke new values into these 3 RAM variables
uint32_t si5351bx_vcoa = (SI5351BX_XTAL * SI5351BX_MSA);  // 25mhzXtal calibrate
uint8_t si5351bx_rdiv = 0;              // 0-7, CLK pin sees fout/(2**rdiv)
uint8_t si5351bx_drive[3] = {1, 1, 1};  // 0=2ma 1=4ma 2=6ma 3=8ma for CLK 0,1,2
uint8_t si5351bx_clken = 0xFF;          // Private, all CLK output drivers off

// Inputs
#define PTT_SENSE (A0)
#define PBT (A6)
#define FBUTTON (A2)
#define FBUTTON2 (A3)
#define TXRX 7
#define CARRIER 6

#define pinA 4
#define pinB 3
#define pinSw 2  // switch
#define STEPS 4

ClickEncoder encoder(pinA, pinB, pinSw, STEPS);
DigitalButton button(FBUTTON);
DigitalButton button2(FBUTTON2);
DigitalButton ptt(PTT_SENSE);

LiquidCrystal lcd(8, 9, 10, 11, 12, 13);

// Modes
#define LSB (0)
#define USB (1)

// Operation modes
#define NORMAL (0)
#define INTX (1)
#define SETTINGS (2)

bool inTx = false;
byte mode = LSB;                   // mode of the currently active VFO
unsigned long bfo_freq = BFOFREQ;  // the frequency (Hz) of the BFO
bool vfo_high = true;
int PBT_offset = 0;
int PBT_offset_old = 0;
bool carrierEnabled = 0;
bool forceRefresh = false;

unsigned long baseTune = 7100000UL;
int RXshift = 0;
unsigned long frequency;  // the 'dial' frequency as shown on the display
int fine = 0;             // fine tune offset (Hz)
int freqMultip = 10;

void si5351bx_init() {  // Call once at power-up, start PLLA
  // uint8_t reg;
  uint32_t msxp1;
  Wire.begin();
  i2cWrite(149, 0);             // SpreadSpectrum off
  i2cWrite(3, si5351bx_clken);  // Disable all CLK output drivers
  i2cWrite(183,
           ((SI5351BX_XTALPF << 6) |
            0x12));  // Set 25mhz crystal load capacitance (tks Daniel KB3MUN)
  msxp1 = 128 * SI5351BX_MSA - 512;  // and msxp2=0, msxp3=1, not fractional
  uint8_t vals[8] = {0, 1, BB2(msxp1), BB1(msxp1), BB0(msxp1), 0, 0, 0};
  i2cWriten(26, vals, 8);  // Write to 8 PLLA msynth regs
  i2cWrite(177, 0x20);     // Reset PLLA  (0x80 resets PLLB)
  // for (reg=16; reg<=23; reg++) i2cWrite(reg, 0x80);    // Powerdown CLK's
  // i2cWrite(187, 0);                  // No fannout of clkin, xtal, ms0, ms4
}

void si5351bx_setfreq(uint8_t clknum, uint32_t fout) {  // Set a CLK to fout Hz
  uint32_t msa, msb, msc, msxp1, msxp2, msxp3p2top;
  if ((fout < 500000) || (fout > 109000000))  // If clock freq out of range
    si5351bx_clken |= 1 << clknum;            //  shut down the clock
  else {
    msa = si5351bx_vcoa / fout;  // Integer part of vco/fout
    msb = si5351bx_vcoa % fout;  // Fractional part of vco/fout
    msc = fout;                  // Divide by 2 till fits in reg
    while (msc & 0xfff00000) {
      msb = msb >> 1;
      msc = msc >> 1;
    }
    msxp1 =
        (128 * msa + 128 * msb / msc - 512) | (((uint32_t)si5351bx_rdiv) << 20);
    msxp2 = 128 * msb - 128 * msb / msc * msc;       // msxp3 == msc;
    msxp3p2top = (((msc & 0x0F0000) << 4) | msxp2);  // 2 top nibbles
    uint8_t vals[8] = {BB1(msc),   BB0(msc),        BB2(msxp1), BB1(msxp1),
                       BB0(msxp1), BB2(msxp3p2top), BB1(msxp2), BB0(msxp2)};
    i2cWriten(42 + (clknum * 8), vals, 8);  // Write to 8 msynth regs
    i2cWrite(16 + clknum, 0x0C | si5351bx_drive[clknum]);  // use local msynth
    si5351bx_clken &= ~(1 << clknum);  // Clear bit to enable clock
  }
  i2cWrite(3, si5351bx_clken);  // Enable/disable clock
}

void i2cWrite(uint8_t reg, uint8_t val) {  // write reg via i2c
  Wire.beginTransmission(SI5351BX_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void i2cWriten(uint8_t reg, uint8_t *vals, uint8_t vcnt) {  // write array
  Wire.beginTransmission(SI5351BX_ADDR);
  Wire.write(reg);
  while (vcnt--) Wire.write(*vals++);
  Wire.endTransmission();
}

void setFrequency() {
  if (vfo_high) {
    if (mode & 1)  // if we are in UPPER side band mode
      si5351bx_setfreq(2, (bfo_freq + frequency - RXshift + fine));
    else  // if we are in LOWER side band mode
      si5351bx_setfreq(2, (bfo_freq + frequency + RXshift + fine));
  } else {
    if (mode & 1)  // if we are in UPPER side band mode
      si5351bx_setfreq(2, (bfo_freq - frequency + RXshift - fine));
    else  // if we are in LOWER side band mode
      si5351bx_setfreq(2, (bfo_freq - frequency - RXshift - fine));
  }
}

void SetSideBand() {
  if (vfo_high) {
    switch (mode) {
      case USB:
        bfo_freq = bfo_freq - PBT_offset;
        break;
      case LSB:
        bfo_freq = bfo_freq + BFO_OFFSET_USB + PBT_offset;
        break;
    }
  } else {
    switch (mode) {
      case LSB:
        bfo_freq = bfo_freq - PBT_offset;
        break;
      case USB:
        bfo_freq = bfo_freq + BFO_OFFSET_USB + PBT_offset;
        break;
    }
  }
  si5351bx_setfreq(0, bfo_freq);
  setFrequency();
}

void passBandTuning() {
  if (inTx)
    PBT_offset = 0;  // no offset during TX (PBT works only in RX)
  else
    PBT_offset =
        2 * (analogRead(PBT) - 512);  // read the analog voltage from the PBT
                                      // pot (zero is centre position)
  if (abs(PBT_offset - PBT_offset_old) > 5) {
    SetSideBand();
    forceRefresh = true;
    PBT_offset_old = PBT_offset;
    return true;
  }
  return false;
}

unsigned long old_freq;
int lastMode = -1;

void modeNormal() {
  frequency += encoder.getValue() * freqMultip;
  if (frequency > MAX_FREQ) {
    frequency = MAX_FREQ;
  } else if (frequency < MIN_FREQ) {
    frequency = MIN_FREQ;
  }
  if (lastMode != NORMAL) {
    modeNormalView();
  }
  passBandTuning();
  if (frequency != old_freq) {
    setFrequency();
    forceRefresh = true;
    old_freq = frequency;
    Serial.print("Freq: ");
    Serial.println(frequency);
  }

  int buttonState = encoder.getButton();

  if (buttonState != 0) {
    Serial.print("Button: ");
    Serial.println(buttonState);
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
        if (freqMultip >= 10000) {
          freqMultip = 10000;
        } else {
          freqMultip *= 10;
        }
        forceRefresh = true;
        break;
    }
  }
  lastMode = NORMAL;
  if(forceRefresh){
    forceRefresh = false;
    modeNormalView();
  }
}

void modeNormalView() {
  lcd.cursor();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.print("     ");
  lcd.print(2 * PBT_offset);
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB");
  } else if (mode == USB) {
    lcd.print("USB");
  }
  if (carrierEnabled) {
    lcd.setCursor(15, 1);
    lcd.print('C');
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
    if(forceRefresh){
    forceRefresh = false;
    modeInTXView();
  }
}

void modeInTXView() {
  lcd.noCursor();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(frequency);
  lcd.setCursor(0, 1);
  if (mode == LSB) {
    lcd.print("LSB   TX");
  } else if (mode == USB) {
    lcd.print("USB   TX");
  }
  if (carrierEnabled) {
    lcd.setCursor(15, 1);
    lcd.print('C');
  }
}

void checkTX() {
  int buttonState = ptt.getButton();
  Serial.print("PTT: ");
  Serial.println(buttonState);
  if (buttonState == 0) {
    inTx = true;
  } else {
    inTx = false;
  }
}

void checkCarrier() {
  int buttonState = button.getButton();
  if (buttonState == 5) {
    carrierEnabled = !carrierEnabled;
    digitalWrite(CARRIER, carrierEnabled);
    forceRefresh = true;
  }
}

void setup() {
  Serial.begin(9600);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  lcd.begin(16, 2);

  analogReference(DEFAULT);

  encoder.setAccelerationEnabled(true);
  frequency = baseTune;
  old_freq = 0;

  si5351bx_init();
  si5351bx_vcoa = (SI5351BX_XTAL * SI5351BX_MSA);

  si5351bx_drive[2] = 0;  // VFO drive level (0=2mA, 1=4mA, 2=6mA, 3=8mA)
  si5351bx_drive[0] = 3;  // BFO drive level (0=2mA, 1=4mA, 2=6mA, 3=8mA)

  SetSideBand();

  pinMode(PTT_SENSE, INPUT);
  pinMode(TXRX, OUTPUT);
  pinMode(CARRIER, OUTPUT);
  pinMode(PBT, INPUT);
  pinMode(FBUTTON, INPUT_PULLUP);
  pinMode(FBUTTON2, INPUT_PULLUP);

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
  button.service();
  button2.service();
  ptt.service();
}
