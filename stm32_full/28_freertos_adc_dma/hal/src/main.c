#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ============================================================================
 * HAL 版 FreeRTOS + ADC + DMA
 * ============================================================================
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                                  | 底层寄存器操作                  |
 * |-----------------------------------------|---------------------------------|
 * | HAL_ADC_Init()                          | 配置 ADC_CR1/CR2/SMPR/SQR       |
 * | HAL_ADC_ConfigChannel()                 | 配置通道号、序列位置、采样时间    |
 * | HAL_ADC_Start_DMA(&hadc, buf, len)      | ADC_CR2.DMA=1 + DMA 配置        |
 * | HAL_ADC_ConvHalfCpltCallback()          | DMA 半满中断（HTIF）             |
 * | HAL_ADC_ConvCpltCallback()              | DMA 全满中断（TCIF）             |
 * | __HAL_LINKDMA(&hadc, DMA_Handle, hdma)  | 把 ADC 句柄和 DMA 句柄关联       |
 *
 * ██████  DMA 缓冲区工作原理（循环模式）████
 *
 * 缓冲区数组 g_adc_buf[16] 被分成两半：
 *   前半部分 [0]~[7] 和后半部分 [8]~[15]
 *
 * DMA 按序搬运：
 *   → 搬 [0]~[7]  → 触发半满中断（HTIF）→ 通知任务处理前半
 *   → 搬 [8]~[15] → 触发全满中断（TCIF）→ 通知任务处理后半
 *   → 重新从 [0] 开始（循环模式）
 *
 * 任务在中断回调中被通知，醒来后计算整个缓冲区的平均值。
 *
 * ██████  __HAL_LINKDMA() 的作用 █████
 *
 * 宏 __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)
 * 把 ADC 句柄中的 DMA_Handle 指针指向 DMA 句柄。
 * 这样 HAL_ADC_Start_DMA() 就知道要把 DMA 配置到哪个通道。
 */

#define ADC_BUF_LEN 16U

static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;
static uint16_t g_adc_buf[ADC_BUF_LEN];
static TaskHandle_t g_adc_task_handle;

static void system_clock_72mhz_init(void);
static void gpio_adc_dma_init(void);
static void adc1_init(void);

static void adc_task(void *argument)
{
    (void)argument;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buf, ADC_BUF_LEN);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t sum = 0U;
        for (uint32_t i = 0; i < ADC_BUF_LEN; i++) {
            sum += g_adc_buf[i];
        }

        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
                          (sum / ADC_BUF_LEN) > 2048U ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_adc_dma_init();
    adc1_init();

    xTaskCreate(adc_task, "adc", 192, NULL, 2, &g_adc_task_handle);
    vTaskStartScheduler();

    while (1) {}
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    BaseType_t woken = pdFALSE;
    if (hadc->Instance == ADC1) {
        vTaskNotifyGiveFromISR(g_adc_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    BaseType_t woken = pdFALSE;
    if (hadc->Instance == ADC1) {
        vTaskNotifyGiveFromISR(g_adc_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

static void gpio_adc_dma_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

static void adc1_init(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);

    channel.Channel = ADC_CHANNEL_0;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &channel);
}

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};
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
    periph.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    periph.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    HAL_RCCEx_PeriphCLKConfig(&periph);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {}
}
