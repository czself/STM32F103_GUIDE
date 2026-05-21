#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 ADC 中断采样"。
 *
 * 目标：
 * 1. 使用 PA1 (ADC1_IN1) 读取电位器电压
 * 2. 用 EOC 中断代替轮询等待
 * 3. 在中断里读取结果，并根据结果控制 LED
 * 4. 中断中再次启动下一次转换，形成持续采样
 *
 * 与上一课（轮询）的核心区别：
 *   轮询：CPU 主动循环检查 EOC，等待期间不能做别的事
 *   中断：ADC 转换完成后主动通知 CPU，CPU 等待期间可以干其他事
 *
 * 中断链路：
 *   ADC 转换完成 → EOC 置位 → EOCIE 使能 → 硬件触发中断请求
 *   → NVIC 放行（如果已使能）→ CPU 进入 ADC1_2_IRQHandler()
 *   → 读取 DR → 控制 LED → 再次启动转换
 */

/*
 * 全局变量 —— 在中断和主循环之间共享
 *
 * volatile 的原因：
 *   中断中修改，主循环（可能空闲或处理其他任务）中读取。
 *   如果没有 volatile，编译器可能把 g_adc_value 缓存在寄存器里，
 *   导致主循环读不到最新值。
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
 *
 * 与上一课完全相同。
 * HSE (8MHz) → PLL (x9) → SYSCLK (72MHz)
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
 *
 * 低电平点亮，高电平熄灭。
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
 *
 * MODE1=00, CNF1=00，即模拟输入。
 */
static void pa1_adc_input_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
}

/*
 * adc1_init —— 初始化 ADC1（中断模式）
 *
 * 与上一课的差异：
 *   1. 新增：使能 EOCIE（转换完成中断使能位）
 *   2. 新增：NVIC 配置（使能 ADC1_2_IRQn）
 *   3. 没有在 init 中启动转换（启动在 main 中第一次调用 adc1_start_conversion）
 *   4. 后续转换由中断函数中的 adc1_start_conversion() 持续驱动
 */
static void adc1_init(void)
{
    /*
     * 第 1 步：开时钟 + 分频
     */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    /*
     * 第 2 步：规则组 + 采样时间
     * 与上一课一致：单通道、最长采样时间
     */
    ADC1->SQR1 &= ~ADC_SQR1_L;
    ADC1->SQR3 &= ~ADC_SQR3_SQ1;
    ADC1->SQR3 |= 1U;

    ADC1->SMPR2 &= ~ADC_SMPR2_SMP1;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1;

    /*
     * ★ 第 3 步：关键区别 —— 开中断
     *
     * CR1（Control Register 1）的 EOCIE 位（bit 5）：
     *   EOCIE = End Of Conversion Interrupt Enable
     *
     * 0 = 禁止转换完成中断（上一课轮询模式就是这个配置）
     * 1 = 使能转换完成中断（本课使用）
     *
     * 当 EOCIE = 1 时：
     *   每次 ADC 完成一次转换（EOC 置位），ADC 外设就向 NVIC 发送中断请求。
     *   CPU 就会暂停当前代码，跳转到 ADC1_2_IRQHandler() 执行。
     */
    ADC1->CR1 |= ADC_CR1_EOCIE;

    /*
     * 第 4 步：上电 + 校准
     * 与上一课一致。
     */
    ADC1->CR2 |= ADC_CR2_ADON;
    delay(1000U);

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {
    }

    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }

    /*
     * ★ 第 5 步：关键区别 —— NVIC 配置
     *
     * 为什么需要 NVIC？
     *   ADC 内部使能了 EOCIE，这只是"分闸"。
     *   NVIC 是"总闸"，必须在 NVIC 中也使能 ADC 的中断通道，
     *   中断信号才能到达 CPU 内核。
     *
     * 为什么是 ADC1_2_IRQn？
     *   在 STM32F103 中，ADC1 和 ADC2 共用一个中断号：ADC1_2_IRQn。
     *   无论是 ADC1 还是 ADC2 产生中断，CPU 都跳转到同一个入口函数 ADC1_2_IRQHandler()。
     *   所以在中断函数中需要判断是哪个 ADC 触发了中断。
     *
     * NVIC_SetPriority：设置优先级为 1（越小越高）
     * NVIC_EnableIRQ：打开 NVIC 总闸
     */
    NVIC_SetPriority(ADC1_2_IRQn, 1U);
    NVIC_EnableIRQ(ADC1_2_IRQn);
}

/*
 * adc1_start_conversion —— 软件触发一次 ADC 转换
 *
 * 与上一课的 adc1_read_channel1 的前半部分相同。
 * 但不再包含"轮询等待 EOC"和"读取 DR"——这两步由中断接管。
 *
 * 这个函数会在两个地方被调用：
 *   1. main 中第一次启动转换（首次触发）
 *   2. 中断处理函数中再次启动（持续驱动）
 */
