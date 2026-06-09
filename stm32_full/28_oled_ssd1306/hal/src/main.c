#include "stm32f1xx_hal.h"

static I2C_HandleTypeDef hi2c1;
static void system_clock_72mhz_init(void); static void pc13_led_init(void); static void i2c1_init(void);
#define OLED_ADDR 0x78U
static void oled_write(uint8_t control, uint8_t value) { uint8_t buf[2] = {control, value}; HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 100); }
static void oled_cmd(uint8_t c) { oled_write(0x00U, c); }
static void oled_data(uint8_t d) { oled_write(0x40U, d); }
static void oled_init(void) { uint8_t init[] = {0xAE,0x20,0x02,0xB0,0xC8,0x00,0x10,0x40,0x81,0x7F,0xA1,0xA6,0xA8,0x3F,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,0xDB,0x40,0x8D,0x14,0xAF}; for (uint32_t i=0;i<sizeof(init);++i) oled_cmd(init[i]); }
int main(void)
{
    HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); i2c1_init(); HAL_Delay(50); oled_init();
    oled_cmd(0xB0); oled_cmd(0x00); oled_cmd(0x10); for (uint8_t i=0;i<128;++i) oled_data((i & 1U) ? 0xAAU : 0x55U);
    while (1) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(500); }
}
static void i2c1_init(void)
{
    GPIO_InitTypeDef gpio = {0}; __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_I2C1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7; gpio.Mode = GPIO_MODE_AF_OD; gpio.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOB, &gpio);
    hi2c1.Instance = I2C1; hi2c1.Init.ClockSpeed = 100000; hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2; hi2c1.Init.OwnAddress1 = 0; hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; hi2c1.Init.OwnAddress2 = 0; hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; HAL_I2C_Init(&hi2c1);
}
static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) {
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        while (1) {
        }
    }
}
static void pc13_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}
