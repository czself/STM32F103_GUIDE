#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 UART 轮询收发实验"。
 *
 * 本课目标：
 * 1. 配置 USART1 工作在 115200 8N1
 * 2. 使用轮询方式发送和接收数据
 * 3. 通过串口命令控制板载 LED
 * 4. 建立对 TXE、RXNE、DR、BRR 的直观理解
 *
 * UART 通信本质：
 *   串行通信就是"一根线逐位传输数据"。
 *   TX 发、RX 收，双方约定好速度（波特率）和格式（8N1）。
 *   CPU 通过 DR 寄存器与串口交换数据。
 *
 * 本课为什么叫"轮询"？
 *   发送时：CPU 不断检查 TXE 标志，直到可以发下一个字节。
 *   接收时：CPU 不断检查 RXNE 标志，看有没有新数据。
 *   CPU 主动去问、去等——这就是轮询。
 *
 * USART vs UART：
 *   USART = Universal Synchronous/Asynchronous Receiver Transmitter
 *   UART  = Universal Asynchronous Receiver Transmitter（异步模式）
 *   STM32F103 的外设名叫 USART，但我们只使用异步模式。
 *   所以平时可以说"配置 UART"或"使用 USART1"。
 */

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void usart1_gpio_init(void);
static void usart1_init(void);
static void usart1_send_byte(uint8_t byte);
static void usart1_send_string(const char *str);
static uint8_t usart1_receive_byte_nonblocking(uint8_t *byte);
static void led_on(void);
static void led_off(void);
static void led_toggle(void);

/*
 * system_clock_72mhz_init —— 配置系统时钟到 72MHz
 *
 * 特别说明 USART1 的时钟来源：
 *   USART1 挂在 APB2 总线上（与 ADC1 相同）。
 *   当前 PCLK2 = 72MHz，这个频率用于计算 USART1 的波特率。
 *   USART2 和 USART3 挂在 APB1 上（PCLK1 = 36MHz）。
 *   不同 USART 的时钟源不同，计算 BRR 时要特别注意。
 */
static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

/*
 * led_pc13_init —— 初始化 PC13 LED
 */
static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

/*
 * usart1_gpio_init —— 配置 USART1 的 TX/RX 引脚
 *
 * USART1 的引脚映射（F103 的默认映射，没有重映射）：
 *   PA9  → USART1_TX（发送）
 *   PA10 → USART1_RX（接收）
 *
 * PA9（TX）为什么配成"复用推挽输出"？
 *   普通推挽输出（MODE=10, CNF=00）：CPU 通过 GPIO 寄存器直接控制引脚电平。
 *   复用推挽输出（MODE=10, CNF=10）：引脚的输出信号由 USART1 外设控制。
 *   这里 PA9 需要由 USART1 自动发送数据，所以必须用复用功能。
 *
 * PA10（RX）为什么配成"浮空输入"？
 *   接收引脚是输入方向，外部信号（USB 转串口模块的 TX）送进来。
 *   浮空输入意味着没有内部上拉/下拉干扰信号。
 *   不需要复用功能配置——USART1 的接收器会自动读取引脚电平。
 */
static void usart1_gpio_init(void)
{
    /*
     * 第 1 步：开启 GPIOA、AFIO、USART1 时钟
     *
     * USART1、GPIOA、AFIO 都挂在 APB2 总线上。
     * IOPAEN（bit 2）：GPIOA 时钟
     * AFIOEN（bit 0）：复用功能 I/O 时钟
     * USART1EN（bit 14）：USART1 时钟
     *
     * 注意：使用 USART，GPIO 复用功能输出也需要 AFIO 时钟吗？
     *   对于 F103，开启 AFIO 时钟是标准做法。
     *   虽然部分场景下不开启也可能工作，但为了保证稳定，建议打开。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /*
     * 第 2 步：PA9 → USART1_TX → 复用推挽输出
     *
     * PA9 是高 8 位引脚（8~15），在 CRH 寄存器中配置。
     * CRH 中每隔 4 位控制一个引脚：
     *   bit 4~7   → CNF9[1:0] + MODE9[1:0]
     *
     * 目标配置：
     *   MODE9 = 11 → 输出模式，最大速度 50MHz（串口需要较高的翻转速度）
     *   CNF9  = 10 → 复用推挽输出（Alternate Function Push-Pull）
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOA->CRH |= GPIO_CRH_MODE9;                   /* MODE9 = 11 */
    GPIOA->CRH |= GPIO_CRH_CNF9_1;                   /* CNF9 = 10, CNF9_1 = 1 */

    /*
     * 第 3 步：PA10 → USART1_RX → 浮空输入
     *
     * MODE10 = 00 → 输入模式
     * CNF10  = 01 → 浮空输入（CNF10_0 = 1, CNF10_1 = 0）
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_CNF10_0;                  /* CNF10 = 01 → 浮空输入 */
}

