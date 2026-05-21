#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 SPI1 主模式回环实验"。
 *
 * 本课目标：
 * 1. 使用 SPI1 工作在主模式、Mode 0（CPOL=0, CPHA=0）
 * 2. 通过 PA7(MOSI) -> PA6(MISO) 跳线做最小回环验证
 * 3. 周期性发送测试字节，并检查收回数据是否一致
 * 4. 成功时翻转 LED，失败时点亮 LED 表示错误
 *
 * SPI vs UART 核心区别：
 *   UART：异步通信，只需 TX/RX 两根线，双方约定波特率
 *   SPI：同步通信，需 SCK（时钟）线，由主机主动打时钟驱动数据交换
 *
 * SPI 的本质：
 *   主机和从机内部各有一个移位寄存器。
 *   主机每打一拍 SCK 时钟，同时从 MOSI 移出 1 位、从 MISO 移入 1 位。
 *   所以 SPI 的发送和接收永远是**同时发生**的。
 *
 * 本课使用回环（loopback）的原因：
 *   PA7(MOSI) 用杜邦线连到 PA6(MISO)，自己发给自己收。
 *   这样不需要外部 SPI 设备，就能验证 SPI 主模式的配置是否正确。
 *
 * SPI Mode 0 的含义：
 *   CPOL=0：空闲时 SCK 为低电平
 *   CPHA=0：第一个边沿（上升沿）采样数据
 *   这是最常见的 SPI 模式，很多 SPI 设备都支持。
 */

#define SPI_TEST_PERIOD_MS 1000U

/* SysTick 毫秒计数 */
static volatile uint32_t g_ms_ticks = 0U;

static void system_clock_72mhz_init(void);
static void systick_init(void);
static void delay_ms(uint32_t ms);
static void led_pc13_init(void);
static void led_on(void);
static void led_off(void);
static void led_toggle(void);
static void spi1_gpio_init(void);
static void spi1_init(void);
static uint8_t spi1_transfer_byte(uint8_t tx_byte);

void SysTick_Handler(void)
{
    g_ms_ticks++;
}

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
 * systick_init —— 配置 SysTick 为 1ms 中断
 *
 * 72MHz / 72000 = 1kHz = 1ms
 */
static void systick_init(void)
{
    SysTick->LOAD = 72000U - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_ms_ticks;
    while ((g_ms_ticks - start) < ms) {
    }
}

static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

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
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        led_on();
    } else {
        led_off();
    }
}

/*
 * spi1_gpio_init —— 配置 SPI1 的引脚
 *
 * SPI1 的默认引脚映射（没有重映射）：
 *   PA5 → SPI1_SCK（串行时钟，主机输出）
 *   PA6 → SPI1_MISO（主入从出，主机输入）
 *   PA7 → SPI1_MOSI（主出从入，主机输出）
 *
 * 本课只验证 MOSI→MISO 回环，不接外部从机。
 * 所以 NSS（PA4）不需要配置物理引脚，使用软件管理。
 *
 * SCK（PA5）和 MOSI（PA7）的引脚配置：
 *   作为 SPI 主机的输出引脚，信号由 SPI 外设驱动。
 *   所以必须配成"复用推挽输出"（不是通用推挽输出！）。
 *   如果配成通用推挽输出，引脚始终由 GPIO 控制，SPI 信号无法通过。
 *
 * MISO（PA6）的引脚配置：
 *   作为主机的接收输入引脚，需要从外部读取信号。
 *   虽然本课回环中 PA6 被 PA7 驱动，但作为输入脚，
 *   配成浮空输入即可——SPI 的接收器会自动读取引脚电平。
 */
