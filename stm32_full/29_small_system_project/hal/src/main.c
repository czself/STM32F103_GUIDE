#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * HAL 版小系统综合项目
 * ============================================================================
 *
 * 本文件将 LED、按键、ADC、UART 整合到一个 FreeRTOS 多任务系统中。
 * HAL 负责外设初始化，FreeRTOS 负责任务调度。
 *
 * 系统数据流：
 *   按键 (PB0) → key_task → EVENT_KEY → g_event_queue → control_task → LED + 串口
 *   串口 (PA9/10) → USART1 ISR → g_uart_queue → uart_task → 解析 → g_event_queue
 *   ADC (PA0) → adc_task → g_adc_value → uart_task / status_task → 串口打印
 *
 * 和寄存器版的区别：
 *   - GPIO 配置用 HAL_GPIO_Init() 而不是手动操作 CRL/CRH
 *   - UART 用 HAL_UART_Init() + HAL_UART_Transmit()
 *   - ADC 用 HAL_ADC_Start() + HAL_ADC_PollForConversion()
 *   - 队列/任务 API 完全相同
 *
 * 注意：HAL 的 SysTick_Handler 被 FreeRTOSConfig.h 重映射为 xPortSysTickHandler
 * SysTick 同时服务于 HAL 的 HAL_IncTick() 和 FreeRTOS 的 Tick 调度。
 */

enum { EVENT_KEY = 1, EVENT_UART_TOGGLE = 2 };

static ADC_HandleTypeDef hadc1;
static UART_HandleTypeDef huart1;
static QueueHandle_t g_event_queue;
static QueueHandle_t g_uart_queue;
static volatile uint16_t g_adc_value;
static uint8_t g_rx_byte;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void adc1_init(void);
static void uart1_init(void);

static void uart_write_str(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

static void uart_write_u16(uint16_t value)
{
    char buf[6];
    int i = 0;
    if (value == 0U) {
        uint8_t c = '0';
        HAL_UART_Transmit(&huart1, &c, 1, 20);
        return;
    }
    while (value > 0U) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (i > 0) {
        uint8_t c = (uint8_t)buf[--i];
        HAL_UART_Transmit(&huart1, &c, 1, 20);
    }
}

static void adc_task(void *argument)
{
    (void)argument;
    while (1) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 20);
        g_adc_value = (uint16_t)HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void key_task(void *argument)
{
    uint8_t last = 0U;
    (void)argument;
    while (1) {
        uint8_t now = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET);
        if ((now != 0U) && (last == 0U)) {
            uint8_t event = EVENT_KEY;
            xQueueSend(g_event_queue, &event, 0);
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void control_task(void *argument)
{
    uint8_t event;
    (void)argument;
    while (1) {
        if (xQueueReceive(g_event_queue, &event, portMAX_DELAY) == pdPASS) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            uart_write_str(event == EVENT_KEY ? "key\r\n" : "toggle\r\n");
        }
    }
}

static void uart_task(void *argument)
{
    uint8_t byte;
    (void)argument;
    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
    uart_write_str("system ready\r\n");
    while (1) {
        if (xQueueReceive(g_uart_queue, &byte, portMAX_DELAY) == pdPASS) {
            if ((byte == 't') || (byte == 'T')) {
                uint8_t event = EVENT_UART_TOGGLE;
                xQueueSend(g_event_queue, &event, 0);
            } else if ((byte == 's') || (byte == 'S')) {
                uart_write_str("adc=");
                uart_write_u16(g_adc_value);
                uart_write_str("\r\n");
            }
        }
    }
}

static void status_task(void *argument)
{
    (void)argument;
    while (1) {
        uart_write_str("adc=");
        uart_write_u16(g_adc_value);
        uart_write_str("\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();
    adc1_init();
    uart1_init();

    g_event_queue = xQueueCreate(8, sizeof(uint8_t));
    g_uart_queue = xQueueCreate(32, sizeof(uint8_t));

    xTaskCreate(adc_task, "adc", 160, NULL, 2, NULL);
    xTaskCreate(key_task, "key", 128, NULL, 2, NULL);
    xTaskCreate(control_task, "ctrl", 192, NULL, 2, NULL);
    xTaskCreate(uart_task, "uart", 224, NULL, 2, NULL);
    xTaskCreate(status_task, "stat", 192, NULL, 1, NULL);
    vTaskStartScheduler();

    while (1) {}
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;
    if (huart->Instance == USART1) {
        xQueueSendFromISR(g_uart_queue, &g_rx_byte, &woken);
        HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
    }
    portYIELD_FROM_ISR(woken);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(USART1_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void adc1_init(void)
{
    ADC_ChannelConfTypeDef channel = {0};
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
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

static void uart1_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
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
