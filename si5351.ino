#include <Wire.h>

#define SI_CLK0_CONTROL  16      // Register definitions
#define SI_CLK1_CONTROL 17
#define SI_CLK2_CONTROL 18
#define SI_SYNTH_PLL_A  26
#define SI_SYNTH_PLL_B  34
#define SI_SYNTH_MS_0   42
#define SI_SYNTH_MS_1   50
#define SI_SYNTH_MS_2   58
#define SI_PLL_RESET    177

#define SI_R_DIV_1    0b00000000      // R-division ratio definitions
#define SI_R_DIV_2    0b00010000
#define SI_R_DIV_4    0b00100000
#define SI_R_DIV_8    0b00110000
#define SI_R_DIV_16   0b01000000
#define SI_R_DIV_32   0b01010000
#define SI_R_DIV_64   0b01100000
#define SI_R_DIV_128    0b01110000

#define SI_CLK_SRC_PLL_A  0b00000000
#define SI_CLK_SRC_PLL_B  0b00100000

#define SI5351_CLK_PLL_SELECT_B (1<<5) 
#define SI5351_CLK_INTEGER_MODE (1<<6)   
#define SI5351_CLK_INPUT_MULTISYNTH_N  (3<<2)

#define SI5351_CLK_DRIVE_STRENGTH_MASK    (3<<0)
#define SI5351_CLK_DRIVE_STRENGTH_2MA   (0<<0)
#define SI5351_CLK_DRIVE_STRENGTH_4MA   (1<<0)
#define SI5351_CLK_DRIVE_STRENGTH_6MA   (2<<0)
#define SI5351_CLK_DRIVE_STRENGTH_8MA   (3<<0)

#define PLL_N 32
#define PLLFREQ (xtal_freq_calibrated * PLL_N)

uint32_t plla_freq, pllb_freq;

class I2C {
public:
  #define I2C_DELAY   3    // Determines I2C Speed (2=939kb/s (too fast!!); 3=822kb/s; 4=731kb/s; 5=658kb/s; 6=598kb/s). Increase this value when you get I2C tx errors (E05); decrease this value when you get a CPU overload (E01). An increment eats ~3.5% CPU load; minimum value is 3 on my QCX, resulting in 84.5% CPU load
  #define I2C_DDR DDRC     // Pins for the I2C bit banging
  #define I2C_PIN PINC
  #define I2C_PORT PORTC
  #define I2C_SDA (1 << 4) // PC4
  #define I2C_SCL (1 << 5) // PC5
  #define DELAY(n) for(uint8_t i = 0; i != n; i++) asm("nop");
  #define I2C_SDA_GET() I2C_PIN & I2C_SDA
  #define I2C_SCL_GET() I2C_PIN & I2C_SCL
  #define I2C_SDA_HI() I2C_DDR &= ~I2C_SDA;
  #define I2C_SDA_LO() I2C_DDR |=  I2C_SDA;
  #define I2C_SCL_HI() I2C_DDR &= ~I2C_SCL; DELAY(I2C_DELAY);
  #define I2C_SCL_LO() I2C_DDR |=  I2C_SCL; DELAY(I2C_DELAY);

  I2C(){
    I2C_PORT &= ~( I2C_SDA | I2C_SCL );
    I2C_SCL_HI();
    I2C_SDA_HI();
    suspend();
  }
  ~I2C(){
    I2C_PORT &= ~( I2C_SDA | I2C_SCL );
    I2C_DDR &= ~( I2C_SDA | I2C_SCL );
  }  
  inline void start(){
    resume();  //prepare for I2C
    I2C_SCL_LO();
    I2C_SDA_HI();
  }
  inline void stop(){
    I2C_SCL_HI();
    I2C_SDA_HI();
    I2C_DDR &= ~(I2C_SDA | I2C_SCL); // prepare for a start: pull-up both SDA, SCL
    suspend();
  }
  #define SendBit(data, mask) \
    if(data & mask){ \
      I2C_SDA_HI();  \
    } else {         \
      I2C_SDA_LO();  \
    }                \
    I2C_SCL_HI();    \
    I2C_SCL_LO();
  /*#define SendByte(data) \
    SendBit(data, 1 << 7) \
    SendBit(data, 1 << 6) \
    SendBit(data, 1 << 5) \
    SendBit(data, 1 << 4) \
    SendBit(data, 1 << 3) \
    SendBit(data, 1 << 2) \
    SendBit(data, 1 << 1) \
    SendBit(data, 1 << 0) \
    I2C_SDA_HI();  // recv ACK \
    DELAY(I2C_DELAY);     \
    I2C_SCL_HI();         \
    I2C_SCL_LO();*/
  inline void SendByte(uint8_t data){
    SendBit(data, 1 << 7);
    SendBit(data, 1 << 6);
    SendBit(data, 1 << 5);
    SendBit(data, 1 << 4);
    SendBit(data, 1 << 3);
    SendBit(data, 1 << 2);
    SendBit(data, 1 << 1);
    SendBit(data, 1 << 0);
    I2C_SDA_HI();  // recv ACK
    DELAY(I2C_DELAY);
    I2C_SCL_HI();
    I2C_SCL_LO();
  }