static void spi1_gpio_init(void)
{
    /*
     * 第 1 步：开启 SPI1、GPIOA、AFIO 时钟
     *
     * SPI1 挂在 APB2 总线上（与 USART1、ADC1 相同）。
     * PCLK2 = 72MHz，SPI1 的时钟源就是 PCLK2。
     * 后面 BR 分频系数基于 PCLK2 = 72MHz 计算。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /*
     * 第 2 步：PA5 → SPI1_SCK，PA7 → SPI1_MOSI
     *
     * 都是输出信号，都由 SPI 外设控制，
     * 所以配成"复用推挽输出"：
     *   MODE = 11 → 输出模式 50MHz
     *   CNF  = 10 → 复用推挽输出
     *
     * 注意：MODE 设为 11（50MHz），因为 SPI 时钟频率需要较高的翻转速度。
     * 虽然本课 BR 分频后实际 SCK 频率不高，但配成 50MHz 是标准做法。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5 |
                    GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOA->CRL |= GPIO_CRL_MODE5;      /* MODE5 = 11 */
    GPIOA->CRL |= GPIO_CRL_CNF5_1;     /* CNF5 = 10 */
    GPIOA->CRL |= GPIO_CRL_MODE7;      /* MODE7 = 11 */
    GPIOA->CRL |= GPIO_CRL_CNF7_1;     /* CNF7 = 10 */

    /*
     * 第 3 步：PA6 → SPI1_MISO
     *
     * 作为输入，配成浮空输入：
     *   MODE6 = 00 → 输入模式
     *   CNF6  = 01 → 浮空输入
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    GPIOA->CRL |= GPIO_CRL_CNF6_0;     /* CNF6 = 01 → 浮空输入 */
}

/*
 * spi1_init —— 初始化 SPI1 为主模式 Mode 0
 *
 * CR1 是本课最重要的寄存器。核心配置如下：
 *
 * MSTR（Master Selection，bit 2）：
 *   0 = 从模式，1 = 主模式 ← 本课
 *   主机负责提供 SCK 时钟并控制通信。
 *
 * BR[2:0]（Baud Rate Control，bit 3-5）：
 *   分频系数，决定 SCK 时钟频率。
 *   本课：BR = 010 → f_PCLK / 8 = 72MHz / 8 = 9MHz
 *   9MHz 的 SCK 在回环中完全够用，
 *   且比最高频率（18MHz）更稳健。
 *
 *   BR 可选值（基于 PCLK2=72MHz）：
 *     000 = /2   → 36MHz（可能超出 SPI 最高限制）
 *     001 = /4   → 18MHz
 *     010 = /8   → 9MHz  ← 本课使用
 *     011 = /16  → 4.5MHz
 *     100 = /32  → 2.25MHz
 *     101 = /64  → 1.125MHz
 *     110 = /128 → 562.5kHz
 *     111 = /256 → 281.25kHz
 *
 * CPOL（Clock Polarity，bit 1）：
 *   0 = 空闲时 SCK 为低电平 ← 本课（Mode 0）
 *   1 = 空闲时 SCK 为高电平（Mode 1/2/3）
 *
 * CPHA（Clock Phase，bit 0）：
 *   0 = 第一个边沿（SCK 从低变高的上升沿）采样数据 ← 本课（Mode 0）
 *   1 = 第二个边沿采样数据
 *
 *   CPOL + CPHA 组合决定 SPI Mode：
 *     Mode 0：CPOL=0, CPHA=0 ← 本课使用
 *     Mode 1：CPOL=0, CPHA=1
 *     Mode 2：CPOL=1, CPHA=0
 *     Mode 3：CPOL=1, CPHA=1
 *
 * SSM（Software Slave Management，bit 9）：
 *   1 = 软件管理 NSS（不使用物理 NSS 引脚） ← 本课
 *   SSM=1 时，NSS 引脚的电平由 SSI 位决定。
 *
 * SSI（Internal Slave Select，bit 8）：
 *   1 = 内部 NSS 信号为高（有效） ← 本课
 *   当 SSM=1 时，SSI 位的值被用作内部 NSS 信号。
 *   SSI=1 告诉 SPI 外设："片选已有效，可以工作"。
 *   如果不设 SSI=1，主模式可能无法正常产生 SCK 时钟。
 *
 * SPE（SPI Enable，bit 6）：
 *   1 = 使能 SPI ← 本课
 *   SPI 总开关，必须在所有其他配置完成后才打开。
 */
