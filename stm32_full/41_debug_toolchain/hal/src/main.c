#include "stm32f1xx_hal.h"

/*
 * HAL版：调试工具链与断点观察。
 *
 * HAL 版把 GPIO 和时钟配置封装成结构体/API，但调试器观察的东西没有变：
 * Watch 仍然看 RAM 变量，断点仍然暂停 CPU，DWT->CYCCNT 仍然来自 Cortex-M3 内核。
 */

static volatile uint32_t g_watch_counter;
static volatile uint32_t g_cycle_counter;

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 这些字段最终仍然对应 RCC 的 HSE、PLL 和总线分频配置。
     * 调试时若时钟没有到 72MHz，HAL_Delay 和 CYCCNT 换算都会跟着偏。
     */
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

static void debug_counter_init(void)
{
    /*
     * HAL 不负责 DWT 周期计数器。
     * 这里直接操作 Cortex-M 调试寄存器，让 Watch 可以观察运行周期。
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    debug_counter_init();

    while (1) {
        /*
         * volatile 让这些写入保留下来，调试器 Watch 更容易看见每轮变化。
         */
        g_watch_counter++;
        g_cycle_counter = DWT->CYCCNT;

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500U);
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
