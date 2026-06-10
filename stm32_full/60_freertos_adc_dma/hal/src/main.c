#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL版：FreeRTOS + ADC + DMA。
 *
 * HAL 把 ADC/DMA 寄存器写法变成 handle 和 Init 字段；
 * FreeRTOS 机制仍然是 DMA 回调用 FromISR 通知任务，任务再计算平均值。
 */

#define ADC_BUF_LEN 16U

static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;
static uint16_t g_adc_buf[ADC_BUF_LEN];
static TaskHandle_t g_adc_task_handle;

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void system_clock_72mhz_init(void);
static void gpio_adc_dma_init(void);
static void adc1_init(void);

static void adc_task(void *argument)
{
    (void)argument;

    /*
     * HAL_ADC_Start_DMA() 会启动 ADC 转换和 DMA 搬运。
     * 之后 DMA 循环填充 g_adc_buf，半满/全满时进入 HAL 回调。
     */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buf, ADC_BUF_LEN) != HAL_OK) {
        stop_for_debug();
    }

    while (1) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t sum = 0U;
        for (uint32_t i = 0; i < ADC_BUF_LEN; i++) {
            sum += g_adc_buf[i];
        }

        GPIO_PinState led_state = GPIO_PIN_SET;
        if ((sum / ADC_BUF_LEN) > 2048U) {
            led_state = GPIO_PIN_RESET;
        }

        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, led_state);
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_adc_dma_init();
    adc1_init();

    BaseType_t adc_ok = xTaskCreate(adc_task,
                                    "adc",
                                    192,
                                    NULL,
                                    2,
                                    &g_adc_task_handle);

    if ((adc_ok != pdPASS) || (g_adc_task_handle == NULL)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    BaseType_t woken = pdFALSE;

    if (hadc->Instance == ADC1) {
        /* 半个缓冲已可读：回调只通知任务，不在中断里求平均。 */
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

    /* GPIO_MODE_ANALOG 对应寄存器版清 PA0 MODE/CNF。 */
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
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
        stop_for_debug();
    }

    /*
     * 把 ADC handle 的 DMA_Handle 指针接到 hdma_adc1。
     * 没有这一步，HAL_ADC_Start_DMA() 不知道要驱动哪条 DMA 通道。
     */
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
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        stop_for_debug();
    }

    channel.Channel = ADC_CHANNEL_0;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
        stop_for_debug();
    }
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
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        stop_for_debug();
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
        stop_for_debug();
    }

    periph.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    periph.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
        stop_for_debug();
    }
}

void vApplicationMallocFailedHook(void)
{
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    stop_for_debug();
}
