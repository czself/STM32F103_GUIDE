#include "stm32f1xx_hal.h"

static void system_clock_72mhz_init(void); static void pc13_led_init(void); static void pa0_exti_init(void);
int main(void){HAL_Init();system_clock_72mhz_init();pc13_led_init();pa0_exti_init();while(1){HAL_SuspendTick();HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON,PWR_STOPENTRY_WFI);system_clock_72mhz_init();HAL_ResumeTick();HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);HAL_Delay(500);}}
void EXTI0_IRQHandler(void){HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);} void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){(void)GPIO_Pin;}
static void pa0_exti_init(void){GPIO_InitTypeDef gpio={0};__HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_AFIO_CLK_ENABLE();__HAL_RCC_PWR_CLK_ENABLE();gpio.Pin=GPIO_PIN_0;gpio.Mode=GPIO_MODE_IT_FALLING;gpio.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOA,&gpio);HAL_NVIC_SetPriority(EXTI0_IRQn,2,0);HAL_NVIC_EnableIRQ(EXTI0_IRQn);}
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
        while (1) {
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        while (1) {
        }
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
