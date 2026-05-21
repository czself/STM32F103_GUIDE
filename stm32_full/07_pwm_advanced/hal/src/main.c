#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 PWM 进阶”。
 *
 * 目标：
 * 1. 继续使用 TIM2_CH1 在 PA0 输出 PWM
 * 2. 通过分段步进策略动态修改占空比
 * 3. 实现更自然的呼吸灯
 */

static TIM_HandleTypeDef htim2;

static void system_clock_72mhz_init(void);
static void gpio_pa0_pwm_init(void);
static void tim2_pwm_init(void);
static uint32_t next_duty(uint32_t duty, int8_t direction);
static void error_handler(void);

int main(void)
{
    uint32_t duty = 0U;
    int8_t direction = 1;

    HAL_Init();
    system_clock_72mhz_init();
    gpio_pa0_pwm_init();
    tim2_pwm_init();

    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 通过 HAL 宏修改当前比较值。
         * 它本质上还是在改 CCR1。
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

static void gpio_pa0_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

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
    htim2.Init.Prescaler = 72U - 1U;
    htim2.Init.Period = 1000U - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        error_handler();
    }

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
        if (duty + step >= 1000U) {
            return 1000U;
        }
        return duty + step;
    }

    if (duty <= step) {
        return 0U;
    }
    return duty - step;
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
