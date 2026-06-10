#include "stm32f1xx_hal.h"
#include <string.h>

/*
 * 本文件是"HAL版 UART 中断接收实验"。
 *
 * 本课目标：
 * 1. 用 HAL 初始化 USART1 为 115200 8N1
 * 2. 使用 HAL_UART_Receive_IT() 开启单字节中断接收
 * 3. 在回调里只做"收数据 + 置标志 + 重启接收"
 * 4. 在主循环里做命令解析和 LED 控制
 *
 * HAL 中断链路：
 *   main 中调用 HAL_UART_Receive_IT()
 *     → 启动"接收 1 字节"的中断任务
 *     → 使能 CR1.RXNEIE
 *   USART1 收到字节后：
 *     硬件 → USART1_IRQHandler() → HAL_UART_IRQHandler() 分发
 *     → HAL 检测到 RXNE → 读 DR → 调用 HAL_UART_RxCpltCallback()
 *     → 用户在回调中拿到数据、置标志、再次启动接收
 */

/*
 * UART 句柄
 */
static UART_HandleTypeDef huart1;

/*
 * 三变量设计（HAL 中断接收的标准模式）：
 *   g_rx_irq_byte：HAL 直接写入的目标字节（HAL_UART_Receive_IT 的缓冲区）
 *   g_rx_byte：交给主循环处理的"数据交接区"
 *   g_rx_ready："有新数据"标志
 *
 * 为什么不用一个变量？
 *   如果主循环正处理到一半，中断又来了，直接写 g_rx_byte 可能造成数据错乱。
 *   用 g_rx_irq_byte 做 HAL 的缓冲区，g_rx_byte 做主循环的读取区，
 *   中间通过回调交接，逻辑清晰。
 */
