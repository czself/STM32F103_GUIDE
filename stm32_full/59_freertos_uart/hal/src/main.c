#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/*
 * HAL版：FreeRTOS + USART1 中断队列。
 *
 * HAL 把 RXNE/DR 的细节封装进 HAL_UART_IRQHandler() 和回调函数。
 * RTOS 边界不变：回调里只入队并重新启动下一字节接收，uart_task 再回显和解析。
 */

static UART_HandleTypeDef huart1;
static QueueHandle_t g_uart_queue;
static uint8_t g_rx_byte;

static void stop_for_debug(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

static void system_clock_72mhz_init(void);
static void gpio_uart_init(void);
static void uart1_init(void);

static void uart_task(void *argument)
{
    uint8_t byte;

    (void)argument;

    /*
     * HAL_UART_Receive_IT() 只安排“一次 1 字节中断接收”。
     * 如果回调里不重新调用，HAL 版常见现象就是只能收到第一个字节。
     */
    if (HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1U) != HAL_OK) {
        stop_for_debug();
    }

    while (1) {
        if (xQueueReceive(g_uart_queue, &byte, portMAX_DELAY) == pdPASS) {
            (void)HAL_UART_Transmit(&huart1, &byte, 1U, 20U);

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

    BaseType_t uart_ok = xTaskCreate(uart_task,
                                     "uart",
                                     192,
                                     NULL,
                                     2,
                                     NULL);

    if ((g_uart_queue == NULL) || (uart_ok != pdPASS)) {
        stop_for_debug();
    }

    vTaskStartScheduler();

    stop_for_debug();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;

    if (huart->Instance == USART1) {
        /*
         * 这是 HAL 版的 ISR 边界：回调仍处在中断上下文。
         * 因此用 FromISR 队列 API，不在这里解析 t/T，也不做阻塞发送。
         */
        (void)xQueueSendFromISR(g_uart_queue, &g_rx_byte, &woken);

        /*
         * 重新启动下一次 1 字节接收。
         * 这一步漏掉时，串口通常表现为“只响应第一个字节”。
         */
        (void)HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1U);
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

    /*
     * GPIO_MODE_AF_PP 对应寄存器版 PA9 的复用推挽输出，
     * 也就是 USART1_TX 把串口波形从 PA9 输出。
     */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
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

    /*
     * 这些字段最终会配置 BRR、CR1 等 USART 寄存器：
     * 115200、8 数据位、无校验、1 停止位，和 platformio monitor_speed 对齐。
     */
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        stop_for_debug();
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
}

void vApplicationMallocFailedHook(void)
{
    /* UART 队列、任务控制块和任务栈分配失败会停在这里。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    stop_for_debug();
}