static void spi1_init(void)
{
    /*
     * 第 1 步：先清除 CR1 的主要配置位
     *
     * 上电或复位后 CR1 的默认值不一定是 0。
     * 先清除可能影响配置的位，确保从干净状态开始。
     */
    SPI1->CR1 &= ~(SPI_CR1_BIDIMODE |    /* 双向模式 */
                   SPI_CR1_BIDIOE |       /* 双向输出使能 */
                   SPI_CR1_CRCEN |        /* CRC 使能 */
                   SPI_CR1_DFF |          /* 数据帧格式（16 位） */
                   SPI_CR1_RXONLY |       /* 只接收 */
                   SPI_CR1_SSM |          /* 软件 NSS 管理 */
                   SPI_CR1_SSI |          /* 内部 NSS */
                   SPI_CR1_LSBFIRST |     /* 低位优先 */
                   SPI_CR1_BR |           /* 波特率分频 */
                   SPI_CR1_MSTR |         /* 主从选择 */
                   SPI_CR1_CPOL |         /* 时钟极性 */
                   SPI_CR1_CPHA);         /* 时钟相位 */

    /*
     * 第 2 步：配置为本课需要的模式
     *
     * MSTR = 1：主模式
     * BR = 010（BR_1）：PCLK2 / 8 = 9MHz
     * CPOL = 0：空闲低
     * CPHA = 0：第一个边沿采样
     * SSM = 1：软件 NSS
     * SSI = 1：内部 NSS 有效
     *
     * LSBFIRST = 0（默认）：高位先发送（MSB first）
     * DFF = 0（默认）：8 位数据格式
     * RXONLY = 0（默认）：全双工
     */
    SPI1->CR1 |= SPI_CR1_MSTR;            /* 主模式 */
    SPI1->CR1 |= SPI_CR1_BR_1;            /* BR = 010, /8 */
    SPI1->CR1 |= SPI_CR1_SSM;             /* 软件 NSS */
    SPI1->CR1 |= SPI_CR1_SSI;             /* 内部 NSS 有效 */

    /*
     * 第 3 步：打开 SPI（SPE = 1）
     *
     * SPE 必须在所有配置完成后再打开。
     * 如果在配置还没完成时就打开 SPE，可能产生不可预期的时钟行为。
     */
    SPI1->CR1 |= SPI_CR1_SPE;
}

/*
 * spi1_transfer_byte —— SPI 同步收发一个字节
 *
 * 这是 SPI 最核心的操作。
 * 与 UART 不同，SPI 的发送和接收是同时发生的。
 *
 * 流程：
 *   1. 等待 TXE = 1（发送缓冲区空，可以写入数据）
 *   2. 写 DR 启动发送（SPI 硬件自动产生 SCK 时钟并移位）
 *   3. 等待 RXNE = 1（接收缓冲区非空，收到一个完整字节）
 *   4. 读 DR 取回收到的字节
 *   5. 等待 BSY = 0（总线空闲）
 *
 * 为什么用 *(__IO uint8_t *)&SPI1->DR 而不是直接 SPI1->DR？
 *   SPI1->DR 是 16 位寄存器，直接读取会返回 16 位值。
 *   串行传输是 8 位的，所以用 8 位指针强转，只操作低 8 位。
 *   写时也一样：只写入 8 位，SPI 硬件会自动处理。
 *
 * 为什么等 TXE 而不是直接写？
 *   TXE = 1 表示发送缓冲区为空，可以写入下一字节。
 *   如果 TXE=0 时写入，可能覆盖正在发送的数据。
 *
 * 为什么等 RXNE？
 *   SPI 是同步的：写了 1 字节进去，就会同时收到 1 字节。
 *   RXNE = 1 表示收到的数据已在 DR 中，可以读取。
 *
 * 为什么还要等 BSY？
 *   RXNE 只表示数据已到接收缓冲区，
 *   但整个 SPI 移位过程可能还没完全结束。
 *   BSY = 0 才表示 SPI 总线完全空闲。
 *   对于连续传输场景，等 BSY 特别重要。
 *
 * 参数：要发送的字节
 * 返回：收到的字节
 */
