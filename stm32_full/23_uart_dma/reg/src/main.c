#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 UART + DMA 发送实验"。
 *
 * 本课目标：
 * 1. 使用 USART1 作为串口发送端
 * 2. 使用 DMA1_Channel4 自动把内存中的字符串搬到 USART1->DR
 * 3. 每完成一次 DMA 发送，就翻转一次板载 LED
 * 4. 建立对 USART1_TX <-> DMA1_Channel4 映射关系的理解
 *
 * 与上一课（22_uart_interrupt）的区别：
 *   中断版：CPU 在中断中逐字节发送（usart1_send_byte）
 *   DMA 版：DMA 自动把整段数据搬到 DR，CPU 只需启动一次
 *
 * 为什么 UART 发送适合 DMA？
 *   发送一段字符串需要逐字节等待 TXE→写 DR，重复几十次。
 *   这对 DMA 来说是完美的"内存→外设"搬运场景：
 *   数据源在内存（字符串），目标固定（USART1->DR），重复性强。
 *
 * 为什么是 DMA1_Channel4？
 *   在 STM32F103 中，USART1_TX 固定映射到 DMA1_Channel4。
 *   USART1_RX 固定映射到 DMA1_Channel5。
 *   这是芯片内部硬连线，必须查参考手册确认。
 */

#define DMA_TX_PERIOD_MS 1000U

/*
 * SysTick 相关变量和定时功能（用于周期发送）
 */
static volatile uint32_t g_ms_ticks = 0U;
static volatile uint8_t g_uart_dma_busy = 0U;
static volatile uint8_t g_uart_dma_done = 0U;

/* 要发送的字符串（存在 Flash 中，DMA 从 Flash 地址读取） */
static const uint8_t g_dma_message[] = "[reg] USART1 DMA TX demo running...\r\n";

static void system_clock_72mhz_init(void);
static void systick_init(void);
static void delay_ms(uint32_t ms);
static void led_pc13_init(void);
static void led_toggle(void);
static void usart1_gpio_init(void);
static void usart1_init(void);
static void dma1_channel4_init(void);
static void usart1_send_byte_polling(uint8_t byte);
static void usart1_send_string_polling(const char *str);
static void usart1_dma_send(const uint8_t *data, uint16_t len);

/*
 * SysTick_Handler —— SysTick 中断
 * 每 1ms 触发一次，g_ms_ticks 自增。
 * 供 delay_ms 使用。
 */
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
 * systick_init —— 配置 SysTick 为 1ms 定时中断
 *
 * SysTick 是一个 24 位递减计数器。
 * 72MHz 下，计数值从 72000 减到 0 需要 1ms。
 * 每次减到 0 触发一次 SysTick_Handler 中断。
 */
static void systick_init(void)
{
    /*
     * LOAD = 72000 - 1 = 71999
     * 计一个数 = 1/72MHz ≈ 13.9ns
     * 计数次数 = 72000 → 72000/72MHz = 1ms
     */
    SysTick->LOAD = 72000U - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* 使用系统时钟 72MHz */
                    SysTick_CTRL_TICKINT_Msk |       /* 使能中断 */
                    SysTick_CTRL_ENABLE_Msk;         /* 启动 */
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

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void usart1_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 → USART1_TX → 复用推挽输出 */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOA->CRH |= GPIO_CRH_MODE9;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;

    /* PA10 保持标准配置 */
    GPIOA->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_CNF10_0;
}

static void usart1_init(void)
{
    USART1->BRR = 0x0271U;

    /*
     * 本课只做发送，不需要接收。
     * 所以 CR1 只设 TE，不设 RE。
     */
    USART1->CR1 = USART_CR1_TE;
    USART1->CR1 |= USART_CR1_UE;
}

/*
 * dma1_channel4_init —— 配置 DMA1 通道 4（USART1_TX）
 *
 * 与 ADC+DMA 课程的关键区别：
 *   1. 方向：内存 → 外设（12/13 课是外设 → 内存）
 *   2. 通道：DMA1_Channel4（不是 Channel1）
 *   3. USART 侧需要开 DMAT（CR3 寄存器）
 *
 * DMA 配置说明：
 *   DIR = 1：内存 → 外设（往 USART1->DR 写数据）
 *   MINC = 1：内存地址自增（字符串逐字节发送）
 *   PINC = 0：外设地址不自增（始终写 USART1->DR）
 *   PSIZE/MSIZE = 00：8 位宽度（串口是字节单位）
 *   CIRC = 0：普通模式，不循环（发完就停）
 *   TCIE = 1：传输完成中断（通知 CPU 发完了）
 */
