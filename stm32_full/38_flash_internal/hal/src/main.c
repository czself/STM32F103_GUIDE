#include "stm32f1xx_hal.h"

/*
 * HAL 版：内部 Flash 最后一页写入半字。
 *
 * HAL_FLASHEx_Erase() 和 HAL_FLASH_Program() 封装了页擦除和半字编程，
 * 但流程顺序仍然必须是：解锁 -> 擦除 -> 编程 -> 上锁 -> 读回。
 */

#define FLASH_LAST_PAGE 0x0800FC00UL
#define FLASH_TEST_VALUE 0x1234U

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static uint16_t flash_read_test_value(void);
static void flash_write_demo(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();

    if (flash_read_test_value() != FLASH_TEST_VALUE) {
        flash_write_demo();
    }

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay((flash_read_test_value() == FLASH_TEST_VALUE) ? 100U : 500U);
    }
}

static uint16_t flash_read_test_value(void)
{
    return *(__IO uint16_t *)FLASH_LAST_PAGE;
}

static void flash_write_demo(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        error_handler();
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_LAST_PAGE;
    erase.NbPages = 1U;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        error_handler();
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                          FLASH_LAST_PAGE,
                          FLASH_TEST_VALUE) != HAL_OK) {
        HAL_FLASH_Lock();
        error_handler();
    }

    if (HAL_FLASH_Lock() != HAL_OK) {
        error_handler();
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
