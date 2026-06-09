#include "stm32f1xx.h"

/*
 * ============================================================================
 * 寄存器版 NVIC 优先级实验
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. NVIC（Nested Vectored Interrupt Controller，嵌套向量中断控制器）
 *    - 属于 Cortex-M3 内核，不属于 STM32 的某个外设
 *    - 负责接收各外设的中断请求，根据优先级进行仲裁，决定 CPU 先响应谁
 *    - 支持中断嵌套：高优先级中断可以抢占正在执行的低优先级中断
 *
 * 2. 中断优先级规则
 *    - 优先级数字越小，优先级越高（与直觉相反，务必牢记）
 *    - 本课：EXTI0 优先级 = 0（高），TIM2 优先级 = 2（低）
 *    - 所以 EXTI0 可以抢占 TIM2
 *
 * 3. 外设中断使能 vs NVIC 使能（"两道门"模型）
 *    - 第一道门（外设侧）：TIM2->DIER.UIE 或 EXTI->IMR.MR0
 *      允许外设在事件发生时向 NVIC 发出中断请求信号
 *    - 第二道门（NVIC 侧）：NVIC_EnableIRQ()
 *      允许 NVIC 接收并响应某个中断源
 *    - 两道门必须都打开，CPU 才会进入对应的中断处理函数
 *
 * 4. 中断标志清除
 *    - 进入中断处理函数后，必须手动清除中断标志位
 *    - 否则退出中断后，标志位仍然置位，CPU 会立即再次进入
 *    - EXTI->PR 是写 1 清除，TIM2->SR 是写 0 清除
 *    - 不同外设的清标志方式可能不同，务必查阅参考手册
 *
 * ██████  Demo 演示现象 ██████
 *
 * - TIM2 每 2 秒进入一次低优先级中断（优先级 2），让 PC13 LED 长亮 0.5 秒
 * - PA0 按键触发 EXTI0 高优先级中断（优先级 0），让 LED 做一次"反向短脉冲"
 * - 关键观察：如果在 TIM2 长亮窗口里按下 PA0，LED 会立刻短暂熄灭再亮回去
 *   这说明 EXTI0 成功抢占了正在执行的 TIM2 中断
 *
 * ██████  教学说明 ██████
 *
 * 本实验故意在 TIM2_IRQHandler 里做忙等待 500 万次循环，目的只有一个：
 * 制造一个肉眼可见的"低优先级中断执行窗口"，让你有机会在窗口期内按下按键
 * 真实项目中绝对不允许在中断服务函数里做长时间忙等待！
 */

/*---------------------------------------------------------------------------*
 * 系统时钟配置：8MHz HSE -> PLL x9 -> 72MHz SYSCLK
 *---------------------------------------------------------------------------*/
