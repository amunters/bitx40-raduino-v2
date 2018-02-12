# Raduino v2 for BitX40

This sketch implements digital BFO functionality to the BitX40. The original analog BFO oscillator is disabled. Instead we use the CLK0 output of the si5351 to generate the BFO signal, and inject it into the BitX40 board.
The BFO frequency is controlled by the sketch. The sketch will set the BFO frequency depending on the mode (LSB, USB, etc.). This has multiple advantages:
- Adjustable BFO frequency allows for optimum alignment with the filter pass band in each mode
- It is now possible to set the VFO to the high side of the IF in each mode => no images from 41m broadcast stations, less birdies
- Possibility to add a 'clarifier' pot to the front panel. The behaviour is somewhat similar to 'IF shift' as found on many commercial radios.

First time builders: It is recommmended to first install the [raduino v1](https://github.com/amunters/bitx40) sketch and make sure that everything including the related mods work properly, before proceding with this v2 sketch.

**Note 1:** Unlike [raduino v1](https://github.com/amunters/bitx40), this sketch will not work on a unmodified out-of-the-box BITX40 + raduino board. Some additional minimal hardware modifications as outlined [below](operating-instructions.md) are required (v2 is not downward compatible with v1).

**Note 2:** Upgrading from raduino_v1 to v2: Existing hardware modifications as used in [raduino v1](https://github.com/amunters/bitx40) still work under v2. The wiring and pin connections are still the same, except the CAL wire (pin A2) is no longer used (v1 is upward compatible with v2).

**Note 3:** The library [PinChangeInterrupt](https://playground.arduino.cc/Main/PinChangeInterrupt) is required for interrupt handling. Use your IDE to install it before compiling this sketch!

![Hardware mod overview](hardware%20modification%20overview%20v2.PNG) 

See the [operating and modification instructions](operating-instructions.md) for full details.

## Donate

I develop and maintain ham radio software as a hobby and distribute it for free. However, if you like this software, please consider to donate a small amount to my son's home who stays in an institute for kids with an intellectual disability and autism. The money will be used for adapted toys, a tricycle, a trampoline or a swing. Your support will be highly appreciated by this group of 6 young adolescents!

 [![Donate](https://www.paypalobjects.com/en_US/GB/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=PTAMBM6QT8LP8)

## Revision record

v2.04
- renamed "CLARIFIER" to "Passband Tuning (PBT)" as this seems a more appropriate name for this function
- improved the code so that the si5351 does not keep receiving tuning updates once the frequency has reached the upper or lower limit

v2.03
- Added BFO calibration parameters for CWL and CWU to optimize the performance in CW mode
- Revised and improved the calibration procedures (tks Michael, VE3WMB)
- Corrected a bug that in certain scenarios mode switching did not function correctly
- Updated the instructions for the CW-CARRIER mod: Advise to use a 4.7K resistor instead of 10K in order to ensure full output power in CW mode
- Added capability (see line 31) to display the user's callsign on the second line of the LCD
- Added capability (see line 32) to adjust the delay time for 'fast tuning' (when the tuning pot is at either end of the range)

v2.02
- Fixed another bug in the calibration procedure
- Slightly changed the initial BFO frequencies to 11998800 Hz (LSB) and 11996600 Hz (USB)

v2.01
- Fixed a bug that the VFO setting (high/low side of IF) didn't return to the original state after executing the calibration procedure

v2.00
- Initial release of Raduino v2 sketch for BitX40