/*
 * usart1_init —— 初始化 USART1 为 115200 8N1
 *
 * 本课初始化流程：
 *   1. 设置波特率（BRR 寄存器）
 *   2. 使能发送器（TE）、接收器（RE）
 *   3. 开启 USART（UE）
 *
 * 关于 BRR 的计算：
 *   BRR 的值由 USART 时钟和目标波特率决定。
 *   USART1 时钟 = PCLK2 = 72MHz
 *   目标波特率 = 115200
 *   采样模式 = 16 倍过采样（默认）
 *
 *   公式：USARTDIV = 时钟 / (16 × 目标波特率)
 *                 = 72,000,000 / (16 × 115200)
 *                 = 72,000,000 / 1,843,200
 *                 = 39.0625
 *
 *   BRR 编码：
 *     整数部分 DIV_Mantissa = 39 = 0x27
 *     小数部分 DIV_Fraction = 0.0625 × 16 = 1 = 0x1
 *     BRR = (Mantissa << 4) | Fraction
 *         = (39 << 4) | 1
 *         = 624 + 1
 *         = 0x0271
 *
 *   验证：实际波特率 = 72MHz / (16 × (39 + 1/16))
 *                    = 72MHz / (16 × 39.0625)
 *                    ≈ 115200 ✓
 *
 * CR1 寄存器相关位：
 *   UE（bit 13）：USART 总使能，必须先配好其他位再开 UE
 *   TE（bit 3）：发送器使能
 *   RE（bit 2）：接收器使能
 */
static void usart1_init(void)
{
    /*
     * 第 1 步：设置波特率寄存器 BRR
     *
     * 注意：BRR 寄存器的值取决于 USART 的时钟频率。
     * 如果将来改动了时钟配置（如 PCLK2 不再是 72MHz），
     * BRR 的值必须重新计算！
     */
    USART1->BRR = 0x0271U;

    /*
     * 第 2 步：配置 CR1 控制寄存器
     *
     * 这里先配置 TE 和 RE，但先不开 UE（USART 总使能）。
     * 原因是有些寄存器需要在 UE=0 时才能修改。
     * 先配其他位，最后再 UE=1 开启，这是标准做法。
     *
     * TE = 1：发送器使能。USART1 的 TX 引脚开始正常工作。
     * RE = 1：接收器使能。USART1 的 RX 引脚开始监听数据。
     */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE;

    /*
     * 第 3 步：开启 USART（UE = 1）
     *
     * UE = USART Enable（bit 13）。
     * 这是 USART 模块的总开关。
     * 没有 UE=1，TE 和 RE 的配置不会生效。
     */
    USART1->CR1 |= USART_CR1_UE;
}

/*
 * usart1_send_byte —— 轮询方式发送一个字节
 *
 * 为什么需要等待 TXE？
 *   TXE（Transmit Data Register Empty）= 1 表示：
 *   发送数据寄存器是空的，可以写入下一个要发送的字节。
 *
 * 如果 TXE = 0 时强制写入 DR：
 *   可能会覆盖上一字节尚未移出（正在发送中）的数据。
 *   导致上一字节的数据丢失或波形出错。
 *
 * 发送流程：
 *   1. 等待 TXE = 1
 *   2. 写入 DR → 硬件自动移位发送
 *   3. TXE 再次变为 0（因为 DR 又有数据了）
 *   4. 移位发送完成后，TXE 重新变为 1
 */
static void usart1_send_byte(uint8_t byte)
{
    /*
     * 轮询等待 TXE 置位
     *
     * 此处的 while 循环就是"轮询"——CPU 不断检测 TXE 位。
     * 在 115200 波特率下，发送 1 个字节（10 位：起始位 + 8 数据 + 停止位）
     * 约需要 10 / 115200 ≈ 86.8us。
     * 这 86.8us 内 CPU 就在这个循环里等待。
     *
     * 思考：如果换成中断方式发送，CPU 在等待期间可以去做什么？
     */
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }

    /*
     * 向 DR 写入数据
     *
     * DR（Data Register）是一个双用途寄存器：
     *   写入 = 加载待发送数据 → 硬件自动串行发出
     *   读取 = 取出刚收到的数据
     *
     * 写入 DR 后，硬件会将这个字节加上起始位和停止位，
     * 按照 BRR 设定的波特率在 TX 引脚上逐位输出。
     *
     * 写入 DR 后 TXE 会被硬件自动清 0，
     * 直到数据移位发送完成，TXE 才会再次变成 1。
     */
    USART1->DR = byte;
}

/*
 * usart1_send_string —— 发送一个字符串
 *
 * 不断调用 usart1_send_byte，直到遇到字符串结尾 '\0'。
 */
static void usart1_send_string(const char *str)
{
    while (*str != '\0') {
        usart1_send_byte((uint8_t)*str);
        str++;
    }
}

