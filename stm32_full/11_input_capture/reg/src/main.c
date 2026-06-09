#include "stm32f1xx.h"

/*
 * 本文件是"寄存器版 输入捕获"。
 *
 * 目标：
 * 1. 用 TIM2_CH1 在 PA0 输出 1kHz 方波
 * 2. 用导线把 PA0 接到 PA6
 * 3. 用 TIM3_CH1 在 PA6 做输入捕获
 * 4. 计算相邻两次上升沿的计数差
 * 5. 若结果接近 1000，则点亮 PC13
 *
 * 整体链路：
 *   TIM2 (PWM 输出 1kHz) → PA0 ──杜邦线── PA6 → TIM3_CH1 (输入捕获)
 *
 * 为什么同时用两个定时器？
 *   本课是"输入捕获"课，需要一个被测信号。
 *   让 MCU 自己产生信号、自己测量，不依赖外部信号发生器。
 */

/*
 * 全局变量 —— 在中断和主循环之间共享，必须加 volatile。
 *
 * 为什么是 uint16_t？
 *   因为 TIM3->CCR1 是 16 位寄存器，ARR = 0xFFFF，捕获值范围 0~65535。
 *
 * 为什么减法在回绕时仍然正确？
 *   uint16_t 的减法在 C 语言中的行为等价于 (a - b) mod 65536。
 *   所以当 CNT 从 65535 回绕到 0 时，减法结果恰好等于真正经过的计数个数。
 *   详见 README 6.6 节。
 */
static volatile uint16_t g_last_capture = 0U;   /* 上一次捕获到的 CCR1 值 */
static volatile uint16_t g_period_ticks = 0U;     /* 本次 CCR1 - 上次 CCR1 = 周期（计数值） */
static volatile uint8_t  g_capture_ready = 0U;   /* 中断置 1，主循环清 0，表示有新数据 */

/*
 * system_clock_72mhz_init —— 配置系统时钟到 72MHz
 *
 * 时钟路径：
 *   HSE (8MHz) → PLL (x9) → SYSCLK (72MHz)
 *                         → AHB (72MHz, 不分频)
 *                         → APB1 (36MHz, 2分频) — TIM3 挂在这条总线上
 *                         → APB2 (72MHz, 不分频) — GPIO / TIM2 挂在这条总线上
 *
 * 注意：虽然 APB1 是 36MHz，但 TIM 在 APB1 分频系数≠1 时，内部自动倍频 x2，
 * 所以 TIM3 的输入时钟仍然是 72MHz。
 */
static void system_clock_72mhz_init(void)
{
    /*
     * 第 1 步：配置 Flash 等待周期
     *
     * STM32F103 在 72MHz 下需要 2 个等待周期才能稳定读取 Flash。
     * PRFTBE = 1：开启预取缓冲，提高指令执行效率。
     * LATENCY = 2：2 个等待周期。
     */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /*
     * 第 2 步：打开 HSE（外部高速晶振）
     */
    RCC->CR |= RCC_CR_HSEON;                         /* HSEON = 1：请求 HSE 起振 */
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {        /* 等待 HSERDY 标志置 1 */
    }

    /*
     * 第 3 步：配置时钟树
     *
     * 先清除所有相关配置位，从"干净状态"开始设置：
     *   HPRE    — AHB 预分频
     *   PPRE1   — APB1 预分频
     *   PPRE2   — APB2 预分频
     *   PLLSRC  — PLL 时钟源选择
     *   PLLXTPRE — HSE 分频器（本课不用）
     *   PLLMULL — PLL 倍频系数
     *   SW      — 系统时钟源选择
     */
    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    /*
     * AHB 不分频：HCLK = SYSCLK = 72MHz
     */
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;

    /*
     * APB1 2分频：PCLK1 = HCLK / 2 = 36MHz
     * 注意：TIM 外设在 APB1 分频≠1 时，时钟会被硬件自动 x2 → 72MHz
     */
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;

    /*
     * APB2 不分频：PCLK2 = HCLK = 72MHz
     */
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;

    /*
     * PLL 时钟源 = HSE (8MHz)
     * PLL 倍频 = x9 → 8MHz * 9 = 72MHz
     */
    RCC->CFGR |= RCC_CFGR_PLLSRC;                   /* PLLSRC = 1：PLL 输入 = HSE */
    RCC->CFGR |= RCC_CFGR_PLLMULL9;                 /* PLLMULL = 0111：x9 倍频 */

    /*
     * 第 4 步：开启 PLL 并等待就绪
     */
    RCC->CR |= RCC_CR_PLLON;                         /* PLLON = 1 */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {        /* 等待 PLLRDY */
    }

    /*
     * 第 5 步：切换系统时钟到 PLL
     *
     * 先清 SW 位，再设 SW_PLL，确保切换干净。
     * 然后等待 SWS (Switch Status) 确认切换完成。
     */
    RCC->CFGR &= ~RCC_CFGR_SW;                       /* 清 SW 位 */
    RCC->CFGR |= RCC_CFGR_SW_PLL;                    /* SW = 10：选择 PLL 作为 SYSCLK */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {  /* 等待 SWS = PLL */
    }
}

