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

static ADC_HandleTypeDef hadc1;
static volatile uint16_t g_adc0, g_adc1;
static void adc_scan_init(void)
{
    GPIO_InitTypeDef gpio = {0}; ADC_ChannelConfTypeDef ch = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_ADC1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1; gpio.Mode = GPIO_MODE_ANALOG; HAL_GPIO_Init(GPIOA, &gpio);
    hadc1.Instance = ADC1; hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE; hadc1.Init.ContinuousConvMode = ENABLE; hadc1.Init.DiscontinuousConvMode = DISABLE; hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START; hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT; hadc1.Init.NbrOfConversion = 2; HAL_ADC_Init(&hadc1);
    ch.Channel = ADC_CHANNEL_0; ch.Rank = ADC_REGULAR_RANK_1; ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5; HAL_ADC_ConfigChannel(&hadc1, &ch);
    ch.Channel = ADC_CHANNEL_1; ch.Rank = ADC_REGULAR_RANK_2; HAL_ADC_ConfigChannel(&hadc1, &ch);
    HAL_ADCEx_Calibration_Start(&hadc1); HAL_ADC_Start(&hadc1);
}
int main(void){ HAL_Init(); system_clock_72mhz_init(); pc13_led_init(); adc_scan_init(); while(1){ HAL_ADC_PollForConversion(&hadc1,10); g_adc0=HAL_ADC_GetValue(&hadc1); HAL_ADC_PollForConversion(&hadc1,10); g_adc1=HAL_ADC_GetValue(&hadc1); HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13); HAL_Delay(300); } }