/*
 * usart1_receive_byte_nonblocking —— 非阻塞检查是否有新字节收到
 *
 * 为什么叫"非阻塞"？
 *   这个函数检查 RXNE 标志，如果有数据就读出来返回 1，
 *   如果没数据就返回 0，不会卡住等待。
 *   与之对应的"阻塞式接收"会 while (!RXNE) 一直等。
 *
 * RXNE（Read Data Register Not Empty）= 1：
 *   表示接收移位寄存器已把收到的数据移到了 DR 中，
 *   等待 CPU 来读取。
 *
 * 读取 DR 后 RXNE 被自动清除 → 可以接收下一字节。
 *
 * 参数 byte：输出参数，存放收到的字节
 * 返回：1 = 收到数据，0 = 没收到
 */
static uint8_t usart1_receive_byte_nonblocking(uint8_t *byte)
{
    /*
     * 检查 RXNE 标志
     *
     * 如果 RXNE = 1，说明收到了一个新字节。
     * 如果 RXNE = 0，说明还没收到任何数据。
     *
     * 这里与发送的 TXE 不同：
     *   发送是"等待 TXE=1 再写"——CPU 主动准备发。
     *   接收是"等 RXNE=1 再读"——CPU 被动等外面送。
     *   所以接收用轮询效率更低——你不知数据何时来。
     *   这就是下一课（中断接收）要解决的问题。
     */
    if ((USART1->SR & USART_SR_RXNE) != 0U) {
        /*
         * 读取 DR 取出收到的字节
         *
         * 读 DR 时硬件会自动清除 RXNE 标志，
         * 表示 CPU 已取走数据，DR 可以接收新数据了。
         */
        *byte = (uint8_t)USART1->DR;
        return 1U;
    }

    return 0U;
}

/* LED 控制辅助函数 */
static void led_on(void)
{
    GPIOC->BRR = GPIO_BRR_BR13;
}

static void led_off(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void led_toggle(void)
{
    /*
     * ODR（Output Data Register）：
     *   输出数据寄存器，反映当前引脚输出电平。
     *   bit 13 = 1 → PC13 高电平（LED 灭）
     *   bit 13 = 0 → PC13 低电平（LED 亮）
     *   用 ODR 读取当前状态来实现翻转逻辑。
     */
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        led_on();
    } else {
        led_off();
    }
}

/*
 * main —— 主函数
 *
 * 流程：
 *   1. 系统初始化（时钟、LED、USART）
 *   2. 打印欢迎信息
 *   3. 主循环不断检查串口接收
 *   4. 收到命令后解析并控制 LED
 *
 * 命令协议（极简）：
 *   '1' → LED ON
 *   '0' → LED OFF
 *   't' 或 'T' → LED TOGGLE
 *
 * 串口参数：
 *   波特率 115200，数据位 8，停止位 1，无校验
 */
int main(void)
{
    uint8_t ch;

    system_clock_72mhz_init();
    led_pc13_init();
    usart1_gpio_init();
    usart1_init();

    /*
     * 串口发送欢迎信息
     *
     * 使用 \r\n（回车换行）而不是 \n：
     *   很多串口助手在收到 \n 时不会自动回到行首。
     *   用 \r\n 可以确保光标回到行首并换行，显示更整齐。
     */
    usart1_send_string("\r\n[reg] USART1 polling demo ready.\r\n");
    usart1_send_string("Send '1' to LED ON, '0' to LED OFF, 't' to TOGGLE.\r\n");

    while (1) {
        /*
         * 非阻塞检查串口是否有新数据
         *
         * 为什么用非阻塞？
         *   如果用阻塞式接收（while (!RXNE)），
         *   主循环会被卡住，无法做其他任务。
         *   非阻塞方式让 CPU 在没数据时可以干别的事。
         *
         * 本课主循环里没别的事，用非阻塞是为了演示概念。
         * 在真实项目中，没收到数据时可以去处理传感器、刷新显示等。
         */
        if (usart1_receive_byte_nonblocking(&ch) != 0U) {
            /*
             * 回显收到的字符
             *
             * 串口通信的一个重要习惯：收到命令后回显。
             * 这可以让你在串口助手中确认通信正常，无乱码。
             */
            usart1_send_string("RX: ");
            usart1_send_byte(ch);
            usart1_send_string("\r\n");

            /*
             * 命令解析
             */
            if (ch == '1') {
                led_on();
                usart1_send_string("LED ON\r\n");
            } else if (ch == '0') {
                led_off();
                usart1_send_string("LED OFF\r\n");
            } else if (ch == 't' || ch == 'T') {
                led_toggle();
                usart1_send_string("LED TOGGLE\r\n");
            } else {
                usart1_send_string("Unknown command. Use 1 / 0 / t\r\n");
            }
        }

        /*
         * 没有收到数据时，CPU 在这里可以做其他任务。
         * 本课为空——只等待接收命令。
         */
    }
}