#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 ADC + DMA 基础实验"。
 *
 * 本课目标：
 * 1. 让 ADC1 持续采样 PA1(ADC1_IN1)
 * 2. 不再让 CPU 轮询 EOC，也不再让 CPU 在中断里读 DR
 * 3. 使用 DMA1_Channel1 自动把 ADC1->DR 的值搬到内存变量 g_adc_value
 * 4. 主循环只读取 g_adc_value，并根据它控制板载 LED
 *
 * 与上一课（11_adc_interrupt）的区别：
 *   中断版：ADC 完成转换 → 中断通知 CPU → CPU 亲自读 DR
 *   DMA 版：ADC 完成转换 → DMA 自动把 DR 搬到内存 → CPU 只需读内存变量
 *
 * 这一课的进化：
 *   CPU 从"等待转换完成"（轮询）→"被中断通知再读"（中断）
 *   →"数据已经被搬好了，直接看内存"（DMA）
 *
 * 为什么 DMA 适合 ADC？
 *   ADC 是"不断产生数据的外设"，每完成一次转换，DR 里就有新数据。
 *   让 DMA 自动把这些数据搬到内存，CPU 就可以解放出来做其他任务。
 *   这就是"ADC + DMA" 在 STM32 中如此经典的原因。
 *
 * 为什么是 DMA1_Channel1？
 *   在 STM32F103 中，ADC1 的 DMA 请求固定连接到 DMA1_Channel1。
 *   这不是软件配置决定的，是芯片内部硬连线决定的。
 *   需要查参考手册的 DMA 请求映射表确认。
 */

/*
 * 全局变量 —— DMA 直接写入的目标内存
 *
 * 这个变量放在 RAM 里，DMA 的目标就是不断把最新 ADC 结果写到这里。
 *
 * volatile 很重要：
 * - 因为这个变量不是只被当前 C 代码顺序修改
 * - DMA 硬件会在后台异步改它
 * - 如果不加 volatile，编译器可能会把它缓存在寄存器里
 *   导致主循环读不到最新值
 */
static volatile uint16_t g_adc_value = 0U;

/*
 * delay —— 简单的软件延时
 */
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

/*
 * led_pc13_init —— 初始化 PC13 作为 LED 输出
 */
static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

/*
 * pa1_adc_input_init —— 配置 PA1 为模拟输入模式
 */
static void pa1_adc_input_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA1 → ADC1_IN1，模拟输入模式：
     * MODE1 = 00, CNF1 = 00
     * 直接清零即可。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
}

/*
 * dma1_channel1_init —— 配置 DMA1 通道 1
 *
 * 这是本课相比上一课新增的核心初始化函数。
 * DMA 需要知道 5 个关键信息：
 *   1. 从哪搬（CPAR = 外设地址）
 *   2. 搬到哪（CMAR = 内存地址）
 *   3. 搬多少（CNDTR = 传输计数）
 *   4. 怎么搬（CCR = 方向、宽度、自增、循环等）
 *   5. 开始搬（CCR.EN = 1）
 *
 * 本课 DMA 配置说明：
 *   方向：外设 → 内存（ADC 数据到 RAM 变量）
 *   外设地址：ADC1->DR（固定）
 *   内存地址：&g_adc_value（固定）
 *   数据宽度：16 位（ADC 是 12 位，用半字）
 *   地址自增：都不自增（只搬到一个变量）
 *   循环模式：开启（持续采样）
 */
