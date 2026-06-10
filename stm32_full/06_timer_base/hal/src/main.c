#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 定时器基础”。
 *
 * 目标：
 * 1. 使用 HAL 配置 TIM2 基础定时功能
 * 2. 让 TIM2 每 1 秒触发一次更新中断
 * 3. 在 HAL 的周期到达回调中翻转 LED
 */

static TIM_HandleTypeDef htim2;

static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void tim2_base_init(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    led_pc13_init();
    tim2_base_init();

    /*
     * 启动 TIM2 基础定时，并开启更新中断。
     */
    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 主循环里不需要轮询延时。
         * LED 翻转会在定时器中断回调里完成。
         */
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

static void tim2_base_init(void)
{
    /*
     * 打开 TIM2 时钟。
     */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /*
     * 告诉 HAL：当前要操作的是 TIM2。
     */
    htim2.Instance = TIM2;

    /*
     * 当前 TIM2 输入时钟在本课配置下是 72MHz。
     *
     * Prescaler = 7200 - 1 后：
     * 72MHz / 7200 = 10kHz
     */
    htim2.Init.Prescaler = 7200U - 1U;

    /*
     * Period = 10000 - 1 后：
     * 10kHz / 10000 = 1Hz
     *
     * 也就是每 1 秒产生一次更新事件。
     */
    htim2.Init.Period = 10000U - 1U;

    /*
     * 向上计数模式。
     */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;

    /*
     * 时钟分频对本课基础定时不做额外分频。
     */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    /*
     * 不使用自动重装预装载。
     */
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置 NVIC，让 CPU 能接收 TIM2 中断。
     */
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void)
{
    /*
     * 真正的中断入口函数仍然是 TIM2_IRQHandler。
     *
     * HAL_TIM_IRQHandler() 会在里面检查标志位，
     * 并在合适时机调用 HAL_TIM_PeriodElapsedCallback()。
     */
    HAL_TIM_IRQHandler(&htim2);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    /*
     * 先确认回调来源确实是 TIM2。
     */
    if (htim->Instance == TIM2) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

void SysTick_Handler(void)
{
    /*
     * HAL_Init() 会配置 SysTick。即使本课不使用 HAL_Delay()，
     * 最小 HAL 工程也应维护 HAL tick，避免 SysTick 进入默认中断入口。
     */
    HAL_IncTick();
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
