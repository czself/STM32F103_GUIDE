#include "stm32f1xx_hal.h"

/*
 * HAL 版：ADC 多通道扫描。
 *
 * 本课重点看 HAL 的 Rank 如何对应寄存器版 SQR3 的规则组顺序。
 * Rank 1 先读到，Rank 2 后读到；变量赋值顺序不能乱。
 */

static ADC_HandleTypeDef hadc1;
static volatile uint16_t g_adc0;
static volatile uint16_t g_adc1;

static void system_clock_72mhz_init(void);
static void pc13_led_init(void);
static void adc_scan_init(void);
static uint16_t adc_poll_and_read(void);
static void error_handler(void);

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    pc13_led_init();
    adc_scan_init();

    while (1) {
        /*
         * 连续扫描模式下，HAL_ADC_GetValue() 每次取出当前 Rank 的结果。
         * 这里的读取顺序必须和 adc_scan_init() 里配置的 Rank 一致：
         *   第一次读 Rank 1，也就是 PA0 / ADC_CHANNEL_0
         *   第二次读 Rank 2，也就是 PA1 / ADC_CHANNEL_1
         */
        g_adc0 = adc_poll_and_read();
        g_adc1 = adc_poll_and_read();

        /*
         * 与寄存器版保持同一个可观察现象：
         * PA0 的电压高于 PA1 时翻转 LED，否则保持当前状态。
         */
        if (g_adc0 > g_adc1) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }

        HAL_Delay(300);
    }
}

static void adc_scan_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_ChannelConfTypeDef ch = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);

    hadc1.Instance = ADC1;
    /*
     * ScanConvMode = ENABLE 对应寄存器版 CR1.SCAN = 1。
     * 它让 ADC 不再只转换一个通道，而是按 Rank 顺序执行规则组。
     */
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;

    /*
     * ContinuousConvMode = ENABLE 对应寄存器版 CR2.CONT = 1。
     * 启动一次后，ADC 会持续按 Rank 1 -> Rank 2 -> Rank 1... 循环扫描。
     */
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;

    /*
     * NbrOfConversion 对应规则组长度。
     * 这里写 2，等价于寄存器版 SQR1.L = 1。
     */
    hadc1.Init.NbrOfConversion = 2U;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        error_handler();
    }

    /*
     * Rank 1 对应规则组第 1 次转换：PA0 / ADC_CHANNEL_0。
     */
    ch.Channel = ADC_CHANNEL_0;
    ch.Rank = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) {
        error_handler();
    }

    /*
     * Rank 2 对应规则组第 2 次转换：PA1 / ADC_CHANNEL_1。
     */
    ch.Channel = ADC_CHANNEL_1;
    ch.Rank = ADC_REGULAR_RANK_2;

    if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) {
        error_handler();
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
        error_handler();
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        error_handler();
    }
}

static uint16_t adc_poll_and_read(void)
{
    /*
     * PollForConversion 等待“当前 Rank”转换完成。
     * 在多通道扫描里，它不是一次等完整个规则组，
     * 而是每完成一个 Rank 就可以读取一次 DR。
     */
    if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK) {
        error_handler();
    }

    /*
     * GetValue 对应读取 ADC1->DR。
     * 调用两次就会按 Rank 顺序取到 PA0、PA1 两个结果。
     */
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
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
