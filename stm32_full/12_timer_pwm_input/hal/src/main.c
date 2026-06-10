#include "stm32f1xx_hal.h"

/*
 * HAL 版：TIM PWM 输入模式。
 *
 * 本课不是重新讲普通输入捕获，而是看 HAL 如何表达 PWM 输入组合：
 * - CH1 direct TI 捕获上升沿
 * - CH2 indirect TI 捕获同一个 TI1 的下降沿
 * - reset slave mode 让每个上升沿把 CNT 清零
 */

static TIM_HandleTypeDef htim3;
static volatile uint32_t g_period_ticks;
static volatile uint32_t g_high_ticks;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void tim3_pwm_input_init(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    tim3_pwm_input_init();

    while (1) {
        /*
         * 轮询读取捕获寄存器。
         * Channel 1 对应 CCR1：周期；Channel 2 对应 CCR2：高电平时间。
         */
        g_period_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);
        g_high_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2);

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}

static void tim3_pwm_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_IC_InitTypeDef ic = {0};
    TIM_SlaveConfigTypeDef slave = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /*
     * PA6 是 TIM3_CH1 输入脚。GPIO_MODE_INPUT 只负责让引脚进入输入状态，
     * 真正的“捕获上升沿/下降沿”由后面的 TIM IC 配置决定。
     */
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 72U - 1U;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFFU;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    if (HAL_TIM_IC_Init(&htim3) != HAL_OK) {
        error_handler();
    }

    /*
     * CH1：direct TI，直接连接 TI1，捕获上升沿。
     * 这一路保存 PWM 周期到 CCR1。
     */
    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter = 0;

    if (HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    /*
     * CH2：indirect TI，也看 TI1，但捕获下降沿。
     * 这一路保存高电平时间到 CCR2。
     */
    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;

    if (HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_2) != HAL_OK) {
        error_handler();
    }

    /*
     * reset slave mode 对应寄存器版 SMCR：
     * - InputTrigger = TI1FP1
     * - SlaveMode = RESET
     *
     * 结果是每个上升沿清零 CNT，下一个上升沿捕获到的 CCR1 就是周期。
     */
    slave.SlaveMode = TIM_SLAVEMODE_RESET;
    slave.InputTrigger = TIM_TS_TI1FP1;

    if (HAL_TIM_SlaveConfigSynchro(&htim3, &slave) != HAL_OK) {
        error_handler();
    }

    if (HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    if (HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) {
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
