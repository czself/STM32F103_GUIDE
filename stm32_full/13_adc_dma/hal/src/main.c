#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL版 ADC + DMA 缓冲区采样实验"。
 *
 * 本课目标：
 * 1. 用 HAL 初始化 ADC1 与 DMA1_Channel1
 * 2. 让 HAL_ADC_Start_DMA() 把 ADC 数据持续搬到一个长度为 16 的缓冲区
 * 3. 主循环对缓冲区求平均，再控制板载 LED
 *
 * 与上一课（12_dma_basic HAL 版）的关键区别：
 *   上一课：MemInc = DISABLE（目标是单个变量）
 *   本课：MemInc = ENABLE（目标是数组，需要地址自增）
 *
 *   上一课：Start_DMA 长度 = 1
 *   本课：Start_DMA 长度 = 16（数组大小）
 */

#define ADC_BUFFER_SIZE 16U

static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;

/*
 * 这里不再是单个变量，而是采样缓冲区。
 * DMA 会不断往这个数组里写数据。
 * volatile 防止编译器优化掉对它的缓存。
 */
static volatile uint16_t g_adc_buffer[ADC_BUFFER_SIZE] = {0};

static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void pa1_adc_input_init(void);
static void dma1_channel1_init(void);
static void adc1_init(void);
static uint16_t adc_buffer_average_get(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * 与上一课 HAL 版的主要变化：
 *   1. dma1_channel1_init 中 MemInc = ENABLE（上一课是 DISABLE）
 *   2. Start_DMA 的目标地址是数组，长度是 16（上一课是变量，长度 1）
 *   3. 主循环调用 adc_buffer_average_get() 求平均
 */
int main(void)
{
    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    dma1_channel1_init();
    adc1_init();

    /*
     * 这一句和上一课相比，最关键的变化有两点：
     * 1. 目标地址是数组首地址 g_adc_buffer（不再是 &g_adc_value）
     * 2. 长度是 ADC_BUFFER_SIZE = 16（不再是 1）
     *
     * HAL_ADC_Start_DMA 内部会根据长度配置 DMA 的 CNDTR，
     * 并根据 MemInc 配置决定是否自增内存地址。
     */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buffer, ADC_BUFFER_SIZE) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 对 16 个采样值求平均，用平均值控制 LED。
         * DMA 在后台不断刷新 g_adc_buffer 的内容。
         */
        uint16_t avg_value = adc_buffer_average_get();

        if (avg_value > 2048U) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
    }
}

/*
 * system_clock_72mhz_init —— HAL 版时钟配置
 */
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

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) error_handler();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) error_handler();
}

static void led_pc13_init(void)
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

static void pa1_adc_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * dma1_channel1_init —— HAL 版 DMA 初始化（缓冲区模式）
 *
 * 与上一课相比的唯一变化：
 *   MemInc = DMA_MINC_ENABLE（不再是 DISABLE）
 *
 * 为什么需要 MemInc = ENABLE？
 *   目标是一个数组，DMA 需要每搬一个数据就自动指向下一个数组元素。
 *   如果不自增，16 个采样值都会覆盖数组的第一个元素。
 *
 * 对应寄存器版：CCR.MINC = 1
 */
static void dma1_channel1_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;

    /*
     * ★ 与上一课的唯一区别：
     *   MemInc = ENABLE（内存地址自增）
     *   对应寄存器版：CCR.MINC = 1
     */
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;

    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) error_handler();

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
}

/*
 * adc1_init —— HAL 版 ADC1 初始化
 *
 * 与上一课完全相同：连续转换 + DMA 请求使能
 */
static void adc1_init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) error_handler();

    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) error_handler();
}

/*
 * adc_buffer_average_get —— 求缓冲区平均值
 *
 * 与寄存器版相同：用 uint32_t 求和避免溢出。
 */
static uint16_t adc_buffer_average_get(void)
{
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < ADC_BUFFER_SIZE; i++) {
        sum += g_adc_buffer[i];
    }

    return (uint16_t)(sum / ADC_BUFFER_SIZE);
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}