static uint8_t spi1_transfer_byte(uint8_t tx_byte)
{
    uint8_t rx_byte;

    /*
     * 第 1 步：等待 TXE
     */
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {
    }

    /*
     * 第 2 步：写入 DR（启动 SPI 传输）
     *
     * 硬件检测到 DR 中有数据后，
     * 自动在 SCK 引脚上产生 8 个时钟脉冲，
     * 同时从 MOSI 移出数据、从 MISO 移入数据。
     */
    *(__IO uint8_t *)&SPI1->DR = tx_byte;

    /*
     * 第 3 步：等待 RXNE
     *
     * 8 个 SCK 时钟后，1 字节传输完成，
     * 收到的数据出现在 DR 中，RXNE 置位。
     */
    while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
    }

    /*
     * 第 4 步：读取 DR（取回收到的字节）
     *
     * 读 DR 后 RXNE 自动清除。
     */
    rx_byte = *(__IO uint8_t *)&SPI1->DR;

    /*
     * 第 5 步：等待 BSY = 0（总线空闲）
     *
     * BSY（Busy）标志：SPI 正在通信或发送缓冲区非空时为 1。
     */
    while ((SPI1->SR & SPI_SR_BSY) != 0U) {
    }

    return rx_byte;
}

/*
 * main —— 主函数
 *
 * 流程：
 *   1. 系统初始化（时钟、SysTick、LED）
 *   2. SPI1 初始化（引脚 + 主模式配置）
 *   3. 主循环：发送测试字节 → 检查回环结果 → 控制 LED
 *
 * 回环验证方法：
 *   - 先用杜邦线把 PA7(MOSI) 连到 PA6(MISO)
 *   - 发送 0xA5 → 检查是否收到 0xA5
 *   - 发送 0x3C → 检查是否收到 0x3C
 *   - 成功：LED 翻转一次
 *   - 失败：LED 常亮（提示错误）
 *
 * 为什么交替发 0xA5 和 0x3C？
 *   0xA5 = 1010 0101，0x3C = 0011 1100
 *   两位完全不同的模式，能暴露更多潜在问题。
 *   如果发出去什么收回来什么一直不变，可能只是"电平恰好没变"。
 */
int main(void)
{
    uint8_t tx_byte = 0xA5U;
    uint8_t rx_byte;

    system_clock_72mhz_init();
    systick_init();
    led_pc13_init();
    spi1_gpio_init();
    spi1_init();

    while (1) {
        /*
         * SPI 同步收发一个字节
         *
         * 发送 tx_byte 的同时，也会收到从 MISO 进来的数据。
         * 因为 PA7(MOSI) 和 PA6(MISO) 用杜邦线相连，
         * 所以理论上收回来的应该等于发出去的。
         */
        rx_byte = spi1_transfer_byte(tx_byte);

        if (rx_byte == tx_byte) {
            /*
             * 回环成功：发出的字节和收回的字节一致。
             * 翻转 LED，表示 SPI 配置正确、引脚连接正确。
             */
            led_toggle();
        } else {
            /*
             * 回环失败：发出的和收回的不一致。
             * LED 常亮，提示错误。
             * 常见原因：PA7 没接到 PA6、引脚模式配错、SPI 时钟没打开。
             */
            led_on();
        }

        /*
         * 交替发送两个测试字节
         */
        if (tx_byte == 0xA5U) {
            tx_byte = 0x3CU;
        } else {
            tx_byte = 0xA5U;
        }

        delay_ms(SPI_TEST_PERIOD_MS);
    }
}