static void system_clock_72mhz_init(void)
{
    /*
     * 第一步：配置 Flash 预取缓冲和等待周期
     * 72MHz 运行时，Flash 需要 2 个等待周期才能稳定读取
     * PRFTBE = 预取缓冲使能，提高指令读取效率
     */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /*
     * 第二步：使能 HSE（外部高速晶振，8MHz）
     */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /*
     * 第三步：配置 PLL 相关分频和倍频
     * - HPRE（AHB 预分频）= DIV1 → HCLK = SYSCLK = 72MHz
     * - PPRE1（APB1 预分频）= DIV2 → PCLK1 = 36MHz（APB1 最大允许 36MHz）
     * - PPRE2（APB2 预分频）= DIV1 → PCLK2 = 72MHz
     * - PLLSRC = HSE（选择 HSE 作为 PLL 输入）
     * - PLLMULL = x9 → PLL 输出 = 8MHz × 9 = 72MHz
     *
     * 注意：APB1 分频不为 1 时，定时器时钟（TIM2-7）实际 = PCLK1 × 2 = 72MHz
     * 这是 STM32F1 的特殊设计，目的在于让定时器即使挂在 36MHz 总线上也能得到 72MHz
     */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    /*
     * 第四步：使能 PLL，等待 PLL 锁定
     */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    /*
     * 第五步：切换系统时钟到 PLL 输出
     * SWS 位会反映当前实际使用的系统时钟源
     */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

/*---------------------------------------------------------------------------*
 * 软件延时函数（忙等待，仅供教学演示使用）
 *---------------------------------------------------------------------------*/
static void delay_busy(volatile uint32_t n)
{
    /*
     * volatile 关键字告诉编译器：这个变量的值可能被意想不到地改变
     * 防止编译器优化掉这个空循环
     * __NOP() 是一条汇编空指令，消耗 1 个 CPU 周期
     */
    while (n-- > 0U) {
        __NOP();
    }
}

/*---------------------------------------------------------------------------*
 * PC13 板载 LED 初始化
 *---------------------------------------------------------------------------*/
static void led_pc13_init(void)
{
    /*
     * PC13 是 BluePill 板载 LED 所在引脚
     *
     * BluePill 常见设计：低电平点亮 LED，高电平熄灭 LED
     *   - 写 BRR（Bit Reset Register）：输出 0，LED 亮
     *   - 写 BSRR（Bit Set Reset Register）：输出 1，LED 灭
     *
     * 为什么不用 ODR 直接写？
     *   ODR 是"读-改-写"，在多中断环境下可能导致引脚电平异常
     *   BSRR/BRR 是原子操作，只影响指定的引脚，更安全
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;          // 打开 GPIOC 时钟
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13); // 清除旧配置
    GPIOC->CRH |= GPIO_CRH_MODE13_1;              // MODE13=10：输出模式 2MHz
    GPIOC->BSRR = GPIO_BSRR_BS13;                 // 初始状态：LED 灭
}

/*---------------------------------------------------------------------------*
 * LED 控制函数
 *---------------------------------------------------------------------------*/
static void led_on(void)
{
    GPIOC->BRR = GPIO_BRR_BR13;   // BR13 写 1 → PC13 输出低电平 → LED 亮
}

static void led_off(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS13; // BS13 写 1 → PC13 输出高电平 → LED 灭
}

static void led_toggle(void)
{
    /*
     * 通过读取 ODR 的当前状态来判断 LED 是亮是灭
     * 取反后写入对应的 BSRR 或 BRR
     * 注意：ODR 读出来的是当前引脚输出电平值
     */
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        led_on();     // 当前灭 -> 点亮
    } else {
        led_off();    // 当前亮 -> 熄灭
    }
}

/*---------------------------------------------------------------------------*
 * PA0 按键 EXTI0 中断初始化
 *---------------------------------------------------------------------------*/
static void key_pa0_exti_init(void)
{
    /*
     * ██ 为什么同时打开 GPIOA 和 AFIO 时钟？██
     *
     * GPIOA 时钟：必须打开才能访问 GPIOA 的寄存器（CRL、IDR、BSRR 等）
     * AFIO 时钟：EXIT0 可以来自 PA0/PB0/PC0/PD0...
     *           需要通过 AFIO 的 EXTICR 寄存器选择具体来自哪个 GPIO 端口
     *           要写 AFIO 寄存器，就必须先打开 AFIO 时钟
     *
     * 这回答了初学者常问的问题：
     * "为什么 GPIO 中断要多开一个 AFIO 时钟？"
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /*
     * 配置 PA0 为上拉输入
     * MODE0 = 00：输入模式（输出配置无效）
     * CNF0  = 10：上拉/下拉输入
     * ODR0  = 1 ：选择内部上拉（写入 BSRR.BS0）
     *
     * 按键另一端接 GND，所以：
     * - 未按下：PA0 被内部上拉为高电平（3.3V）
     * - 按下时：PA0 通过按键被拉到 GND，变成低电平
     * - 电平变化方向：从高到低 → 下降沿
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;

    /*
     * 配置 AFIO 的 EXTICR[0] 寄存器，选择 EXTI0 的信号来源
     * EXTICR[0] 控制 EXTI[3:0] 的端口映射
     * EXTI0 的 4 位控制字段：
     *   - 0000：PA0  ← 本课选择这个
     *   - 0001：PB0
     *   - 0010：PC0
     *   - 0011：PD0
     *   ...
     */
    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;

    /*
     * ██ 三道子开关 ██
     *
     * 第一道：EXTI->IMR（Interrupt Mask Register）
     *   IMR.MR0 = 1：允许 EXTI0 把中断请求信号发送到 NVIC
     *   相当于外设侧的中断总闸
     *
     * 第二道：EXTI->FTSR（Falling Trigger Selection Register）
     *   FTSR.TR0 = 1：选择下降沿作为触发条件
     *   即 PA0 从高电平跳变到低电平时产生中断请求
     *
     * 第三道：EXTI->RTSR（Rising Trigger Selection Register）
     *   RTSR.TR0 = 0：不选择上升沿触发
     *   本课只需要下降沿触发，所以把上升沿关掉
     *
     * 注意：IMR 只是允许"传递请求"，真正让 CPU 响应的开关在 NVIC
     * 这是许多初学者的理解盲区。
     */
    EXTI->IMR |= EXTI_IMR_MR0;
    EXTI->FTSR |= EXTI_FTSR_TR0;
    EXTI->RTSR &= ~EXTI_RTSR_TR0;

    /*
     * 清除 EXTI0 挂起标志
     * 初始化阶段可能有残留的中断标志（比如上电时 PA0 电平不稳定）
     * 如果不先清除，会使能 NVIC 后可能立刻进入一次"幽灵中断"
     *
     * 注意：EXTI->PR 是"写 1 清除"（Write 1 to Clear）
     * 这和 TIM2->SR 的"写 0 清除"不同，务必区分！
     */
    EXTI->PR = EXTI_PR_PR0;

    /*
     * ██ NVIC 优先级配置 ██
     *
     * NVIC_SetPriority(IRQn, priority) 这个函数：
     *   1. 找到该 IRQn 对应的 NVIC->IPRx 寄存器（优先级数组）
     *   2. 把 priority 写入该寄存器的高 4 位（STM32F1 只使用高 4 位）
     *   3. STM32F103 使用 4 位优先级，所以有效值为 0~15
     *
     * 优先级数字越小 → 抢占能力越强
     * EXTI0 = 0（最高），TIM2 = 2（较低）
     * 因此当 TIM2 中断正在执行时，EXTI0 可以插入打断
     *
     * NVIC_EnableIRQ(IRQn)：
     *   设置 NVIC->ISER（Interrupt Set Enable Register）对应位
     *   ISER 是 NVIC 侧的"中断使能开关"
     */
    NVIC_SetPriority(EXTI0_IRQn, 0U);  // EXTI0 优先级：0（高）
    NVIC_EnableIRQ(EXTI0_IRQn);        // 使能 EXTI0 中断
}

/*---------------------------------------------------------------------------*
 * TIM2 定时器更新中断初始化
 *---------------------------------------------------------------------------*/
static void tim2_interrupt_init(void)
{
    /*
     * TIM2 挂在 APB1 总线上
     * APB1 时钟使能寄存器是 RCC->APB1ENR（不是 APB2ENR！）
     * TIM1/TIM8 在 APB2 上，TIM2-7 在 APB1 上，这是一个容易搞混的细节
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * ██ TIM2 计数频率计算 ██
     *
     * 系统时钟配置：
     *   SYSCLK = 72MHz
     *   PCLK1 = 36MHz（APB1 最大允许 36MHz）
     *
     * STM32F1 的特殊机制：
     *   当 APB1 分频系数 != 1 时，APB1 上的定时器时钟 = PCLK1 × 2
     *   这里 PPRE1 = DIV2，所以 TIM2 实际计数时钟 = 36MHz × 2 = 72MHz
     *
     * 本课计数器参数：
     *   PSC = 7200 - 1 → 72MHz / 7200 = 10kHz（计数器递增频率）
     *   ARR = 20000 - 1 → 10kHz / 20000 = 0.5Hz
     *   所以 TIM2 大约每 2 秒产生一次更新事件（溢出）
     *
     * 为什么 PSC 和 ARR 都要减 1？
     *   因为计数器从 0 开始计数，到达设定值时溢出
     *   计数周期 = (PSC+1) × (ARR+1) / 时钟频率
     */
    TIM2->PSC = 7200U - 1U;    // 预分频值
    TIM2->ARR = 20000U - 1U;   // 自动重装值

    /*
     * 清除更新中断标志 UIF
     * 上电后 SR 寄存器可能有随机值，先清一下确保稳定
     * 注意：TIM 的 SR 寄存器是"写 0 清除"，和 EXTI 的 PR 相反
     */
    TIM2->SR &= ~TIM_SR_UIF;

    /*
     * 使能 TIM2 更新中断
     * DIER（DMA/Interrupt Enable Register）
     * UIE（Update Interrupt Enable）：更新事件发生时，允许 TIM2 发出中断请求
     *
     * 重要理解：
     *   这一步只是让 TIM2"可以发出"中断请求信号
     *   相当于外设侧的第一道门
     *   NVIC 侧的第二道门需要 NVIC_EnableIRQ() 来打开
     */
    TIM2->DIER |= TIM_DIER_UIE;

    /*
     * 设置 TIM2 的 NVIC 优先级为 2
     * EXTI0 优先级为 0，所以 TIM2 比 EXTI0 低
     * 当 EXTI0 来临时，TIM2 中断会被抢占
     */
    NVIC_SetPriority(TIM2_IRQn, 2U);
    NVIC_EnableIRQ(TIM2_IRQn);

    /*
     * 启动 TIM2 计数器
     * CR1.CEN（Counter ENable）：置 1 后计数器开始递增
     * 在这之前的所有配置都在准备阶段
     * 这一句才真正让 TIM2 跑起来
     */
    TIM2->CR1 |= TIM_CR1_CEN;
}

/*---------------------------------------------------------------------------*
 * EXTI0 中断服务函数
 *---------------------------------------------------------------------------*/
void EXTI0_IRQHandler(void)
{
    /*
     * 进入中断后，第一步必须先检查中断源并清除标志
     * 这是因为多个 EXTI 线可以共享同一个中断向量（如 EXTI9_5_IRQHandler）
     * 检查标志可以确认究竟是谁触发了中断
     *
     * 注意这里学习的一个关键设计模式：
     *   中断服务函数 = 检查标志 → 清除标志 → 执行业务逻辑
     *   这个顺序不能颠倒，否则可能出现"清除了标志但还没处理又来一次"的问题
     */
    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        /*
         * 清除 EXTI0 挂起标志
         * EXTI 的 PR 寄存器是"写 1 清除"（W1C）
         * 直接向 PR0 位写 1 即可清除
         */
        EXTI->PR = EXTI_PR_PR0;

        /*
         * 执行反向短脉冲
         *
         * 此时可能有三种情况：
         * 情况 1：LED 正在灭（不在 TIM2 中断窗口内）
         *   → led_toggle() 让 LED 亮 → 等待 → led_toggle() 让 LED 灭
         *   结果：按键后 LED 短暂亮一下
         *
         * 情况 2：LED 正在亮（在 TIM2 中断窗口内）
         *   → led_toggle() 让 LED 灭 → 等待 → led_toggle() 让 LED 亮
         *   结果：LED 长亮期间突然短暂熄灭又恢复
         *   这个现象就是"抢占"的肉眼可见证据！
         *
         * 情况 3：LED 正在执行 EXTI0 中断时又按了一次按键
         *   本课优先级足够简单，同优先级中断不会相互抢占
         *   新的 EXTI0 请求会被挂起，等当前中断结束后再处理
         */
        led_toggle();
        delay_busy(900000U);  // 短脉冲宽度控制（约 10ms 量级）
        led_toggle();
    }
}

