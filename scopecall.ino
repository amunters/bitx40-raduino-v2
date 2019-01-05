#include <si5351.h>

Si5351 si5351;

void setup() {
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 25003900L, 0);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
  si5351.output_enable(SI5351_CLK2, 1);
  si5351.set_freq(25e8, SI5351_CLK2);
}

void loop() {}