/*
 * led_pc13_init —— 初始化板载 LED（PC13）
 *
 * PC13 特性：
 *   低电平点亮，高电平熄灭。
 *   因此初始输出高电平 → LED 灭。
 */
static void led_pc13_init(void)
{
    /*
     * 第 1 步：开启 GPIOC 时钟
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * 第 2 步：配置 PC13 为推挽输出
     *
     * PC13 属于高 8 位引脚，用 CRH 寄存器。
     * MODE13[1:0] = 10 → 输出模式，50MHz
     * CNF13[1:0]  = 00 → 通用推挽输出
     */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;                 /* MODE13 = 10 */

    /*
     * 第 3 步：初始输出高电平 → LED 灭
     *
     * BSRR (Bit Set Reset Register)：
     *   低 16 位写 1 → 对应引脚输出高电平
     *   高 16 位写 1 → 对应引脚输出低电平
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;                    /* BS13 = 1 → PC13 输出高电平 */
}

/*
 * tim2_pwm_output_init —— 配置 TIM2_CH1 (PA0) 输出 1kHz PWM
 *
 * 频率计算：
 *   TIM2 输入时钟 = 72MHz (TIM2 挂在 APB1，但分频≠1 时自动 x2 到 72MHz)
 *   PSC = 71      → 计数频率 = 72MHz / (71+1) = 1MHz
 *   ARR = 999     → PWM 频率 = 1MHz / 1000 = 1kHz
 *   CCR1 = 500    → 占空比 = 500/1000 = 50%
 *
 * PA0 引脚复用：
 *   PA0 的第二功能 AFIO 映射为 TIM2_CH1。
 *   所以需要配成"复用推挽输出"而非"通用推挽输出"。
 */
