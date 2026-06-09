#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 ADC 轮询采样"。
 *
 * 目标：
 * 1. 使用 PA1 (ADC1_IN1) 读取电位器电压
 * 2. 用轮询方式等待转换完成
 * 3. 根据 ADC 值大小控制 PC13 LED 亮灭
 *
 * 整体链路：
 *   电位器（0~3.3V）→ PA1（模拟输入）→ ADC1 转换
 *   → 轮询等待 EOC → 读取 DR 寄存器 → 比较阈值 → LED
 *
 * ADC 是什么？
 *   ADC = Analog to Digital Converter，模数转换器。
 *   作用是把连续的模拟电压转换成离散的数字值。
 *   STM32F103 的 ADC 是 12 位，所以结果范围是 0~4095。
 *   0 对应 0V，4095 对应 3.3V（参考电压 VREF+）。
 *
 * 为什么需要配置"模拟输入模式"？
 *   普通的 GPIO 输入模式有施密特触发器，会干扰模拟信号的准确采样。
 *   模拟输入模式会断开数字输入通路，让模拟电压直接进入 ADC 模块。
 *
 * 为什么 ADC 时钟不能太高？
 *   F103 的 ADC 最大时钟限制约 14MHz，太高会导致采样不准确。
 *   本课用 PCLK2 (72MHz) / 6 = 12MHz，在稳妥范围内。
 */

/*
 * delay —— 简单的软件延时
 *
 * 被 volatile 修饰的 count：
 *   告诉编译器每次循环都重新加载 count，不要优化掉循环。
 * __NOP() 是一条空指令，用来消耗一个 CPU 周期。
 * 在 72MHz 下，delay(1000) 大约延时几十微秒（取决于编译器优化等级）。
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
 * 时钟路径：
 *   HSE (8MHz，外部晶振) → PLL (x9) → SYSCLK (72MHz)
 *                                 → AHB (72MHz，不分频)
 *                                 → APB1 (36MHz，2分频)
 *                                 → APB2 (72MHz，不分频)
 *
 * 注意 APB1 虽然只有 36MHz，但定时器在分频≠1 时会自动 x2。
 * ADC 挂在 APB2 上，所以 ADC 输入时钟来自 PCLK2 = 72MHz。
 * 后面还需要进一步分频（/6）才能给 ADC 使用。
 */