  inline void resume(){
  #ifdef LCD_RS_PORTIO
    I2C_PORT &= ~I2C_SDA; // pin sharing SDA/LCD_RS mitigation
  #endif
  }
  inline void suspend(){
    I2C_SDA_LO();         // pin sharing SDA/LCD_RS: pull-down LCD_RS; QCXLiquidCrystal require this for any operation
  }
};

I2C i2c;
#define SI5351_ADDR 0x60              // I2C address of Si5351   (typical)

void i2cSendRegister(uint8_t reg, uint8_t* data, uint8_t n){
    i2c.start();
    i2c.SendByte(SI5351_ADDR << 1);
    i2c.SendByte(reg);
    while (n--) i2c.SendByte(*data++);
    i2c.stop();      
}

void i2cSendRegister(uint8_t reg, uint8_t val){ 
  i2cSendRegister(reg, &val, 1); 
}

void si5351_reset(){
  i2cSendRegister(SI_PLL_RESET, 0xA0);  
}

void si5351a_clkoff(uint8_t clk)
{
  //i2c_init();
  
  i2cSendRegister(clk, 0x80);   // Refer to SiLabs AN619 to see bit values - 0x80 turns off the output stage

  //i2c_exit();
}


static void setup_pll(uint8_t pll, uint8_t mult, uint32_t num, uint32_t denom)
{
  uint32_t P1;          // PLL config register P1
  uint32_t P2;          // PLL config register P2
  uint32_t P3;          // PLL config register P3

  if (num == 0){
    P1 = 128 * mult - 512;
    P2 = 0;
    P3 = 1;    
  }
  else {
    P1 = (uint32_t)(128 * ((float)num / (float)denom));
    P1 = (uint32_t)(128 * (uint32_t)(mult) + P1 - 512);
    P2 = (uint32_t)(128 * ((float)num / (float)denom));
    P2 = (uint32_t)(128 * num - denom * P2);
    P3 = denom;
  }
  
  i2cSendRegister(pll + 0, (P3 & 0x0000FF00) >> 8);
  i2cSendRegister(pll + 1, (P3 & 0x000000FF));
  i2cSendRegister(pll + 2, (P1 & 0x00030000) >> 16);
  i2cSendRegister(pll + 3, (P1 & 0x0000FF00) >> 8);
  i2cSendRegister(pll + 4, (P1 & 0x000000FF));
  i2cSendRegister(pll + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  i2cSendRegister(pll + 6, (P2 & 0x0000FF00) >> 8);
  i2cSendRegister(pll + 7, (P2 & 0x000000FF));
}

static void setup_multisynth(uint8_t clk, uint8_t pllSource, uint32_t divider,  uint32_t num, uint32_t denom, uint32_t rdiv,  uint8_t drive_strength){
  uint8_t synth, control;

  switch (clk){
    case 0:
      synth = 42;
      control = 16;
      break;
    case 1:
      synth = 50;
      control =17;
      break;
    //clock 2
    default:
      synth = 58;
      control = 18;
      break;   
  }
  
  uint8_t dat;

  uint32_t P1;
  uint32_t P2;
  uint32_t P3;
  uint32_t div4 = 0;

  /* Output Multisynth Divider Equations
   * where: a = div, b = num and c = denom
   * P1 register is an 18-bit value using following formula:
   *  P1[17:0] = 128 * a + floor(128*(b/c)) - 512
   * P2 register is a 20-bit value using the following formula:
   *  P2[19:0] = 128 * b - c * floor(128*(b/c))
   * P3 register is a 20-bit value using the following formula:
   *  P3[19:0] = c
   */
  /* Set the main PLL config registers */
 #define SI5351_DIVBY4     (3<<2) 
  
  if (divider == 4) {
    div4 = SI5351_DIVBY4;
    P1 = P2 = 0;
    P3 = 1;
  } else if (num == 0) {
    /* Integer mode */
    P1 = 128 * divider - 512;
    P2 = 0;
    P3 = 1;
  } else {
    /* Fractional mode */
    P1 = 128 * divider + ((128 * num) / denom) - 512;
    P2 = 128 * num - denom * ((128 * num) / denom);
    P3 = denom;
  }

  i2cSendRegister(synth + 0,   (P3 & 0x0000FF00) >> 8);
  i2cSendRegister(synth + 1,   (P3 & 0x000000FF));
  i2cSendRegister(synth + 2,   ((P1 & 0x00030000) >> 16) | div4 | rdiv);
  i2cSendRegister(synth + 3,   (P1 & 0x0000FF00) >> 8);
  i2cSendRegister(synth + 4,   (P1 & 0x000000FF));
  i2cSendRegister(synth + 5,   ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  i2cSendRegister(synth + 6,   (P2 & 0x0000FF00) >> 8);
  i2cSendRegister(synth + 7,   (P2 & 0x000000FF));

/* clock control register
 *  |    7    |    6    |    5    |    4    |   3    |  2   |   1   |  0   |
 *   pwr off?   ms_int?   pll b?    o/p inv? <--  source --> <-- drive ---->
 */

  /* Configure the clk control and enable the output */
  dat = drive_strength | SI5351_CLK_INPUT_MULTISYNTH_N;
  if (pllSource == SI_SYNTH_PLL_B)
    dat |= SI_CLK_SRC_PLL_B;
  if (num == 0)
    dat |= SI5351_CLK_INTEGER_MODE;
  i2cSendRegister(control, dat);
}


static void set_frequency_fixedpll(int clk, int pll, uint32_t pllfreq, uint32_t freq, int rdiv, uint8_t drive_strength){
  int32_t denom = 0x80000l;
  uint32_t divider = pllfreq / freq; // range: 8 ~ 1800
  uint32_t integer_part = divider * freq;
//  Serial.print("*");Serial.print(integer_part);
  uint32_t reminder = pllfreq -  integer_part;
//  Serial.print("r");Serial.print(reminder);

  uint32_t num = ((uint64_t)reminder * (uint64_t)denom)/freq;
/*
  Serial.print("pll:");Serial.print(pllfreq);
  Serial.print(", Freq:");
  Serial.print(freq);Serial.print(",");
  Serial.print(", divr:");  Serial.print(divider);
  Serial.print(", rem:");Serial.print(reminder);
  Serial.print(", num");
  Serial.println(num);
*/
  setup_multisynth(clk, pll, divider, num, denom, rdiv, drive_strength);
}


static void set_freq_fixeddiv(int clk, int pll, uint32_t frequency, int divider,  uint8_t drive_strength){
  int32_t denom = 0x80000;
  int32_t pllfreq = frequency * divider;
  int32_t multi = pllfreq / xtal_freq_calibrated;
  int32_t num = ((uint64_t)(pllfreq % xtal_freq_calibrated) * 0x80000)/xtal_freq_calibrated;

/*  Serial.print("317:");
  Serial.print(multi);Serial.print(",");
  Serial.print(num);Serial.print(",");
  Serial.print(denom);Serial.print(",");
  Serial.println(divider); */
  setup_pll(pll, multi, num, denom);
  setup_multisynth(clk, pll, divider, 0, 1, SI_R_DIV_1, drive_strength);
}

void si5351_setfreq(int clk, uint32_t frequency){
  uint8_t pll;
  if (clk == 2)
    pll = SI_SYNTH_PLL_B;
  else
    pll = SI_SYNTH_PLL_A;
 
  //Serial.println(frequency);
  if (frequency <= 100000000l){
    setup_pll(pll, 26, 0, 1);
    set_frequency_fixedpll(clk, pll, (xtal_freq_calibrated * 26), frequency, SI_R_DIV_1, SI5351_CLK_DRIVE_STRENGTH_8MA);
  }
  else if (frequency < 150000000l)
    set_freq_fixeddiv(clk, pll, frequency, 6, SI5351_CLK_DRIVE_STRENGTH_8MA);
  else 
    set_freq_fixeddiv(clk, pll, frequency, 4, SI5351_CLK_DRIVE_STRENGTH_8MA);
}