static void tim2_pwm_output_init(void)
{
    /*
     * 第 1 步：开启 GPIOA 和 AFIO 时钟
     *
     * IOPAEN = 1：开启 GPIOA 的时钟
     * AFIOEN = 1：开启复用功能 I/O 时钟
     *   使用定时器的复用功能输出时，AFIO 时钟也是必须的。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /*
     * 第 2 步：配置 PA0 为复用推挽输出
     *
     * CRL (低 8 位引脚控制寄存器)：
     *   MODE0[1:0] = 10 → 输出模式，50MHz
     *   CNF0[1:0]  = 10 → 复用功能推挽输出
     *     (CNF0_1 = 1, CNF0_0 = 0)
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);   /* 清原有配置 */
    GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;  /* MODE0=10, CNF0=10 */

    /*
     * 第 3 步：开启 TIM2 时钟
     *
     * TIM2 挂在 APB1 总线上。
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 第 4 步：配置预分频器和自动重装载值
     *
     * PSC = 71 → 计数频率 = 72MHz / 72 = 1MHz
     * ARR = 999 → 每 1000 个计数溢出一次 = 1kHz
     * CCR1 = 500 → 前 500 个计数输出高电平，后 500 个输出低电平
     */
    TIM2->PSC = 72U - 1U;                             /* 预分频值 71 */
    TIM2->ARR = 1000U - 1U;                           /* 自动重装载值 999 */
    TIM2->CCR1 = 500U;                                /* 比较值 500（50% 占空比） */

    /*
     * 第 5 步：配置通道模式为 PWM1
     *
     * CCMR1 是通道模式寄存器：
     *   CC1S[1:0] = 00 → 通道 1 作为输出（而非输入捕获）
     *   OC1M[2:0] = 110 → PWM 模式 1
     *     向上计数时，CNT < CCR1 输出有效电平，CNT >= CCR1 输出无效电平
     *   OC1PE = 1 → 预装载使能
     *     CCR1 的新值在更新事件时才生效，避免中途突变
     *
     * 先清除再设置是最安全的写法。
     */
    TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);  /* 清 CC1S 和 OC1M */
    TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2; /* OC1M = 110 (PWM1) */
    TIM2->CCMR1 |= TIM_CCMR1_OC1PE;                      /* OC1PE = 1（预装载使能） */

    /*
     * 第 6 步：使能通道 1 输出
     *
     * CCER.CC1E = 1 → 通道 1 输出使能，PWM 信号才能到达 PA0 引脚。
     */
    TIM2->CCER |= TIM_CCER_CC1E;

    /*
     * 第 7 步：产生更新事件，立即加载 PSC 和 ARR
     *
     * EGR (Event Generation Register)：
     *   UG (Update Generation) = 1 → 产生更新事件
     *   更新事件会使：
     *     - PSC 实际生效（影子寄存器加载）
     *     - ARR 实际生效
     *     - 计数器 CNT 清零
     *     如果不做这步，新设置的 PSC/ARR 要等到第一次溢出才会生效。
     */
    TIM2->EGR |= TIM_EGR_UG;

    /*
     * 第 8 步：启动 TIM2 计数器
     *
     * CR1.CEN (Counter ENable) = 1 → 计数器开始递增。
     * 之前的所有配置都是"铺路"，这一行才是"开车"。
     */
    TIM2->CR1 |= TIM_CR1_CEN;
}

/*
 * pa6_input_pin_init —— 配置 PA6 为浮空输入
 *
 * PA6 的第二功能是 TIM3_CH1。
 * 作为 TIM3 的输入捕获信号引脚，不需要配置为复用功能，
 * 只需要把引脚配置为浮空输入即可——定时器模块会自动读取引脚电平。
 *
 * 为什么是浮空输入？
 *   外部信号通过杜邦线从 PA0 直接连到 PA6，信号强度足够。
 *   不需要内部上拉或下拉电阻干扰信号。
 */
static void pa6_input_pin_init(void)
{
    /*
     * 开启 GPIOA 时钟（如果之前已经开过，再次设置没有副作用）
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * 配置 PA6 为浮空输入
     *
     * CRL 寄存器中 PA6 对应的位：
     *   MODE6[1:0] = 00 → 输入模式（两位全 0）
     *   CNF6[1:0]  = 01 → 浮空输入（CNF6_0 = 1, CNF6_1 = 0）
     *
     * 注意：浮空输入意味着引脚电平完全由外部决定。
     * 如果外部信号是高阻态，引脚电平不确定。
     * 但本课中 PA6 被 PA0 的低阻抗输出驱动，所以没问题。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);   /* 先清除 */
    GPIOA->CRL |= GPIO_CRL_CNF6_0;                      /* CNF6 = 01 → 浮空输入 */
}

/*
 * tim3_input_capture_init —— 配置 TIM3_CH1 (PA6) 为输入捕获
 *
 * 这是本课的核心初始化函数，完整流程如下：
 *
 *   1. 开启 TIM3 时钟
 *   2. 设置 PSC = 71 → 计数频率 = 1MHz (1 计数 = 1us)
 *   3. 设置 ARR = 0xFFFF → 计数器范围 0~65535
 *   4. 配置 CCMR1.CC1S = 01 → 通道 1 为输入，映射到 TI1 (PA6)
 *   5. 配置 CCER → 上升沿捕获 + 使能捕获
 *   6. 配置 DIER.CC1IE = 1 → 使能捕获中断
 *   7. 先清 SR.CC1IF → 确保没有残留标志
 *   8. 配置 NVIC → 打开中断总闸
 *   9. CR1.CEN = 1 → 启动计数器
 */