static void dma1_channel4_init(void)
{
    /*
     * 第 1 步：开 DMA1 时钟
     * DMA1 挂在 AHB 总线上。
     */
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    /*
     * 第 2 步：配置前先关通道
     */
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;

    /*
     * 第 3 步：CPAR = USART1->DR 的地址
     *
     * 这里与 ADC+DMA 不同。
     * ADC+DMA：CPAR 是数据源（外设 → 内存）
     * 本课：CPAR 是数据目标（内存 → 外设）
     * DMA 会把内存数据写入这个地址。
     */
    DMA1_Channel4->CPAR = (uint32_t)&USART1->DR;

    /*
     * 第 4 步：清除并配置 CCR
     */
    DMA1_Channel4->CCR &= ~(DMA_CCR_MEM2MEM |
                            DMA_CCR_PL |
                            DMA_CCR_MSIZE |
                            DMA_CCR_PSIZE |
                            DMA_CCR_MINC |
                            DMA_CCR_PINC |
                            DMA_CCR_CIRC |
                            DMA_CCR_DIR |
                            DMA_CCR_TCIE);

    /*
     * DIR = 1：内存到外设
     *   0 = 外设 → 内存（ADC+DMA 用）
     *   1 = 内存 → 外设（本课用，把字符串发送到串口）
     *
     * MINC = 1：内存地址自增
     *   字符串的每个字节都在不同地址，必须自增。
     *
     * PINC = 0：外设地址不自增
     *   始终写 USART1->DR，地址固定。
     *
     * PSIZE/MSIZE = 00：8 位（字节宽度）
     *   串口是字节传输，与 ADC 的 16 位不同。
     *
     * CIRC = 0：非循环模式
     *   发送完一次就停，需要手动再启动。
     *   这跟 ADC+DMA 的持续采样场景不同。
     *
     * TCIE = 1：传输完成中断
     *   整个字符串都搬完后触发中断，
     *   我们在中断中做后续处理（关 DMA、置标志）。
     */
    DMA1_Channel4->CCR |= DMA_CCR_DIR;       /* 内存 → 外设 */
    DMA1_Channel4->CCR |= DMA_CCR_MINC;      /* 内存地址自增 */
    DMA1_Channel4->CCR |= DMA_CCR_TCIE;      /* 传输完成中断 */

    /*
     * 第 5 步：NVIC 配置
     * 使能 DMA1_Channel4 的中断通道。
     */
    NVIC_SetPriority(DMA1_Channel4_IRQn, 1U);
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);
}

/*
 * usart1_send_byte_polling —— 轮询发送单字节
 * 欢迎信息使用轮询发送，避免一开始就牵扯 DMA 中断逻辑。
 */
static void usart1_send_byte_polling(uint8_t byte)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }
    USART1->DR = byte;
}

/*
 * usart1_send_string_polling —— 轮询发送字符串
 */
static void usart1_send_string_polling(const char *str)
{
    while (*str != '\0') {
        usart1_send_byte_polling((uint8_t)*str);
        str++;
    }

    /*
     * 等待最后一个字节真正从移位寄存器发完（TC 标志）
     * TC = Transmission Complete
     * TXE 只表示 DR 空了，但最后一个字节可能还没从移位寄存器发出。
     * TC 表示整个物理传输已经完成。
     */
    while ((USART1->SR & USART_SR_TC) == 0U) {
    }
}

