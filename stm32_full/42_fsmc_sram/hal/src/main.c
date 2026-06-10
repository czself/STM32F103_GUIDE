#include "stm32f1xx_hal.h"

/*
 * HAL 版：FSMC SRAM 教学模拟。
 *
 * 本课没有真实 HAL_SRAM_Init()，因为当前板子不接外部 SRAM。
 * HAL 版只演示同一条“写入 -> 读回 -> 校验”软件验证链路。
 */

static volatile uint16_t g_fake_sram[256];
static volatile uint32_t g_sram_errors;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void fake_sram_write_pattern(void);
static void fake_sram_verify_pattern(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    fake_sram_write_pattern();
    fake_sram_verify_pattern();

    while (1) {
        if (g_sram_errors == 0U) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        HAL_Delay(500);
    }
}

static void fake_sram_write_pattern(void)
{
    for (uint16_t index = 0U; index < 256U; index++) {
        g_fake_sram[index] = (uint16_t)(0x5500U | index);
    }
}

static void fake_sram_verify_pattern(void)
{
    g_sram_errors = 0U;

    for (uint16_t index = 0U; index < 256U; index++) {
        uint16_t expected = (uint16_t)(0x5500U | index);

        if (g_fake_sram[index] != expected) {
            g_sram_errors++;
        }
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
