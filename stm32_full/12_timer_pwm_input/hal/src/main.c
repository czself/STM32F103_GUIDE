#include "stm32f1xx_hal.h"

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    /* HAL 结构体只是把寄存器配置换成字段；这里仍然是在选择 HSE 和 PLL x9。 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&osc);
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
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

static TIM_HandleTypeDef htim3;
static volatile uint32_t g_period_ticks;
static volatile uint32_t g_high_ticks;
static void tim3_pwm_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_IC_InitTypeDef ic = {0};
    TIM_SlaveConfigTypeDef slave = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_TIM3_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_6; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_NOPULL; HAL_GPIO_Init(GPIOA, &gpio);
    htim3.Instance = TIM3; htim3.Init.Prescaler = 72U - 1U; htim3.Init.CounterMode = TIM_COUNTERMODE_UP; htim3.Init.Period = 0xFFFFU; htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; HAL_TIM_IC_Init(&htim3);
    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING; ic.ICSelection = TIM_ICSELECTION_DIRECTTI; ic.ICPrescaler = TIM_ICPSC_DIV1; ic.ICFilter = 0; HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1);
    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING; ic.ICSelection = TIM_ICSELECTION_INDIRECTTI; HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_2);
    slave.SlaveMode = TIM_SLAVEMODE_RESET; slave.InputTrigger = TIM_TS_TI1FP1; HAL_TIM_SlaveConfigSynchro(&htim3, &slave);
    HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1); HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_2);
}
int main(void)
{
    HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); tim3_pwm_input_init();
    while (1) { g_period_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1); g_high_ticks = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2); HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(500); }
}