/*---------------------------------------------------------------------------*
 * TIM2 中断服务函数
 *---------------------------------------------------------------------------*/
void TIM2_IRQHandler(void)
{
    /*
     * 同样是"检查标志 → 清除标志 → 处理业务"的模式
     */
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        /*
         * 清除更新中断标志
         * TIM 的 SR 寄存器是"写 0 清除"
         * 所以这里的操作是 &= ~（清除对应位）
         */
        TIM2->SR &= ~TIM_SR_UIF;

        /*
         * 低优先级中断里的长亮窗口
         * delay_busy(5000000U) 大约会消耗几百毫秒
         * 在这段时间内按下 PA0，如果优先级配置正确：
         *   CPU 暂停执行这个函数 → 跳到 EXTI0_IRQHandler
         *   EXTI0_IRQHandler 执行完毕 → 回到这里继续执行
         *
         * 这就是"抢占"的完整过程。
         * 如果没有这个长窗口，抢占发生的时间太短，
         * 肉眼几乎无法观察到 LED 的变化。
         */
        led_on();
        delay_busy(5000000U);  // 长亮窗口（约几百毫秒）
        led_off();
    }
}

/*---------------------------------------------------------------------------*
 * 主函数
 *---------------------------------------------------------------------------*/
int main(void)
{
    /*
     * 初始化顺序说明：
     * 1. 系统时钟必须先配置，因为所有外设时钟都基于系统时钟树
     * 2. LED 初始化放在前面，可以用于输出调试信号
     * 3. EXTI0 和 TIM2 的初始化顺序不影响结果，因为 NVIC 还没启动调度
     *
     * 真正重要的时刻发生在 TIM2 启动后的第一次更新事件，
     * 也就是运行约 2 秒后 LED 第一次长亮时。
     */
    system_clock_72mhz_init();
    led_pc13_init();
    key_pa0_exti_init();
    tim2_interrupt_init();

    while (1) {
        /*
         * 主循环故意什么都不做。
         *
         * 这和有 while(1) 主循环的轮询程序形成了鲜明对比：
         *   - 轮询：CPU 一直在跑循环，检查条件
         *   - 中断：CPU 可以执行其他任务（或进入低功耗），有事件时再响应
         *
         * 本课所有可观察现象都来自 TIM2 中断和 EXTI0 中断服务函数，
         * 主循环纯粹是一个空壳。
         *
         * 这也反过来说明了另一件事：
         * 中断发生后，CPU 执行中断函数，然后继续执行主循环。
         * "中断"不是"代替"主循环，而是"插入"到主循环中执行。
         */
    }
}