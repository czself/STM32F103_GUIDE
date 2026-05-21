#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 EXTI 外部中断”。
 *
 * 目标：
 * - 把 PA0 配成下降沿中断输入
 * - 每次按下按键，翻转一次 PC13 LED 状态
 */

static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void key_pa0_exti_init(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    led_pc13_init();
    key_pa0_exti_init();

    while (1) {
        /*
         * 主循环里不做按键轮询。
         * LED 变化完全由中断回调驱动。
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

static void key_pa0_exti_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 打开 GPIOA 时钟。
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * GPIO_MODE_IT_FALLING 表示：
     * - 该引脚工作在中断模式
     * - 下降沿触发
     *
     * GPIO_PULLUP 表示：
     * - 引脚默认内部上拉
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * 允许 NVIC 接收 EXTI0 中断。
     */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI0_IRQHandler(void)
{
    /*
     * 进入 EXTI0 中断后，把控制权交给 HAL 的通用 EXTI 处理函数。
     *
     * 这里传入 GPIO_PIN_0，表示我们正在处理的是 0 号引脚对应的 EXTI。
     */
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /*
     * 先确认回调来源确实是 PA0。
     */
    if (GPIO_Pin == GPIO_PIN_0) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
