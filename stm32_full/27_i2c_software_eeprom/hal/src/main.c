#include "stm32f1xx_hal.h"

/*
 * HAL 版：软件 I2C 写 EEPROM。
 *
 * HAL_GPIO_WritePin() 只是替代寄存器版的 BSRR/BRR 写法。
 * 本课重点仍然是软件按 I2C 规则控制 SCL/SDA 的时序。
 */

#define SCL_PIN GPIO_PIN_6
#define SDA_PIN GPIO_PIN_7

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void soft_i2c_gpio_init(void);
static void error_handler(void);

static void i2c_delay(void)
{
    for (volatile uint32_t i = 0U; i < 120U; i++) {
        __NOP();
    }
}

static void scl_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(GPIOB, SCL_PIN, state);
}

static void sda_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(GPIOB, SDA_PIN, state);
}

static void i2c_start(void)
{
    sda_write(GPIO_PIN_SET);
    scl_write(GPIO_PIN_SET);
    i2c_delay();

    sda_write(GPIO_PIN_RESET);
    i2c_delay();

    scl_write(GPIO_PIN_RESET);
}

static void i2c_stop(void)
{
    sda_write(GPIO_PIN_RESET);
    scl_write(GPIO_PIN_SET);
    i2c_delay();

    sda_write(GPIO_PIN_SET);
    i2c_delay();
}

static void i2c_write_bit(uint8_t bit_is_one)
{
    sda_write(bit_is_one ? GPIO_PIN_SET : GPIO_PIN_RESET);
    i2c_delay();

    scl_write(GPIO_PIN_SET);
    i2c_delay();

    scl_write(GPIO_PIN_RESET);
}

static void i2c_write_byte(uint8_t byte)
{
    for (uint8_t bit = 0U; bit < 8U; bit++) {
        i2c_write_bit((byte & 0x80U) != 0U);
        byte <<= 1;
    }

    /*
     * 释放 SDA 并给出第 9 个时钟。
     * 当前示例不读取 ACK，因此不能仅凭 LED 判断 EEPROM 写入成功。
     */
    sda_write(GPIO_PIN_SET);
    i2c_delay();
    scl_write(GPIO_PIN_SET);
    i2c_delay();
    scl_write(GPIO_PIN_RESET);
}

static void soft_i2c_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * OUTPUT_OD 表示开漏输出。
     * 写 SET 时释放总线，由上拉电阻拉高；写 RESET 时主动拉低。
     */
    gpio.Pin = SCL_PIN | SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    sda_write(GPIO_PIN_SET);
    scl_write(GPIO_PIN_SET);
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    soft_i2c_gpio_init();

    while (1) {
        i2c_start();
        i2c_write_byte(0xA0U);
        i2c_write_byte(0x00U);
        i2c_write_byte(0x5AU);
        i2c_stop();

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(1000);
    }
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
        error_handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        error_handler();
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

void SysTick_Handler(void)
{
    HAL_IncTick();
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
