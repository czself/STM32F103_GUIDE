#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 ADC + DMA 缓冲区采样实验"。
 *
 * 本课目标：
 * 1. 让 ADC1 持续采样 PA1(ADC1_IN1)
 * 2. 让 DMA1_Channel1 自动把结果写入一个长度为 16 的缓冲区
 * 3. 主循环对缓冲区求平均，再根据平均值控制板载 LED
 * 4. 借这个 demo 真正理解 MINC、CNDTR、循环缓冲区的意义
 *
 * 与上一课（12_dma_basic）的区别：
 *   上一课：DMA 目标是一个变量 → 只保留"最新值"
 *   本课：DMA 目标是一个数组 → 保留一组"历史采样值"
 *
 * 核心新概念：
 *   1. MINC = 1（内存地址自增）：
 *      上一课 MINC=0，始终写同一个变量。
 *      本课 MINC=1，写完数组第 0 个，自动移到第 1 个...
 *      直到第 15 个，然后 CIRC 让它回到第 0 个继续覆盖。
 *
 *   2. CNDTR = 16（不是 1）：
 *      每轮传输搬 16 个数据，搬完一轮后 CIRC 自动重载。
 *
 *   3. 平均滤波（软件处理）：
 *      对 16 个采样值求平均，比单一值更稳定。
 *      这就是"DMA 负责采集 → CPU 负责处理"的生产线模式。
 */

#define ADC_BUFFER_SIZE 16U

/*
 * 这是 DMA 的目标缓冲区。
 *
 * volatile 的原因：
 * - DMA 硬件会在后台持续改写这个数组
 * - CPU 在主循环里也会读取这个数组
 * - 所以不能让编译器把它当成普通静态数组优化掉
 *
 * 注意：DMA 在循环模式下会不断覆盖数组。
 * 所以 g_adc_buffer[0] 在任一时刻都是"当前最新值"，
 * 而整个数组代表最近 16 次采样的历史。
 * 但在循环模式下，"先后顺序"是循环的，不是时间线性的。
 */
static volatile uint16_t g_adc_buffer[ADC_BUFFER_SIZE] = {0};

static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}

/*
 * system_clock_72mhz_init —— 配置系统时钟到 72MHz
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

static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void pa1_adc_input_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
}

/*
 * dma1_channel1_init —— 配置 DMA1 通道 1（缓冲区模式）
 *
 * 与上一课相比的关键变化：
 *   1. CNDTR = 16（不再是 1）
 *   2. MINC  = 1（内存地址自增，不再是 0）
 *   3. CMAR = 数组首地址（不再是 &g_adc_value）
 *
 * DMA 工作流程（以 CNDTR=16, CIRC=1, MINC=1 为例）：
 *   ADC 第一次转换完成 → DMA 搬数据到 g_adc_buffer[0]，CNDTR 变 15
 *   ADC 第二次转换完成 → DMA 搬数据到 g_adc_buffer[1]，CNDTR 变 14
 *   ...
 *   ADC 第 16 次转换完成 → DMA 搬数据到 g_adc_buffer[15]，CNDTR 变 0
 *   因为 CIRC=1，CNDTR 自动重载为 16
 *   ADC 第 17 次转换完成 → DMA 又搬数据到 g_adc_buffer[0]（从头覆盖！）
 *   ...
 *
 * 这就是"循环缓冲区"（Circular Buffer）的基本含义。
 */