static void dma1_channel1_init(void)
{
    /*
     * 第 1 步：开启 DMA1 时钟
     *
     * DMA1 挂在 AHB 总线上，所以要在 AHBENR 中开时钟。
     * AHBENR 与 APB1ENR/APB2ENR 是不同寄存器，要注意区分。
     * DMA1EN 是 RCC->AHBENR 的 bit 0。
     */
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    /*
     * 第 2 步：配置前先关闭通道
     *
     * 在修改 DMA 通道的配置寄存器之前，
     * 必须先确保 CCR.EN = 0 使通道处于关闭状态。
     * 否则某些寄存器的写入可能被硬件忽略或导致未定义行为。
     */
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;

    /*
     * 第 3 步：设置传输计数 CNDTR
     *
     * CNDTR（Counter of Data to Transfer）：
     *   表示这一轮传输要搬多少个数据项。
     *   本课只需要始终保存一个最新 ADC 结果，所以长度就是 1。
     *
     * 注意：CNDTR 在每次传输完成后递减到 0。
     * 但因为本课使用了循环模式（CIRC=1），
     * 传完 1 次后会自动重载为初始值 1。
     */
    DMA1_Channel1->CNDTR = 1U;

    /*
     * 第 4 步：设置外设地址 CPAR
     *
     * CPAR（Channel Peripheral Address Register）：
     *   DMA 读取数据的源地址。
     *   本课写入 ADC1->DR 的地址，即每次从 ADC 数据寄存器取数。
     *
     * 注意：这里用 (uint32_t)&ADC1->DR 取的是 ADC1->DR 的地址值，
     * 而不是 ADC1->DR 的内容。CPAR 存的是地址，不是数据。
     */
    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;

    /*
     * 第 5 步：设置内存地址 CMAR
     *
     * CMAR（Channel Memory Address Register）：
     *   DMA 写入数据的目标地址。
     *   本课写入 g_adc_value 的地址，即每次把 ADC 结果存到这个变量。
     */
    DMA1_Channel1->CMAR = (uint32_t)&g_adc_value;

    /*
     * 第 6 步：配置通道控制寄存器 CCR
     *
     * 先清除所有相关位，再按需设置。
     * 这种"先清再设"的模式，可以防止残留配置干扰。
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
     * DIR（Direction）：
     *   0 = 从外设读取，写入内存（外设 → 内存）
     *   1 = 从内存读取，写入外设（内存 → 外设）
     *   本课：外设（ADC1->DR）→ 内存（g_adc_value）
     *   所以 DIR = 0（默认就是 0，不设也行）
     *
     * CIRC（Circular Mode）：
     *   0 = 普通模式，搬完 CNDTR 次后停止
     *   1 = 循环模式，搬完后自动重载 CNDTR，从头继续
     *   本课：CIRC = 1，因为 ADC 持续采样，DMA 要不停搬运
     *
     * PINC（Peripheral Increment）：
     *   0 = 每次传输后外设地址不自增
     *   本课：源始终是 ADC1->DR，所以 PINC = 0
     *
     * MINC（Memory Increment）：
     *   0 = 每次传输后内存地址不自增
     *   本课：目标始终是同一个变量 g_adc_value，所以 MINC = 0
     *   （下一课 13_adc_dma 中会用到 MINC=1 写入数组）
     *
     * PSIZE（Peripheral Size）：
     *   00 = 8 位
     *   01 = 16 位 ← 本课使用
     *   10 = 32 位
     *   本课：ADC 是 12 位，通常按 16 位处理，所以 PSIZE = 01
     *
     * MSIZE（Memory Size）：
     *   同 PSIZE。本课：内存变量是 uint16_t，所以 MSIZE = 01
     *   注意：PSIZE 和 MSIZE 应保持一致，否则数据可能错位。
     *
     * PL（Priority Level）：
     *   00 = 低，01 = 中，10 = 高，11 = 非常高
     *   本课设为高优先级（10），确保 ADC 数据及时搬运。
     */
    DMA1_Channel1->CCR |= DMA_CCR_CIRC;      /* 循环模式 */
    DMA1_Channel1->CCR |= DMA_CCR_PSIZE_0;   /* 外设 16 位 */
    DMA1_Channel1->CCR |= DMA_CCR_MSIZE_0;   /* 内存 16 位 */
    DMA1_Channel1->CCR |= DMA_CCR_PL_1;      /* 优先级高 */
}

/*
 * adc1_init —— 初始化 ADC1（连续转换 + DMA 模式）
 *
 * 与上一课（11_adc_interrupt）相比的关键变化：
 *   1. CONT = 1：连续转换模式（不是单次转换）
 *   2. DMA  = 1：开启 ADC 的 DMA 请求功能
 *   3. 不需要使能 EOCIE（不需要中断通知 CPU）
 *   4. 不需要配置 NVIC（不需要中断）
 *
 * CONT（Continuous Conversion）= 1：
 *   0 = 单次模式：每次触发只转一次，需要再次 SWSTART
 *   1 = 连续模式：转完一次自动开始下一次
 *   本课使用连续模式 + DMA，让采样和搬运自动持续。
 *
 * DMA 位（CR2 bit 8）：
 *   0 = 禁止 ADC 的 DMA 请求
 *   1 = 允许 ADC 在每次 EOC 时发出 DMA 请求
 *   如果不设这一位，ADC 不会通知 DMA 来搬数据。
 */