static void tim3_input_capture_init(void)
{
    /*
     * 第 1 步：开启 TIM3 时钟
     *
     * TIM3 挂在 APB1 总线上（bit 1 of RCC->APB1ENR）。
     * 在系统时钟配置中 APB1 被设为 2 分频，所以 PCLK1 = 36MHz。
     * 但 TIM 外设在 APB1 分频系数≠1 时，内部时钟会被自动倍频 x2，
     * 所以 TIM3 的实际输入时钟 = 36MHz * 2 = 72MHz。
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /*
     * 第 2 步：配置预分频器 PSC = 71
     *
     * 计数频率 = TIM3 输入时钟 / (PSC + 1)
     *          = 72MHz / (71 + 1)
     *          = 1MHz
     *
     * 这意味着每 1 个计数 = 1us（1 / 1MHz = 1us）。
     * 这样后续计算周期非常直观：捕获差值多少，就是多少微秒。
     *
     * 第 3 步：配置自动重装载值 ARR = 0xFFFF (65535)
     *
     * 为什么设这么大？
     *   因为输入捕获需要测量各种可能的信号周期。
     *   ARR 越大，能测量的最大周期越长。
     *   最大可测时间 = (ARR + 1) * 1us = 65536us ≈ 65.5ms
     *   对应最低可测频率 ≈ 15Hz
     *
     * 对于本课的 1kHz 信号（周期 1000us），这个范围绰绰有余。
     */
    TIM3->PSC = 72U - 1U;                             /* 预分频值 = 71 */
    TIM3->ARR = 0xFFFFU;                              /* 自动重装载值 = 65535 */

    /*
     * 第 4 步：配置通道模式 CCMR1
     *
     * CCMR1 中与本课相关的字段：
     *   CC1S[1:0]  — 通道 1 方向与输入映射选择
     *   IC1PSC[1:0] — 输入捕获预分频
     *   IC1F[3:0]  — 输入捕获滤波器
     *
     * 先清除这三个字段的所有位，再做设置：
     *
     * CC1S = 01（只设 CC1S_0）：
     *   00 = 通道 1 作为输出（PWM/比较模式）
     *   01 = 通道 1 作为输入，映射到 TI1 ← 本课使用
     *   10 = 通道 1 作为输入，映射到 TI2
     *   11 = 通道 1 作为输入，映射到 TRC
     *
     *   CC1S = 01 意味着：
     *     外部信号从 TIM3_CH1 引脚 (PA6) 进入，经过 TI1 通道到达捕获电路。
     *     一旦设为输入模式，CCR1 就不再是比较值，而是"捕获寄存器"。
     *
     * IC1PSC = 00：每个有效边沿都触发一次捕获，不分频。
     * IC1F = 0000：不滤波。本课信号来自内部连线，无需滤波。
     */
    TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S |                   /* 清 CC1S */
                     TIM_CCMR1_IC1PSC |                  /* 清 IC1PSC */
                     TIM_CCMR1_IC1F);                    /* 清 IC1F */
    TIM3->CCMR1 |= TIM_CCMR1_CC1S_0;                    /* CC1S = 01 */

    /*
     * 第 5 步：配置 CCER —— 边沿选择和捕获使能
     *
     * CCER 中与本课相关的位：
     *   CC1E  — 通道 1 捕获使能
     *   CC1P  — 通道 1 极性
     *   CC1NP — 通道 1 极性（互补）
     *
     * 边沿选择（CC1P + CC1NP）：
     *   00 = 上升沿捕获 ← 本课使用
     *   10 = 下降沿捕获
     *   11 = 双沿捕获
     *
     * CC1E = 1：
     *   使能通道 1 的捕获功能。
     *   如果不置这一位，即使有边沿到来，CCR1 也不会被更新。
     */
    TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);    /* CC1P=0, CC1NP=0 → 上升沿 */
    TIM3->CCER |= TIM_CCER_CC1E;                         /* 使能通道 1 捕获 */

    /*
     * 第 6 步：使能捕获中断
     *
     * DIER (DMA/Interrupt Enable Register)：
     *   CC1IE (bit 1) = 1 → 通道 1 捕获事件产生中断请求
     *
     * 如果不开中断会怎样？
     *   捕获仍然会发生，CCR1 仍然会被更新，
     *   但你需要轮询 SR.CC1IF 标志来知道"有新数据了"。
     *   中断方式让 CPU 在捕获事件发生时立即响应，更及时。
     */
    TIM3->DIER |= TIM_DIER_CC1IE;

    /*
     * 第 7 步：清除通道 1 捕获标志
     *
     * SR (Status Register)：
     *   CC1IF (bit 1) = 1 → 通道 1 发生了一次捕获
     *
     * 为什么要在启动前先清一次？
     *   上电复位后 SR 寄存器的内容是不确定的。
     *   如果 CC1IF 恰好为 1，一开启中断就会立刻进入 ISR。
     *   先清除一次，确保是从"干净状态"开始。
     *   这就是"先打扫干净再接待客人"的习惯操作。
     */
    TIM3->SR &= ~TIM_SR_CC1IF;

    /*
     * 第 8 步：配置 NVIC（嵌套向量中断控制器）
     *
     * NVIC_SetPriority(TIM3_IRQn, 1U)：
     *   设置 TIM3 中断的抢占优先级为 1。
     *   STM32F103 使用 4 位优先级（0~15），数值越小优先级越高。
     *
     * NVIC_EnableIRQ(TIM3_IRQn)：
     *   使能 NVIC 中 TIM3 的中断通道。
     *   这是"总闸"——TIM3 内部 CC1IE 是"分闸"，
     *   只有总闸分闸都打开，中断信号才能到达 CPU 内核。
     */
    NVIC_SetPriority(TIM3_IRQn, 1U);
    NVIC_EnableIRQ(TIM3_IRQn);

    /*
     * 第 9 步：启动 TIM3 计数器
     *
     * CR1.CEN = 1 → 计数器开始从 0 向上递增。
     * 没有这一行，前面所有配置都是"停在停车场的车，发动机没启动"。
     */
    TIM3->CR1 |= TIM_CR1_CEN;
}