static void adc1_start_conversion(void)
{
    /*
     * EXTTRIG（bit 20）：允许外部/软件触发规则组转换
     * SWSTART（bit 22）：软件启动规则组转换
     *
     * 设了 EXTTRIG 后，SWSTART 才能生效。
     */
    ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
}

/*
 * ADC1_2_IRQHandler —— ADC1 和 ADC2 的中断入口
 *
 * 为什么叫 ADC1_2？
 *   在 F103 中，ADC1 和 ADC2 共享同一个中断向量。
 *   所以这个函数既处理 ADC1 的中断，也处理 ADC2 的中断。
 *
 * 本课只用了 ADC1，所以只需处理 ADC1 相关逻辑。
 *
 * 执行流程（当 ADC1 转换完成时）：
 *   1. ADC 硬件使 EOC 置位
 *   2. 因已使能 EOCIE，硬件触发中断请求
 *   3. NVIC 放行 → CPU 跳转到此函数
 *   4. 判断确实是 ADC1 产生的 EOC
 *   5. 读取 DR → 保存到 g_adc_value
 *   6. 根据结果控制 LED
 *   7. 再次启动下一次转换（形成持续采样链）
 *
 * ★ 为什么中断里不再轮询 EOC？
 *   因为能进入这个函数，本身就证明 EOC 已经置位了。
 *   不需要（也不应该）再 while (!EOC) 等待。
 *   这就是"中断"和"轮询"最根本的区别。
 */
void ADC1_2_IRQHandler(void)
{
    /*
     * 先确认 ADC1 确实完成了转换
     *
     * 因为：
     *   1. ADC1 和 ADC2 共用这个中断入口
     *   2. 即使只使能了 ADC1 中断，也可能有其他异常导致误入
     *
     * 所以判断 SR.EOC 是否置位，是一种防御性编程。
     */
    if ((ADC1->SR & ADC_SR_EOC) != 0U) {
        /*
         * 读取转换结果。
         *
         * 对于 ADC 来说，读取 DR 就是最核心的数据获取动作。
         * 读取 DR 后，EOC 标志会被自动清除（F103 硬件特性）。
         */
        g_adc_value = (uint16_t)ADC1->DR;

        /*
         * 根据结果更新 LED
         *
         * 注意：这里直接在中断中操作 GPIO。
         * 在简单 demo 中是可以的，但在复杂项目中，
         * 通常建议中断只做"标记事件"（设个标志位），
         * 在主循环或任务中再做具体处理。
         */
        if (g_adc_value > 2048U) {
            GPIOC->BRR = GPIO_BRR_BR13;   /* LED 亮 */
        } else {
            GPIOC->BSRR = GPIO_BSRR_BS13;  /* LED 灭 */
        }

        /*
         * ★ 关键：立即启动下一次转换
         *
         * 为什么需要这步？
         *   本课配置的是"单次转换"模式 (CONT=0)。
         *   如果不再次软件触发，ADC 就只转一次，然后就停住了。
         *
         * 这行一执行，ADC 就开始新一轮采样。
         * 完成后再进中断，再启动，如此循环。
         * 形成：转换 → 中断 → 启动 → 转换 → 中断 → ...
         *
         * 这就是"中断驱动的持续单次采样"。
         */
        adc1_start_conversion();
    }
}

/*
 * main —— 主函数
 *
 * 与上一课（轮询版）的区别：
 *   上一课：main 中每一次循环都调 adc1_read_channel1()，
 *           内部包含 启动→等待→读取 三步，CPU 在等待期间被占用。
 *
 *   本课：main 中只启动第一次转换，之后就交给中断了。
 *         主循环可以空闲（或做其他任务），ADC 完成后会自动进中断处理。
 *
 * 这种"后台采样"的架构，让 CPU 从"傻等 EOC"中解放出来。
 * 虽然本课的主循环什么都没做，但在真实项目中，
 * 你可以利用这段空闲去处理其他任务。
 */
int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    adc1_init();

    /*
     * 启动第一轮转换。
     * 后续转换将由中断里的 adc1_start_conversion() 持续驱动。
     */
    adc1_start_conversion();

    while (1) {
        /*
         * 主循环里不再负责等待 ADC。
         * ADC 采样、结果处理、LED 控制都交给了中断完成。
         *
         * 这里可以放其他需要 CPU 处理的任务。
         * （本课为了简单，让主循环空闲）
         *
         * 如果你不加任何延时，中断可能会以约 21us 一次的频率
         * （ADC 转换时间）持续触发。
         */
    }
}