/*
 * usart1_dma_send —— 启动一次 DMA 发送
 *
 * 启动流程（有严格的顺序要求）：
 *   1. 检查是否正忙（g_uart_dma_busy）
 *   2. 等待上一字节物理发送完成（TC）
 *   3. 设置 CMAR（字符串地址）和 CNDTR（字符串长度）
 *   4. 清除 DMA 通道的中断标志
 *   5. 打开 USART1 的 DMAT 位（允许 DMA 发送）
 *   6. 使能 DMA 通道（CCR.EN = 1）
 *
 * 为什么需要等待 TC？
 *   如果上一次发送的最后一个字节还没完全发出，
 *     DMA 就装了新数据进去，可能造成数据混乱。
 *
 * DMAT（DMA Mode for Transmission）：
 *   位于 CR3 寄存器的 bit 7。
 *   0 = 禁止 DMA 发送
 *   1 = 允许 DMA 发送（DMA 请求可以触发 USART 读取 DR）
 *   如果不设这一位，DMA 通道即使使能了，USART 也不会响应。
 */
static void usart1_dma_send(const uint8_t *data, uint16_t len)
{
    if ((g_uart_dma_busy != 0U) || (len == 0U)) {
        return;
    }

    /* 等待最后一个字节物理发送完成 */
    while ((USART1->SR & USART_SR_TC) == 0U) {
    }

    g_uart_dma_busy = 1U;
    g_uart_dma_done = 0U;

    /* 关 DMA 通道 → 设置地址和长度 → 清标志 */
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;

    DMA1_Channel4->CMAR = (uint32_t)data;
    DMA1_Channel4->CNDTR = len;

    /* 清除通道 4 的所有中断标志 */
    DMA1->IFCR = DMA_IFCR_CGIF4 | DMA_IFCR_CTCIF4 |
                 DMA_IFCR_CHTIF4 | DMA_IFCR_CTEIF4;

    /* 打开 USART 的 DMA 发送开关 */
    USART1->CR3 |= USART_CR3_DMAT;

    /* 最后使能 DMA 通道，开始搬运 */
    DMA1_Channel4->CCR |= DMA_CCR_EN;
}

/*
 * DMA1_Channel4_IRQHandler —— DMA 通道 4 中断
 *
 * 当字符串全部搬运完成（CNDTR 减到 0）时触发。
 *
 * 中断处理：
 *   1. 检查 TCIF4（传输完成标志）
 *   2. 清除标志
 *   3. 关闭 DMA 通道
 *   4. 关闭 USART 的 DMAT
 *   5. 更新状态变量
 */
void DMA1_Channel4_IRQHandler(void)
{
    if ((DMA1->ISR & DMA_ISR_TCIF4) != 0U) {
        /* 清除标志 */
        DMA1->IFCR = DMA_IFCR_CGIF4 | DMA_IFCR_CTCIF4;

        /* 关闭 DMA 通道 */
        DMA1_Channel4->CCR &= ~DMA_CCR_EN;

        /* 关闭 USART 的 DMA 发送请求 */
        USART1->CR3 &= ~USART_CR3_DMAT;

        g_uart_dma_busy = 0U;
        g_uart_dma_done = 1U;
    }
}

/*
 * main —— 主函数
 *
 * 流程：
 *   1. 系统初始化（时钟、SysTick、LED）
 *   2. USART1 初始化
 *   3. DMA1_Channel4 初始化
 *   4. 轮询发送欢迎信息
 *   5. 每隔 1 秒用 DMA 发送一次字符串
 *   6. 发送完成后翻转 LED
 *
 * 注意：欢迎信息用轮询发送，而不是 DMA。
 *   这样可以先把 DMA 发送的核心逻辑单独突出。
 */
int main(void)
{
    system_clock_72mhz_init();
    systick_init();
    led_pc13_init();
    usart1_gpio_init();
    usart1_init();
    dma1_channel4_init();

    usart1_send_string_polling("\r\n[reg] USART1 DMA TX demo ready.\r\n");
    usart1_send_string_polling("A DMA message will be sent every 1 second.\r\n");

    while (1) {
        /*
         * 每 1 秒启动一次 DMA 发送
         *
         * 注意：这里减去 1 是去掉字符串末尾的 '\0' 结束符，
         * 不需要把结束符发送出去。
         */
        usart1_dma_send(g_dma_message, (uint16_t)(sizeof(g_dma_message) - 1U));
        delay_ms(DMA_TX_PERIOD_MS);

        if (g_uart_dma_done != 0U) {
            g_uart_dma_done = 0U;
            led_toggle();
        }
    }
}