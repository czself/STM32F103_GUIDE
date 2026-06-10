#include "stm32f1xx_hal.h"
#include <string.h>

/*
 * 本文件是"HAL版 UART 轮询收发实验"。
 *
 * 本课目标：
 * 1. 用 HAL 初始化 USART1 为 115200 8N1
 * 2. 使用 HAL_UART_Transmit / HAL_UART_Receive 做轮询收发
 * 3. 用串口命令控制板载 LED
 *
 * 与寄存器版的对应关系：
 *   HAL_UART_Transmit()    → 内部轮询 TXE，写 DR
 *   HAL_UART_Receive()     → 内部轮询 RXNE，读 DR
 *   UART_HandleTypeDef     → 封装了 Instance、Init 等配置
 *   HAL_UART_Init()        → 写 BRR、CR1(TE/RE/UE) 等寄存器
 *
 * 理解寄存器版有助于理解 HAL 函数内部在做什么。
 * 理解 HAL 版有助于在实际项目中快速开发。
 */

/*
 * UART 句柄变量
 *
 * UART_HandleTypeDef 包含了：
 *   Instance：外设基地址（如 USART1）
 *   Init：初始化参数（波特率、数据位、停止位、校验等）
 *   Lock：锁状态（防止重入）
 *   State：当前状态（就绪、忙等）
 */
static UART_HandleTypeDef huart1;

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
 * HAL 版串口轮询的三部曲：
 *   HAL_UART_Transmit() → 发送（内部轮询 TXE）
 *   HAL_UART_Receive()  → 接收（内部轮询 RXNE，带超时）
 *
 * 与寄存器版的区别：
 *   寄存器版：直接操作 SR、DR、BRR、CR1
 *   HAL 版：通过句柄 + HAL 函数完成，不需要手算 BRR
 */
