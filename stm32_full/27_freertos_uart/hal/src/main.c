#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * ============================================================================
 * HAL 版 FreeRTOS + UART 中断
 * ============================================================================
 *
 * ██████  HAL UART 中断流程 ██████
 *
 * 1. HAL 初始化阶段
 *    - HAL_UART_Init()：配置 BRR/CR1 等，但不开启接收中断
 *    - HAL_UART_Receive_IT()：开启接收中断（使能 RXNEIE + 启动第一次接收）
 *
 * 2. 中断触发流程
 *    - UART 收到字节 → RXNE 置位 → CPU 进入 USART1_IRQHandler
 *    - USART1_IRQHandler() 调用 HAL_UART_IRQHandler()
 *    - HAL_UART_IRQHandler() 内部：读 DR → 清 RXNE → 调用 HAL_UART_RxCpltCallback()
 *    - 在回调中做业务：投递队列 + 重新调用 HAL_UART_Receive_IT()
 *
 * 3. 重新调用 HAL_UART_Receive_IT() 的重要性
 *    - HAL 在中断完成回调后会关闭接收中断
 *    - 如果不在回调里重新调用，后续字节将无法触发中断
 *    - 这是 HAL 的设计特点（区别于裸机直接用 RXNEIE）
 *
 * 4. HAL_UART_Transmit() —— 同步发送
 *    - 阻塞发送，会等待 TXE 标志
 *    - 在任务中调用没问题，但如果要发送大量数据，建议也用中断或 DMA
 *
 * 5. 中断优先级配置
 *    - USART1 优先级 6（符合 FreeRTOS 的 FromISR 要求：数字 ≥ 5）
 *    - 优先级 6 比 FreeRTOS 管理的任务调度优先级更低
 *    - 这确保了 xQueueSendFromISR() 能安全执行
 */

static UART_HandleTypeDef huart1;
static QueueHandle_t g_uart_queue;
static uint8_t g_rx_byte;

static void system_clock_72mhz_init(void);
static void gpio_uart_init(void);
static void uart1_init(void);

static void uart_task(void *argument)
{
    uint8_t byte;
    (void)argument;

    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

    while (1) {
        if (xQueueReceive(g_uart_queue, &byte, portMAX_DELAY) == pdPASS) {
            HAL_UART_Transmit(&huart1, &byte, 1, 20);
            if ((byte == 't') || (byte == 'T')) {
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            }
        }
    }
}

int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    gpio_uart_init();
    uart1_init();

    g_uart_queue = xQueueCreate(32, sizeof(uint8_t));
    xTaskCreate(uart_task, "uart", 192, NULL, 2, NULL);
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

static void gpio_uart_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

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

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {}
}
