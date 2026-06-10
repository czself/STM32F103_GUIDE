#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 PWM 进阶”。
 *
 * 08 已经学过 HAL 怎样配置 TIM2_CH1 PWM。
 * 本课的新重点是“占空比更新策略”：
 * - 继续用 __HAL_TIM_SET_COMPARE() 改 CCR1
 * - 但 duty 不再简单线性递增
 * - 而是按亮度区间选择不同步长，让呼吸灯更自然
 */

static TIM_HandleTypeDef htim2;

static void system_clock_72mhz_init(void);
static void hal_msp_init_minimal(void);
static void gpio_pa0_pwm_init(void);
static void tim2_pwm_init(void);
static uint32_t next_duty(uint32_t duty, int8_t direction);
static void error_handler(void);

int main(void)
{
    uint32_t duty = 0U;
    int8_t direction = 1;

    HAL_Init();
    hal_msp_init_minimal();
    system_clock_72mhz_init();
    gpio_pa0_pwm_init();
    tim2_pwm_init();

    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 通过 HAL 宏修改当前比较值。
         * 它本质上还是在改 CCR1；PWM 输出本身继续由 TIM2_CH1 硬件完成。
         */
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);

        HAL_Delay(25);

        if (duty >= 1000U) {
            direction = -1;
        } else if (duty == 0U) {
            direction = 1;
        }

        duty = next_duty(duty, direction);
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
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    /*
     * Blue Pill 常用 ST-Link/SWD 下载调试。
     * 关闭 JTAG，保留 SWD，避免 JTAG 复用占住部分 GPIO。
     */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

static void gpio_pa0_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA0 仍然作为 TIM2_CH1 复用推挽输出；这部分沿用 08 的 PWM 基础。 */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void tim2_pwm_init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;

    /*
     * 继续沿用 08 的 1kHz PWM 参数：
     * 72MHz / 72 = 1MHz，1MHz / 1000 = 1kHz。
     */
    htim2.Init.Prescaler = 72U - 1U;
    htim2.Init.Period = 1000U - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        error_handler();
    }

    /*
     * PWM1 + 初始 Pulse=0：
     * 让 LED 从全灭附近开始，再由主循环逐步提高 CCR1。
     */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }
}

static uint32_t next_duty(uint32_t duty, int8_t direction)
{
    uint32_t step;

    /*
     * 这里故意不使用固定步长。
     *
     * 暗部用小步长，避免一开始突然变亮；
     * 中段用较大步长，让呼吸节奏不要拖；
     * 高亮附近再放慢一点，让到顶端的过渡更柔和。
     */
    if (duty < 120U) {
        step = 5U;
    } else if (duty < 400U) {
        step = 15U;
    } else if (duty < 750U) {
        step = 25U;
    } else {
        step = 12U;
    }

    if (direction > 0) {
        /* 变亮方向夹到 1000，和 Period=999 对应的满占空比范围保持一致。 */
        if (duty + step >= 1000U) {
            return 1000U;
        }
        return duty + step;
    }

    if (duty <= step) {
        /* 变暗方向夹到 0，避免 uint32_t 下溢成很大的数。 */
        return 0U;
    }
    return duty - step;
}

void SysTick_Handler(void)
{
    /* 主循环使用 HAL_Delay() 控制呼吸节奏，因此需要维护 HAL tick。 */
    HAL_IncTick();
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
