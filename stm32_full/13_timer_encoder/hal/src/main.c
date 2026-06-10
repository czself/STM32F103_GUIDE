#include "stm32f1xx_hal.h"

/*
 * HAL 版：TIM3 编码器接口。
 *
 * 本课看 HAL 如何把 A/B 相编码器配置映射到底层 TIM 编码器模式。
 * 重点不是 PC13 心跳，而是 TIM_Encoder_InitTypeDef 里的 TI1/TI2 字段。
 */

static TIM_HandleTypeDef htim3;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void tim3_encoder_init(void);
static void error_handler(void);

int main(void)
{
    uint16_t last_count;

    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    tim3_encoder_init();

    last_count = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);

    while (1) {
        uint16_t now_count = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);

        if (now_count != last_count) {
            last_count = now_count;
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
    }
}

static void tim3_encoder_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_Encoder_InitTypeDef enc = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /*
     * PA6/PA7 分别接编码器 A/B 相。
     * GPIO_PULLUP 对应寄存器版里输入上拉，适合常见机械编码器模块。
     */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0U;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFFU;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    /*
     * TIM_ENCODERMODE_TI12 对应寄存器版 encoder mode 3：
     * TI1 和 TI2 都参与计数，硬件根据 A/B 相先后关系判断方向。
     */
    enc.EncoderMode = TIM_ENCODERMODE_TI12;

    /*
     * IC1/IC2 分别描述 A 相和 B 相输入。
     * DIRECTTI 表示通道直接看自己的输入脚：CH1 看 PA6，CH2 看 PA7。
     */
    enc.IC1Polarity = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter = 4U;

    enc.IC2Polarity = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter = 4U;

    if (HAL_TIM_Encoder_Init(&htim3, &enc) != HAL_OK) {
        error_handler();
    }

    /*
     * 启动全部编码器通道。
     * 漏掉 Start 时，TIM3->CNT 不会跟随旋转变化。
     */
    if (HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL) != HAL_OK) {
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
