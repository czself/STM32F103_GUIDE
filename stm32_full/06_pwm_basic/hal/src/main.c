#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 PWM 基础”。
 *
 * 目标：
 * 1. 使用 HAL 配置 TIM2_CH1 的 PWM 输出
 * 2. 在 PA0 输出 1kHz PWM
 * 3. 动态修改 Pulse，占空比随之变化
 */

static TIM_HandleTypeDef htim2;

static void system_clock_72mhz_init(void);
static void hal_msp_init_minimal(void);
static void gpio_pa0_pwm_init(void);
static void tim2_pwm_init(void);
static void error_handler(void);

int main(void)
{
    uint32_t duty = 0U;
    int32_t step = 50;

    HAL_Init();
    hal_msp_init_minimal();
    system_clock_72mhz_init();
    gpio_pa0_pwm_init();
    tim2_pwm_init();

    /*
     * 启动 TIM2 的通道 1 PWM 输出。
     */
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 通过修改比较值，动态改变占空比。
         *
         * __HAL_TIM_SET_COMPARE() 本质上是在改 CCR1。
         */
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);

        HAL_Delay(40);

        if ((int32_t)duty + step >= 1000) {
            duty = 1000U;
            step = -50;
        } else if ((int32_t)duty + step <= 0) {
            duty = 0U;
            step = 50;
        } else {
            duty = (uint32_t)((int32_t)duty + step);
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

static void hal_msp_init_minimal(void)
{
    /*
     * 在 CubeMX / Keil 工程里，这部分基础 MSP 初始化通常由生成器
     * 自动放到 stm32f1xx_hal_msp.c 里。
     *
     * 但在当前这套 PlatformIO 最小 stm32cube 工程里，如果你只有一个
     * main.c，就要自己把最基础的 AFIO / PWR 时钟补上。
     *
     * 对本课 TIM2_CH1 -> PA0 这个例子来说，它们不是“PWM 输出唯一必需”，
     * 但这是 F1 HAL 工程的标准起手式。后续如果加重映射、中断、低功耗，
     * 这里也不会缺口。
     */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

static void gpio_pa0_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 打开 GPIOA 时钟。
     *
     * PA0 将作为 TIM2_CH1 的 PWM 输出脚。
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * 这里必须选择复用推挽输出，而不是普通 GPIO 输出。
     *
     * 原因是 PA0 的电平将由定时器通道硬件驱动。
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void tim2_pwm_init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    /*
     * 打开 TIM2 时钟。
     */
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;

    /*
     * 72MHz / 72 = 1MHz
     */
    htim2.Init.Prescaler = 72U - 1U;

    /*
     * 1MHz / 1000 = 1kHz
     */
    htim2.Init.Period = 1000U - 1U;

    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置通道 1 的 PWM 参数。
     *
     * TIM_OCMODE_PWM1：
     * - 选择 PWM mode 1
     *
     * Pulse：
     * - 对应底层 CCR1
     * - 决定高电平宽度
     */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 250U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }
}

void SysTick_Handler(void)
{
    /*
     * HAL_Init() 会配置 SysTick，HAL_Delay() 依赖这里的 tick 递增。
     *
     * 如果这个中断处理函数缺失，在一些“只有 main.c 的最小工程”里，
     * CPU 会掉进启动文件的默认中断死循环。
     *
     * 现象通常是：
     * - 程序刚启动时像是运行了一下
     * - 然后就卡住
     * - 如果点灯，可能表现成“常亮”或“常灭”
     */
    HAL_IncTick();
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
