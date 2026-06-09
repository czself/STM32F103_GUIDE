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

static TIM_HandleTypeDef htim1;
static void tim1_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0}; TIM_OC_InitTypeDef oc = {0}; TIM_BreakDeadTimeConfigTypeDef bd = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_TIM1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_8; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOA, &gpio);
    htim1.Instance = TIM1; htim1.Init.Prescaler = 72U - 1U; htim1.Init.CounterMode = TIM_COUNTERMODE_UP; htim1.Init.Period = 1000U - 1U; htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; htim1.Init.RepetitionCounter = 0; HAL_TIM_PWM_Init(&htim1);
    oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 300U; oc.OCPolarity = TIM_OCPOLARITY_HIGH; oc.OCFastMode = TIM_OCFAST_DISABLE; HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
    bd.OffStateRunMode = TIM_OSSR_DISABLE; bd.OffStateIDLEMode = TIM_OSSI_DISABLE; bd.LockLevel = TIM_LOCKLEVEL_OFF; bd.DeadTime = 0; bd.BreakState = TIM_BREAK_DISABLE; bd.BreakPolarity = TIM_BREAKPOLARITY_HIGH; bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE; HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bd);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}
int main(void){ HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); tim1_pwm_init(); while(1){ HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(500); } }