/*
 * TIM3_IRQHandler —— TIM3 全局中断入口
 *
 * 每次 TIM3 捕获事件发生时，硬件自动跳转到这里。
 *
 * 注意：
 *   TIM3 只有一个中断入口，但可能有多个中断源
 *   （更新中断 UIF、通道 1 捕获 CC1IF、通道 2 捕获 CC2IF 等）。
 *   所以进入 ISR 后先判断是哪个标志触发了中断。
 */
void TIM3_IRQHandler(void)
{
    /*
     * 判断是否确实是通道 1 捕获事件触发了中断
     *
     * 因为 TIM3 也可能因为更新事件（UIF=1）进入中断，
     * 所以必须检查 CC1IF。
     */
    if ((TIM3->SR & TIM_SR_CC1IF) != 0U) {

        /*
         * 读取捕获值（CCR1）
         *
         * 在输入捕获模式下，CCR1 保存了硬件在上升沿到来那一刻
         * 自动锁存的 CNT 值。我们称之为"边沿时间戳"。
         *
         * 注意：
         *   CCR1 虽然是 16 位寄存器，但 HAL_TIM_ReadCapturedValue
         *   返回 uint32_t。这里直接强转为 uint16_t。
         */
        uint16_t current_capture = (uint16_t)TIM3->CCR1;

        /*
         * 计算周期（计数差）
         *
         * 当前捕获值 - 上次捕获值 = 两次上升沿之间的计数个数
         *
         * 这就是"边沿时间戳"做减法的核心原理：
         *   第一次上升沿  → 时间戳 = T1
         *   第二次上升沿  → 时间戳 = T2
         *   一个周期经过的计数 = T2 - T1
         *
         * 为什么 uint16_t 减法能正确处理 CNT 回绕（65535 → 0）？
         *   假设 T1 = 65500, T2 = 200
         *   T2 - T1 = 200 - 65500 (uint16_t) = 236
         *   真正的周期 = (65536 - 65500) + 200 = 236
         *   C 语言的 uint16_t 减法等价于 (T2 + 65536 - T1) % 65536
         *   所以结果恰好正确，不需要额外处理回绕。
         *
         * 约束条件：
         *   两次捕获之间 CNT 最多只能回绕一次。
         *   即被测信号周期 < ARR + 1 = 65536us。
         *   本课信号周期 1000us，完全满足。
         */
        g_period_ticks = (uint16_t)(current_capture - g_last_capture);

        /*
         * 保存本次捕获值，供下次计算使用
         */
        g_last_capture = current_capture;

        /*
         * 通知主循环：有新数据了
         *
         * 主循环检测到 g_capture_ready = 1 后处理数据。
         * 这里不直接在中断中控制 LED，而是"通知主循环去处理"，
         * 这是"在中断中做最少的事情"的良好编程习惯。
         */
        g_capture_ready = 1U;

        /*
         * 清除通道 1 捕获标志
         *
         * 重要：必须清除！
         *   如果不清除，退出 ISR 后硬件发现 CC1IF 仍然为 1，
         *   会再次触发中断 → 永远卡在同一个中断里。
         *
         * 写 0 清除标志的方式：SR &= ~位掩码
         * 注意：不能写 1 清除，写 1 没有效果。
         */
        TIM3->SR &= ~TIM_SR_CC1IF;
    }
}

