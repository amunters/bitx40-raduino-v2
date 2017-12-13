# Raduino v2 for BitX40

This sketch implements digital BFO functionality to the BitX40. The original analog BFO oscillator is disabled. Instead we use the CLK0 output of the si5351 to generate the BFO signal, and inject it into the BitX40 board.
The BFO frequency is now controlled by the sketch. The sketch will set the appropriate BFO frequency depending on the mode (LSB, USB). 

**Note 1:** Unlike raduino v1, this sketch will not work on a unmodified out-of-the-box BITX40 + raduino board. Some minimal hardware modifications as outlined below are required. 

**Note 2:** The library [PinChangeInterrupt](https://playground.arduino.cc/Main/PinChangeInterrupt) is required for interrupt handling. Use your IDE to install it before compiling this sketch!

![Hardware mod overview](https://github.com/amunters/bitx40/blob/master/hardware%20modification%20overview.PNG) 

See the [operating and modification instructions](https://github.com/amunters/bitx40/blob/master/operating-instructions.md) for full details.

## Donate

I develop and maintain ham radio software as a hobby and distribute it for free. However, if you like this software, please consider to donate a small amount to my son's home who stays in an institute for kids with an intellectual disability and autism. The money will be used for adapted toys, a tricycle, a trampoline or a swing. Your support will be highly appreciated by this group of 6 young adolescents!

 [![Donate](https://www.paypalobjects.com/en_US/GB/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=PTAMBM6QT8LP8)

## Revision record

v2.0  Initial release of Raduino v2 sketch for BitX40