static void dma1_channel1_init(void)
{
    /*
     * 第 1 步：开 DMA1 时钟 + 关通道
     */
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;

    /*
     * 第 2 步：CNDTR = 16（不再是 1）
     *
     * 表示每轮传输要搬 16 个数据项。
     * 配合 CIRC=1 和 MINC=1，DMA 会在数组中循环写入。
     */
    DMA1_Channel1->CNDTR = ADC_BUFFER_SIZE;

    /*
     * 第 3 步：CPAR = 外设地址（不变）
     * 始终从 ADC1->DR 取数据。
     */
    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;

    /*
     * 第 4 步：CMAR = 数组首地址
     *
     * 注意：这里传的是数组首地址。
     * 当 MINC=1 时，DMA 每搬一次就给这个地址加 2（16 位宽度），
     * 自动指向下一个数组元素。
     * 搬完 16 次后，CIRC 让它重回数组首地址。
     */
    DMA1_Channel1->CMAR = (uint32_t)g_adc_buffer;

    /*
     * 第 5 步：配置 CCR
     */
    DMA1_Channel1->CCR &= ~(DMA_CCR_MEM2MEM |
                            DMA_CCR_PL |
                            DMA_CCR_MSIZE |
                            DMA_CCR_PSIZE |
                            DMA_CCR_MINC |
                            DMA_CCR_PINC |
                            DMA_CCR_CIRC |
                            DMA_CCR_DIR);

    /*
     * DIR   = 0  → 外设到内存（与上一课相同）
     * PINC  = 0  → 外设地址不自增，始终读 ADC1->DR（与上一课相同）
     * ★ MINC  = 1  → 内存地址自增，依次写数组各元素（与上一课不同！）
     *     上一课：MINC=0，因为目标只有一个变量
     *     本课：MINC=1，因为目标是一个数组
     *     如果不开 MINC，16 次搬运都写到 g_adc_buffer[0]，
     *     数组完全失去意义。
     * PSIZE = 01 → 外设宽度 16bit（与上一课相同）
     * MSIZE = 01 → 内存宽度 16bit（与上一课相同）
     * CIRC  = 1  → 循环模式，一轮 16 个结束后继续从头覆盖（与上一课相同）
     * PL    = 10 → 优先级高（与上一课相同）
     */
    DMA1_Channel1->CCR |= DMA_CCR_MINC;      /* ★ 内存地址自增 */
    DMA1_Channel1->CCR |= DMA_CCR_PSIZE_0;   /* 外设 16 位 */
    DMA1_Channel1->CCR |= DMA_CCR_MSIZE_0;   /* 内存 16 位 */
    DMA1_Channel1->CCR |= DMA_CCR_CIRC;       /* 循环模式 */
    DMA1_Channel1->CCR |= DMA_CCR_PL_1;       /* 优先级高 */
}

/*
 * adc1_init —— 初始化 ADC1
 *
 * 与上一课完全相同：
 *   CONT=1（连续转换）
 *   DMA=1（DMA 请求使能）
 *   单通道、最长采样时间、标准校准
 */
static void adc1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    ADC1->SQR1 &= ~ADC_SQR1_L;
    ADC1->SQR3 &= ~ADC_SQR3_SQ1;
    ADC1->SQR3 |= 1U;

    ADC1->SMPR2 &= ~ADC_SMPR2_SMP1;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1;

    ADC1->CR2 |= ADC_CR2_CONT;
    ADC1->CR2 |= ADC_CR2_DMA;

    ADC1->CR2 |= ADC_CR2_ADON;
    delay(1000U);

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {
    }

    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }
}

/*
 * adc1_dma_start —— 启动 ADC + DMA
 *
 * 顺序不变：先 DMA 再 ADC
 */
static void adc1_dma_start(void)
{
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
}

/*
 * adc_buffer_average_get —— 对缓冲区求平均
 *
 * 为什么用 uint32_t 求和？
 *   16 个 12 位数据最大和 = 16 × 4095 = 65520
 *   uint16_t 最大 65535，刚好可能溢出。
 *   所以用 uint32_t 安全。
 *
 * 为什么要求平均？
 *   单个 ADC 值可能被噪声干扰。
 *   对多个采样值求平均可以平滑噪声，结果更稳定。
 *   这就是最简单的"软件滤波"——不需要额外硬件。
 *
 * 本课的平均值滤波局限：
 *   1. 没有做 DMA 同步——CPU 可能在 DMA 正在写数组时读取
 *   2. 更健壮的做法：半传输中断 / 双缓冲 / 乒乓缓存
 *   但本课作为入门 demo，直接读取是可以接受的。
 */
static uint16_t adc_buffer_average_get(void)
{
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < ADC_BUFFER_SIZE; i++) {
        sum += g_adc_buffer[i];
    }

    return (uint16_t)(sum / ADC_BUFFER_SIZE);
}

/*
 * main —— 主函数
 *
 * 与上一课主循环的区别：
 *   上一课：读取 g_adc_value（单个变量）→ 比较阈值
 *   本课：调用 adc_buffer_average_get()（求 16 个值的平均）→ 比较阈值
 *
 * 效果差异：
 *   上一课：LED 状态可能因为单次采样噪声而轻微闪烁
 *   本课：平均值更稳定，LED 状态更平滑
 */
int main(void)
{
    uint16_t avg_value;

    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    dma1_channel1_init();
    adc1_init();
    adc1_dma_start();

    while (1) {
        /*
         * DMA 在后台刷新整个采样数组（循环写入，16 个元素持续更新）。
         * CPU 前台对这 16 个采样值求平均，用平均值控制 LED。
         *
         * 这就是"ADC + DMA + 缓冲区 + 软件处理"的最小原型。
         * 很多数据采集系统（示波器、传感器 hub）都基于这个模式。
         */
        avg_value = adc_buffer_average_get();

        if (avg_value > 2048U) {
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }
    }
}