/*
 * main —— 主函数
 *
 * 初始化流程：
 *   1. 配置系统时钟到 72MHz
 *   2. 初始化 PC13 LED
 *   3. 初始化 TIM2 PWM 输出（1kHz）
 *   4. 初始化 PA6 输入引脚
 *   5. 初始化 TIM3 输入捕获
 *
 * 主循环逻辑：
 *   等待 g_capture_ready，然后检查 g_period_ticks 是否接近 1000。
 *
 * 为什么判断 990 ~ 1010？
 *   理论值是 1000，但实际信号可能存在微小抖动（几微秒的误差）。
 *   ±1% 的容差是合理的工程实践。
 *
 * LED 控制：
 *   PC13 低电平点亮，高电平熄灭。
 *   BRR (Bit Reset Register)：写 1 输出低电平 → LED 亮
 *   BSRR (Bit Set Reset Register)：低 16 位写 1 输出高电平 → LED 灭
 */
int main(void)
{
    system_clock_72mhz_init();     /* 配时钟到 72MHz */
    led_pc13_init();               /* 配 PC13 LED（初始灭） */
    tim2_pwm_output_init();        /* TIM2_CH1 在 PA0 输出 1kHz 方波 */
    pa6_input_pin_init();          /* PA6 浮空输入，接收 TIM3_CH1 信号 */
    tim3_input_capture_init();     /* TIM3_CH1 在 PA6 做输入捕获 */

    while (1) {
        /*
         * 如果已经有新的捕获数据就绪
         */
        if (g_capture_ready != 0U) {

            /*
             * 检查周期是否在 990~1010 范围内
             * 即被测信号频率是否接近 1kHz
             */
            if ((g_period_ticks >= 990U) && (g_period_ticks <= 1010U)) {
                /*
                 * 频率正确 → LED 亮
                 *
                 * BRR (Bit Reset Register)：
                 *   低 16 位对应引脚，写 1 输出低电平。
                 *   GPIO_BRR_BR13 展开为 (1U << 13)。
                 *   PC13 输出低电平 → LED 点亮。
                 */
                GPIOC->BRR = GPIO_BRR_BR13;
            } else {
                /*
                 * 频率不正确 → LED 灭
                 *
                 * BSRR (Bit Set Reset Register)：
                 *   低 16 位写 1 → 对应引脚输出高电平。
                 *   GPIO_BSRR_BS13 展开为 (1U << 13)。
                 *   PC13 输出高电平 → LED 熄灭。
                 */
                GPIOC->BSRR = GPIO_BSRR_BS13;
            }

            /*
             * 可选：清标志，准备下一次捕获
             * 这里选择不清除，让主循环持续判断最新的有效数据。
             * 如果每次都清，两个捕获之间 LED 会灭一下。
             */
        }
    }
}