static uint8_t g_rx_irq_byte = 0U;
static volatile uint8_t g_rx_byte = 0U;
static volatile uint8_t g_rx_ready = 0U;

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void usart1_gpio_init(void);
static void usart1_init(void);
static void uart1_send_string(const char *str);
static void led_on(void);
static void led_off(void);
static void led_toggle(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * 与 HAL 轮询版的区别：
 *   轮询版：HAL_UART_Receive(&huart1, &ch, 1, 0) 超时 0ms 做非阻塞检查
 *   中断版：HAL_UART_Receive_IT(&huart1, &buf, 1) 启动中断接收，不等
 *
 * HAL_UART_Receive_IT() 做了什么？
 *   1. 检查参数合法性
 *   2. 设置句柄的 pRxBuffPtr、RxXferSize 等内部状态
 *   3. 使能 CR1.RXNEIE（接收中断）
 *   4. 立即返回 HAL_OK（不等数据到来）
 *
 * 一旦有数据到达，RXNE 置位 → 中断触发 → HAL_UART_IRQHandler 分发
 * → 数据被写入 g_rx_irq_byte → 调用 HAL_UART_RxCpltCallback()
 */
int main(void)
{
    uint8_t ch;

    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    usart1_gpio_init();
    usart1_init();

    /*
     * 开启第一轮"接收 1 个字节"的中断任务
     *
     * 注意：g_rx_irq_byte 是 HAL 内部的接收缓冲区地址。
     * HAL_UART_Receive_IT 启动了中断接收流程后，
     * HAL_UART_IRQHandler 在中断中会把收到的数据写到这里。
     */
    if (HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1U) != HAL_OK) {
        error_handler();
    }

    uart1_send_string("\r\n[hal] USART1 interrupt demo ready.\r\n");
    uart1_send_string("Send '1' to LED ON, '0' to LED OFF, 't' to TOGGLE.\r\n");

    while (1) {
        if (g_rx_ready != 0U) {
            /*
             * 取走数据，清标志
             */
            ch = g_rx_byte;
            g_rx_ready = 0U;

            /* 回显 */
            uart1_send_string("RX: ");
            if (HAL_UART_Transmit(&huart1, &ch, 1U, HAL_MAX_DELAY) != HAL_OK) {
                error_handler();
            }
            uart1_send_string("\r\n");

            /* 命令解析 */
            if (ch == '1') {
                led_on();
                uart1_send_string("LED ON\r\n");
            } else if (ch == '0') {
                led_off();
                uart1_send_string("LED OFF\r\n");
            } else if (ch == 't' || ch == 'T') {
                led_toggle();
                uart1_send_string("LED TOGGLE\r\n");
            } else {
                uart1_send_string("Unknown command. Use 1 / 0 / t\r\n");
            }
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

/*
 * led_pc13_init —— HAL 版初始化 PC13 LED
 */
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

/*
 * usart1_gpio_init —— HAL 版配置 USART1 引脚
 */
static void usart1_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * usart1_init —— HAL 版 USART1 初始化（中断模式）
 *
 * 与轮询版的区别：
 *   轮询版：不需要配 NVIC，因为用轮询方式读取 RXNE
 *   中断版：需要配置 NVIC，使能 USART1_IRQn
 *
 * 注意：RXNEIE 不是在 HAL_UART_Init 中使能的，
 * 而是在 HAL_UART_Receive_IT() 中使能的。
 * 所以这里只初始化波特率、模式等基础参数。
 */
static void usart1_init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        error_handler();
    }

    /*
     * ★ 关键：NVIC 配置
     *
     * 使能 USART1 的中断通道，这样 USART1 的中断请求才能到达 CPU。
     * 对应寄存器版：NVIC_EnableIRQ(USART1_IRQn)
     *
     * RXNEIE 的使能在 HAL_UART_Receive_IT() 中完成。
     */
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/*
 * USART1_IRQHandler —— USART1 中断入口
 *
 * HAL 的中断处理模式：
 *   在中断函数中调用 HAL_UART_IRQHandler，
 *   HAL 内部检测 RXNE，读 DR，调用回调。
 *
 * 用户不能在 USART1_IRQHandler 中直接写业务逻辑，
 * 因为 HAL 需要管理内部状态（如 pRxBuffPtr、RxXferCount）。
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/*
 * HAL_UART_RxCpltCallback —— 接收完成回调
 *
 * 当 HAL_UART_IRQHandler 检测到接收完成（指定的字节数已收够），
 * 就会调用这个回调函数。
 *
 * 对应寄存器版 USART1_IRQHandler 中的：
 *   g_rx_byte = USART1->DR;
 *   g_rx_ready = 1;
 *
 * 参数 huart：触发回调的 UART 句柄
 *
 * ★ 关键：为什么回调中要再次调用 HAL_UART_Receive_IT？
 *   HAL_UART_Receive_IT(&huart1, &buf, 1) 的意思是"接收 1 个字节"。
 *   收完 1 个字节后，本次中断接收任务就完成了。
 *   如果不再次调用，后续来的数据不会再进入中断接收流程。
 *
 *   相当于寄存器版中每次中断后都要重新"设 RXNEIE"——但 HAL 自动处理了这步。
 *   这里的再次调用，是为了重新开启一轮"接收 1 个字节"的任务。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        /*
         * 把收到的字节交给主循环处理
         */
        g_rx_byte = g_rx_irq_byte;
        g_rx_ready = 1U;

        /*
         * ★ 关键：重新启动下一轮中断接收
         *
         * 如果不做这步，后续数据不会再进入中断接收流程。
         * 这就是所谓的"单字节中断接收需要每次重启"。
         *
         * 思考：如果改成接收 10 个字节（HAL_UART_Receive_IT(&huart1, buf, 10)），
         * 是不是每收 10 个字节才进一次回调？
         * 是的——这就是"定长中断接收"。
         */
        if (HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1U) != HAL_OK) {
            error_handler();
        }
    }
}

/*
 * uart1_send_string —— 发送字符串（轮询）
 */
static void uart1_send_string(const char *str)
{
    if (HAL_UART_Transmit(&huart1, (uint8_t *)str, (uint16_t)strlen(str), HAL_MAX_DELAY) != HAL_OK) {
        error_handler();
    }
}

/* LED 控制 */
static void led_on(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

static void led_off(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

static void led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