static void system_clock_72mhz_init(void)
{
    /*
     * 第 1 步：Flash 等待周期
     * 72MHz 运行时，Flash 需要 2 个等待周期才能稳定读取指令。
     * PRFTBE = 1：开启预取缓冲（Prefetch Buffer），提高取指效率。
     * LATENCY = 2：2 个等待周期。
     */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /*
     * 第 2 步：打开 HSE（外部 8MHz 晶振）
     */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /*
     * 第 3 步：配置时钟树
     * 先全部清除，再从干净的状态开始配置。
     */
    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;    /* AHB = SYSCLK / 1 = 72MHz */
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;   /* APB1 = HCLK / 2 = 36MHz */
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;   /* APB2 = HCLK / 1 = 72MHz */
    RCC->CFGR |= RCC_CFGR_PLLSRC;       /* PLL 输入 = HSE */
    RCC->CFGR |= RCC_CFGR_PLLMULL9;     /* PLL 倍频 = x9 → 72MHz */

    /*
     * 第 4 步：开启 PLL 并等待稳定
     */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    /*
     * 第 5 步：切换系统时钟到 PLL
     */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

/*
 * led_pc13_init —— 初始化 PC13 作为 LED 输出
 *
 * PC13 特性：
 *   低电平点亮，高电平熄灭。
 *   所以初始输出高电平 → LED 灭。
 */
static void led_pc13_init(void)
{
    /*
     * 开启 GPIOC 时钟（GPIOC 挂在 APB2 上）
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * PC13 是高 8 位引脚（8~15），所以配置 CRH 寄存器。
     * MODE13 = 10 → 输出模式，最大速度 50MHz
     * CNF13  = 00 → 通用推挽输出
     */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /*
     * 初始为高电平 → LED 灭
     * BSRR 低 16 位写 1 对应引脚输出高电平
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

/*
 * pa1_adc_input_init —— 配置 PA1 为模拟输入模式
 *
 * PA1 的第二功能是 ADC1_IN1（ADC1 的通道 1 输入）。
 *
 * 模拟输入模式 vs 普通输入模式：
 *   普通输入模式（浮空/上拉/下拉）会经过施密特触发器，
 *   施密特触发器会对模拟信号造成畸变，使 ADC 采样不准。
 *
 *   模拟输入模式会：
 *   1. 断开数字输入通路（节省功耗）
 *   2. 让引脚电压直接进入 ADC 采样保持电路
 *   3. 关闭上下拉电阻
 *
 * CRL 寄存器中 PA1 对应的位（F103 的 CRL 管理 0~7 号引脚）：
 *   MODE1[1:0] = 00 → 输入模式
 *   CNF1[1:0]  = 00 → 模拟输入模式
 *   所以直接清零即可。
 */
static void pa1_adc_input_init(void)
{
    /*
     * 打开 GPIOA 时钟（GPIOA 挂在 APB2 上）
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA1 配成模拟输入模式
     * 对 F103 来说，模拟输入就是 MODE1=00, CNF1=00
     * 也就是直接清零即可。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
}

/*
 * adc1_init —— 初始化 ADC1
 *
 * 初始化步骤：
 *   1. 打开 ADC1 时钟
 *   2. 配置 ADC 时钟分频（72MHz / 6 = 12MHz）
 *   3. 设置规则组序列（长度 + 通道号）
 *   4. 设置采样时间
 *   5. 给 ADC 上电（ADON = 1）
 *   6. 复位校准
 *   7. 开始校准
 *
 * 为什么需要校准？
 *   F103 的 ADC 内部有电容阵列，存在制造偏差。
 *   校准过程会测量内部偏差并修正，使转换结果更准确。
 *   复位校准 → 等待结束 → 开始校准 → 等待结束，这是标准流程。
 */
static void adc1_init(void)
{
    /*
     * 第 1 步：打开 ADC1 时钟
     *
     * ADC1 挂在 APB2 总线上，所以用 APB2ENR 使能。
     */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /*
     * 第 2 步：配置 ADC 时钟分频
     *
     * ADCPRE 是 RCC->CFGR 中的位 14~15：
     *   00 = PCLK2 / 2
     *   01 = PCLK2 / 4
     *   10 = PCLK2 / 6 ← 本课使用
     *   11 = PCLK2 / 8
     *
     * 当前 PCLK2 = 72MHz，选择 /6：
     *   ADCCLK = 72MHz / 6 = 12MHz
     *
     * 为什么是 12MHz？
     *   F103 的 ADC 最大时钟典型值是 14MHz。
     *   12MHz 既在稳妥范围内，又提供了不错的转换速度。
     *   如果超过 14MHz，ADC 内部比较器可能来不及稳定。
     *
     * ADC 转换时间计算（12MHz 下）：
     *   采样时间 + 固定 12.5 个周期
     *   239.5 + 12.5 = 252 个周期
     *   252 / 12MHz ≈ 21us
     */
    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    /*
     * 第 3 步：设置规则组
     *
     * SQR（Sequence Register，规则组序列寄存器）：
     *   规则组就是"要转换的通道列表"。
     *   最简单的用法：只转换 1 个通道。
     *
     * SQR1 的 L[3:0] 位：
     *   表示"规则组通道数减 1"。
     *   采 1 个通道 → L = 0
     *   采 4 个通道 → L = 3
     */
    ADC1->SQR1 &= ~ADC_SQR1_L;

    /*
     * SQR3 的 SQ1[4:0] 位：
     *   表示规则组中第 1 个要转换的通道号。
     *   这里填 1，代表通道 1（ADC1_IN1），即 PA1。
     *   SQ1 在 SQR3 中占用 bit 0~4。
     */
    ADC1->SQR3 &= ~ADC_SQR3_SQ1;
    ADC1->SQR3 |= 1U;

    /*
     * 第 4 步：设置采样时间
     *
     * SMPR2（Sample Time Register 2）：
     *   每个通道都有独立的采样时间控制位。
     *   通道 1 的采样时间在 SMPR2 的 SMP1[2:0] 位（bit 3~5）。
     *
     * 采样时间可选值（F103）：
     *   000 = 1.5   cycles
     *   001 = 7.5   cycles
     *   010 = 13.5  cycles
     *   011 = 28.5  cycles
     *   100 = 41.5  cycles
     *   101 = 55.5  cycles
     *   110 = 71.5  cycles
     *   111 = 239.5 cycles ← 本课使用（最长采样时间）
     *
     * 为什么选最长的 239.5 cycles？
     *   1. 教学实验，稳定性优先
     *   2. 电位器输出阻抗可能较高，需要更长的采样时间让采样电容充满
     *   3. 本课不需要高速采样，长采样时间不造成问题
     *
     * 采样时间短了会怎样？
     *   采样电容还没充满就开始转换，结果会偏低且不稳定。
     *
     * ADC_SMPR2_SMP1 这个宏展开就是 SMP1 的三位全 1（bit 3-5 全 1），
     * 即 111 = 239.5 cycles。
     */
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP1;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1;

    /*
     * 第 5 步：开启 ADC
     *
     * CR2（Control Register 2）的 ADON 位（bit 0）：
     *   写 1 给 ADC 上电。
     *   这里第一次写 ADON = 1 是上电。
     *   （注意：后续启动转换时需要再次写 ADON = 1 才能真正开始转换）
     *
     * 上电后需要等待一小段时间让 ADC 内部稳定。
     */
    ADC1->CR2 |= ADC_CR2_ADON;

    /*
     * 上电后等待 ADC 内部稳定
     * delay(1000) 在 72MHz 下大约几十微秒。
     */
    delay(1000U);

    /*
     * 第 6 步：复位校准
     *
     * RSTCAL（Reset Calibration）位（CR2 的 bit 3）：
     *   写 1 开始复位校准。
     *   硬件会自动清除这个位，表示复位校准完成。
     *   所以这里轮询等待它变为 0。
     */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {
    }

    /*
     * 第 7 步：开始校准
     *
     * CAL（Calibration）位（CR2 的 bit 2）：
     *   写 1 开始校准过程。
     *   校准完成后硬件会自动清除 CAL 位。
     *
     * 校准做了什么？
     *   ADC 内部会测量自身的偏移误差，并存储在内部校准寄存器中。
     *   后续每次转换都会自动用这个值修正。
     *   如果不校准，结果可能会有几个 LSB 的偏移。
     */
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }
}

/*
 * adc1_read_channel1 —— 启动一次 ADC 转换并读取结果（轮询方式）
 *
 * 这就是"轮询采样"的核心操作：
 *   1. 软件触发启动转换
 *   2. CPU 等待（轮询 EOC 标志）
 *   3. EOC 标志置位后读取结果
 *
 * 为什么叫"轮询"？
 *   CPU 不干别的，就一直循环检查 EOC 标志是否置位。
 *   如果转换需要 21us，CPU 就傻等 21us，什么也不做。
 *
 * 返回：
 *   12 位的 ADC 转换结果，范围 0~4095。
 */
static uint16_t adc1_read_channel1(void)
{
    /*
     * 第 1 步：软件触发规则组转换
     *
     * CR2 中的相关位：
     *   EXTTRIG（bit 20）：允许外部/软件触发规则组转换
     *   SWSTART（bit 22）：软件启动规则组转换
     *
     * 为什么需要 EXTTRIG？
     *   在 F103 中，如果不设 EXTTRIG，SWSTART 不会生效。
     *   EXTTRIG 是"触发允许"的总开关。
     *   设了 EXTTRIG 后，可以通过软件（SWSTART）或外部事件启动。
     *
     * 一次写两个位的效果：
     *   先允许触发（EXTTRIG）
     *   再产生触发事件（SWSTART）
     *   ADC 检测到 SWSTART 后开始一次转换
     */
    ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;

    /*
     * 第 2 步：轮询等待转换完成
     *
     * SR（Status Register）的 EOC（End Of Conversion）位（bit 1）：
     *   0 = 转换正在进行
     *   1 = 转换已完成，DR 中有新数据
     *
     * 轮询方式的特点：
     *   CPU 不停检查这个位，直到它变成 1。
     *   在 etc. 期间 CPU 不能做其他事。
     *   如果转换时间很长（比如几十微秒），这是对 CPU 资源的浪费。
     *
     * 这就是下一课（中断方式）要解决的问题。
     */
    while ((ADC1->SR & ADC_SR_EOC) == 0U) {
    }

    /*
     * 第 3 步：读取转换结果
     *
     * DR（Data Register）：
     *   12 位的转换结果存放在 DR 的 bit 0~11 中。
     *   bit 12~15 为 0。
     *
     * 读取 DR 后的效果：
     *   1. 数据被取走
     *   2. EOC 标志会自动清除（F103 的特性）
     *   3. ADC 准备好下一次转换
     *
     * 为什么强转为 uint16_t？
     *   ADC1->DR 是 32 位寄存器，但有效数据只在低 16 位。
     *   用 uint16_t 更清晰地表示这是一个 12 位数据。
     */
    return (uint16_t)ADC1->DR;
}

/*
 * main —— 主函数
 *
 * 初始化流程：
 *   1. 系统时钟 → 72MHz
 *   2. PC13 LED → 输出（初始灭）
 *   3. PA1 → 模拟输入（ADC 通道）
 *   4. ADC1 → 单通道轮询模式
 *
 * 主循环逻辑：
 *   1. 调用 adc1_read_channel1 读取一次 ADC 结果
 *   2. 将结果与阈值 2048（约一半量程）比较
 *   3. 大于阈值 → LED 亮；小于等于阈值 → LED 灭
 *   4. delay 一会再继续
 *
 * 为什么阈值是 2048？
 *   12 位 ADC 范围 0~4095，中间值约 2048。
 *   对应电压 ≈ 3.3V * 2048 / 4095 ≈ 1.65V。
 *   旋转电位器到电压超过 1.65V 时 LED 亮。
 *
 * 为什么需要 delay(50000)？
 *   如果不加延时，LED 亮灭变化太快，人眼看不到。
 *   加上延时后，每次采样间隔变长，LED 变化肉眼可见。
 */
int main(void)
{
    uint16_t adc_value;

    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    adc1_init();

    while (1) {
        /*
         * 启动转换 → 等待完成 → 读取结果
         *
         * 这个过程完全由 CPU 主动完成。
         * 在等待期间（约 21us），CPU 不能做其他事。
         */
        adc_value = adc1_read_channel1();

        /*
         * 12 位 ADC 的典型范围是 0~4095。
         * 2048 对应约 1.65V
         */
        if (adc_value > 2048U) {
            /*
             * 电压高于阈值 → LED 亮
             * BRR（Bit Reset Register）：写 1 → 低电平
             */
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            /*
             * 电压低于或等于阈值 → LED 灭
             * BSRR（Bit Set Reset Register）：低 16 位写 1 → 高电平
             */
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }

        /*
         * 延时一段时间，让 LED 变化肉眼可见
         */
        delay(50000U);
    }
}