#include "stm32f1xx_hal.h"

/*
 * HAL 版：核心架构与内存映射观察。
 *
 * HAL 会把 RCC/GPIO 的寄存器操作包装成结构体和 API，但 CPU 看到的地址空间
 * 没有变化：Flash、SRAM、外设寄存器、内核外设仍然位于固定地址范围。
 */

static volatile uint32_t g_debug_words[5];

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void memory_map_sample_init(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    memory_map_sample_init();

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 时钟树细节在上一课已经讲过，这里只保留和 HAL 字段的对应关系。 */
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

static void memory_map_sample_init(void)
{
    /*
     * 这些值建议放进调试器 watch 窗口看。
     * HAL 工程里也能直接读 CMSIS 提供的地址常量和内核寄存器。
     */
    g_debug_words[0] = SCB->CPUID;

    /* Flash 是程序代码主要所在区域。 */
    g_debug_words[1] = FLASH_BASE;

    /* SRAM 是普通变量、栈、堆常见所在区域。 */
    g_debug_words[2] = SRAM_BASE;

    /* 外设地址空间从这里开始，GPIO/RCC/TIM 等寄存器都映射在这一类区域。 */
    g_debug_words[3] = PERIPH_BASE;

    /* GPIOC_BASE 对应 HAL_GPIO_Init(GPIOC, ...) 背后真正操作的端口寄存器组。 */
    g_debug_words[4] = GPIOC_BASE;
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}

void SysTick_Handler(void)
{
    /* HAL_Delay() 依赖 HAL tick；这里保留最小中断入口即可。 */
    HAL_IncTick();
}