int main(void)
{
    uint8_t ch;

    /*
     * HAL_Init() 初始化 HAL 库（Flash 预取 + SysTick）
     */
    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    usart1_gpio_init();
    usart1_init();

    uart1_send_string("\r\n[hal] USART1 polling demo ready.\r\n");
    uart1_send_string("Send '1' to LED ON, '0' to LED OFF, 't' to TOGGLE.\r\n");

    while (1) {
        /*
         * HAL_UART_Receive() —— 轮询接收
         *
         * 参数说明：
         *   第 1 个参数：UART 句柄指针
         *   第 2 个参数：接收缓冲区指针（存放收到的字节）
         *   第 3 个参数：要接收的字节数（这里每次收 1 个字节）
         *   第 4 个参数：超时时间（单位 ms）
         *
         * 超时设为 0 的含义：
         *   立即检查 RXNE，有数据就返回 HAL_OK，
         *   没数据就立即返回 HAL_TIMEOUT。
         *   这样实现了"非阻塞轮询"的效果。
         *
         * 对应寄存器版：
         *   if (USART1->SR & USART_SR_RXNE) {
         *       ch = USART1->DR;
         *   }
         */
        if (HAL_UART_Receive(&huart1, &ch, 1U, 0U) == HAL_OK) {
            /*
             * 回显收到的字符
             */
            uart1_send_string("RX: ");
            HAL_UART_Transmit(&huart1, &ch, 1U, HAL_MAX_DELAY);
            uart1_send_string("\r\n");

            /*
             * 命令解析
             */
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
 *
 * PA9 → USART1_TX：复用推挽输出（GPIO_MODE_AF_PP）
 * PA10 → USART1_RX：输入模式（GPIO_MODE_INPUT）
 *
 * 注意 HAL 中不需要显式开 AFIO 时钟，
 * __HAL_RCC_USART1_CLK_ENABLE() 会处理相关时钟。
 */
static void usart1_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 开启 GPIOA 时钟
     * HAL 中开时钟都用 __HAL_RCC_xxx_CLK_ENABLE() 宏
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * PA9 → USART1_TX
     *
     * GPIO_MODE_AF_PP（复用推挽输出）：
     *   引脚输出由 USART1 外设控制，而非 GPIO 寄存器。
     *   对应寄存器版：MODE9=11, CNF9=10
     *
     * Speed = HIGH：
     *   串口在 115200 波特率下频率约 115kHz，
     *   LOW 速度也够用，但配成 HIGH 更安全。
     */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PA10 → USART1_RX
     *
     * GPIO_MODE_INPUT：输入模式
     *   对应寄存器版：MODE10=00, CNF10=01（浮空输入）
     *
     * Pull = NOPULL：
     *   不指定上下拉，让外部信号自然驱动。
     */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * usart1_init —— HAL 版 USART1 初始化
 *
 * HAL 不需要手算 BRR——HAL 会根据 BaudRate 和时钟频率自动计算。
 * HAL_UART_Init() 内部会：
 *   1. 根据 PCLK2 频率和 BaudRate 算出 BRR 值并写入
 *   2. 配置数据位、停止位、校验位
 *   3. 使能 TE、RE、UE
 *
 * 对应的寄存器操作：
 *   USART1->BRR = HAL 自动计算的值
 *   USART1->CR1 = TE | RE
 *   USART1->CR1 |= UE
 */
static void usart1_init(void)
{
    /*
     * 开启 USART1 时钟
     */
    __HAL_RCC_USART1_CLK_ENABLE();

    /*
     * 填充句柄的 Init 成员
     *
     * Instance：指定外设 = USART1
     *
     * BaudRate：115200
     *
     * WordLength：UART_WORDLENGTH_8B → 8 位数据位
     *
     * StopBits：UART_STOPBITS_1 → 1 位停止位
     *
     * Parity：UART_PARITY_NONE → 无校验
     *
     * Mode：UART_MODE_TX_RX → 同时使能发送和接收
     *
     * HwFlowCtl：UART_HWCONTROL_NONE → 无硬件流控（不用 RTS/CTS）
     *
     * OverSampling：UART_OVERSAMPLING_16 → 16 倍过采样（标准模式）
     */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    /*
     * HAL_UART_Init() 内部做了什么？
     *   1. 如果定义了 HAL_UART_MspInit 回调，调用它（可在其中配 GPIO/NVIC）
     *   2. 写 BRR 寄存器（自动计算）
     *   3. 写 CR1（配置数据位、校验、TE、RE，但不设 UE）
     *   4. 写 CR2（配置停止位）
     *   5. 写 CR3（配置流控）
     *   6. CR1 的 UE = 1（开启 USART）
     *
     * 返回值：HAL_OK 或 HAL_ERROR
     */
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        error_handler();
    }
}

/*
 * uart1_send_string —— HAL 版发送字符串
 *
 * HAL_UART_Transmit 参数：
 *   句柄、数据缓冲区、数据长度、超时时间
 *
 * HAL_MAX_DELAY：最大超时（约 4 秒），基本等于"一直等到发完"。
 * 对于发送来说，轮询等待 TXE 是合理的——数据发不完程序就不该继续。
 *
 * 对应寄存器版：
 *   while (*str) {
 *       while (!TXE);
 *       DR = *str++;
 *   }
 */
static void uart1_send_string(const char *str)
{
    /*
     * HAL_UART_Transmit 内部轮询 TXE，直到所有字节发送完毕。
     * 如果发送过程中出错（如硬件故障），返回 HAL_ERROR。
     */
    if (HAL_UART_Transmit(&huart1, (uint8_t *)str, (uint16_t)strlen(str), HAL_MAX_DELAY) != HAL_OK) {
        error_handler();
    }
}

/* LED 控制辅助函数 */
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
    /*
     * HAL_GPIO_TogglePin 是 HAL 提供的翻转函数，
     * 内部读取 ODR 再写入 BSRR，与寄存器版 led_toggle 逻辑相同。
     */
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

/*
 * error_handler —— 错误处理
 */
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
