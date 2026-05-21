#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 SysTick 毫秒节拍”。
 *
 * 目标：
 * 1. 使用 HAL 配置系统时钟到 72MHz
 * 2. 使用 HAL 配置 SysTick 为 1ms 节拍
 * 3. 在 SysTick 中断中同时维护：
 *    - 我们自己的毫秒计数
 *    - HAL 自己的内部 Tick
 * 4. 证明 HAL_Delay() 的底层依赖链路
 */

static volatile uint32_t g_ms_ticks = 0;

static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void systick_1ms_init(uint32_t hclk_hz);
static void delay_ms(uint32_t ms);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    led_pc13_init();
    systick_1ms_init(HAL_RCC_GetHCLKFreq());

    while (1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

        /*
         * 这里故意使用我们自己的 delay_ms()，
         * 让你看到它和 HAL_Delay() 背后的原理是一类东西。
         */
        delay_ms(500U);

        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(500U);
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

static void led_pc13_init(void)
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

static void systick_1ms_init(uint32_t hclk_hz)
{
    /*
     * HAL_SYSTICK_Config() 会根据传入的节拍值配置 SysTick。
     *
     * hclk_hz / 1000 的意思是：
     * - 如果 HCLK 是 72,000,000
     * - 那么每 72,000 个时钟周期触发一次
     * - 也就是每 1ms 触发一次
     */
    if (HAL_SYSTICK_Config(hclk_hz / 1000U) != HAL_OK) {
        error_handler();
    }

    /*
     * 选择 HCLK 作为 SysTick 时钟源。
     */
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

    /*
     * 设置 SysTick 中断优先级。
     *
     * 本课只做最简单配置，给它较高优先级即可。
     */
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

void SysTick_Handler(void)
{
    /*
     * 这是我们自己的软件毫秒计数。
     */
    g_ms_ticks++;

    /*
     * 这是 HAL 的内部 Tick 维护动作。
     *
     * 如果不调用它，HAL_Delay() 这类依赖 HAL Tick 的函数可能不正常。
     */
    HAL_IncTick();
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_ms_ticks;

    while ((g_ms_ticks - start) < ms) {
    }
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