static void adc1_init(void)
{
    /*
     * 第 1 步：开启 ADC1 时钟 + 分频
     */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    /*
     * 第 2 步：规则组 + 采样时间
     */
    ADC1->SQR1 &= ~ADC_SQR1_L;
    ADC1->SQR3 &= ~ADC_SQR3_SQ1;
    ADC1->SQR3 |= 1U;

    ADC1->SMPR2 &= ~ADC_SMPR2_SMP1;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1;

    /*
     * ★ 第 3 步：关键区别 —— CONT + DMA
     *
     * CONT（bit 1）：连续转换模式
     *   启动一次后，ADC 会一直转，不再需要反复 SWSTART。
     *
     * DMA（bit 8）：DMA 请求使能
     *   每次转换完成后，ADC 向 DMA 控制器发送请求，
     *   DMA 收到请求后执行一次搬运（DR → 内存）。
     */
    ADC1->CR2 |= ADC_CR2_CONT;
    ADC1->CR2 |= ADC_CR2_DMA;

    /*
     * 第 4 步：上电 + 校准
     */
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
 * 这是"起跑线"——之前都是配置，这里真正开始运行。
 *
 * 启动顺序非常重要：
 *   先开 DMA（CCR.EN = 1）→ 再开 ADC（SWSTART）
 *   如果顺序反了，ADC 可能先完成一次转换，
 *   但 DMA 还没准备好，第一次数据就丢了。
 *
 * CONT 模式下的启动流程：
 *   第一次 SWSTART → ADC 转换完成 → EOC + DMA 请求
 *   → DMA 搬运到 g_adc_value → ADC 自动开始下一次转换 → ...
 */
static void adc1_dma_start(void)
{
    /*
     * 第 1 步：开启 DMA 通道
     *
     * 只有 CCR.EN = 1 后，DMA 通道才真正开始响应请求。
     * 在这之前，即使 ADC 发出了 DMA 请求，DMA 也不会响应。
     */
    DMA1_Channel1->CCR |= DMA_CCR_EN;

    /*
     * 第 2 步：启动 ADC 软件转换
     *
     * 因为已经打开 CONT，第一轮启动后，ADC 会持续不断地转换。
     * 因为已经打开 DMA，每次转换完成后自动触发 DMA 搬运。
     *
     * 从此 CPU 不再需要管 ADC 了：
     *   ADC 自己转 → DMA 自己搬 → g_adc_value 自己更新
     *   CPU 只需要读 g_adc_value
     */
    ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
}

/*
 * main —— 主函数
 *
 * 初始化顺序：
 *   clock → LED → PA1(ADC) → DMA → ADC → start
 *
 * 注意：DMA 要在 ADC 之前初始化。
 *   虽然 DMA 在 start 函数中才使能，
 *   但初始化时写的 CPAR/CMAR/CNDTR/CCR 最好在 ADC 开始工作前配好。
 *
 * 主循环：
 *   CPU 不需要碰任何 ADC 寄存器。
 *   只需要读取 g_adc_value——DMA 已经在后台保持它的最新值。
 *
 * 这条链路的精髓：
 *   电位器电压 → ADC1 持续转换 → DMA 自动搬运 → g_adc_value → 主循环读取 → LED 控制
 *   整个过程中，CPU 只需要最后一步：读内存。
 */
int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    dma1_channel1_init();
    adc1_init();
    adc1_dma_start();

    while (1) {
        /*
         * 这里已经看不到"读取 ADC1->DR"的代码了。
         *
         * 原因不是结果消失了，而是 DMA 已经帮我们把结果搬到了 RAM。
         * CPU 只需要直接读 g_adc_value——就像读一个普通的全局变量。
         *
         * 这就是 ADC + DMA 的魅力：
         *   外设数据自动"流"到内存，CPU 只需消费数据。
         */
        if (g_adc_value > 2048U) {
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }
    }
}