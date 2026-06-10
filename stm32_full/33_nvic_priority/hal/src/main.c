#include "stm32f1xx_hal.h"

/*
 * ============================================================================
 * HAL 版 NVIC 优先级实验
 * ============================================================================
 *
 * 本文件演示 HAL 库封装下的 NVIC 优先级配置。
 * 相比寄存器版，HAL 库把底层寄存器的操作包装成更易用的 API 函数，
 * 但底层原理完全相同。
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                              | 对应寄存器操作              |
 * |--------------------------------------|-----------------------------|
 * | HAL_TIM_Base_Start_IT(&htim2)        | DIER.UIE + CR1.CEN          |
 * | HAL_NVIC_SetPriority(IRQn, pre, sub) | 写 NVIC->IPRx 优先级寄存器  |
 * | HAL_NVIC_EnableIRQ(IRQn)             | 写 NVIC->ISER 使能寄存器    |
 * | HAL_GPIO_EXTI_IRQHandler(PIN_0)      | 检查 PR0 + 清除 PR0 + 回调  |
 * | HAL_TIM_IRQHandler(&htim2)           | 检查 SR.UIF + 清除 + 回调   |
 *
 * 注意：HAL 的 Callback 不是魔法！
 * 它的本质是：HAL 的 IRQHandler 入口函数接收中断→检查并清除标志→
 * 调用用户提供的回调函数。因此回调函数中只写业务逻辑，不做清标志操作。
 *
 * ██████  Demo 观察方式 ██████
 * 和寄存器版完全一致：
 * - TIM2 低优先级中断让 LED 长亮（约 2 秒一次）
 * - EXTI0 高优先级中断插入一个反向短脉冲
 * - 长亮期间按键能插入短脉冲 → 证明抢占
 */

static TIM_HandleTypeDef htim2;  /* TIM2 句柄，HAL 通过这个结构体管理 TIM2 的状态 */

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void tim2_init(void);
static void busy_delay(volatile uint32_t n);

int main(void)
{
    /*
     * HAL_Init() 必须第一个调用。
     * 它内部做了三件重要的事：
     * 1. 配置 Flash 预取缓冲和等待周期（默认值）
     * 2. 配置 SysTick 定时器，用于 HAL_Delay() 的时间基准
     * 3. 初始化全局中断优先级分组（默认 NVIC_PRIORITYGROUP_4）
     *
     * 关于优先级分组 NVIC_PRIORITYGROUP_4：
     *   - 4 位全部用作抢占优先级，没有子优先级
     *   - 所以 HAL_NVIC_SetPriority 的第三个参数（子优先级）不起作用
     *   - 这正适合本课的"只比较抢占优先级"的教学需求
     */
    HAL_Init();
    system_clock_72mhz_init();
    gpio_init();
    tim2_init();

    /*
     * HAL_TIM_Base_Start_IT() 同时做了两件事：
     * 1. 使能 TIM2 更新中断（对应 TIM2->DIER.UIE = 1）
     * 2. 启动 TIM2 计数器（对应 TIM2->CR1.CEN = 1）
     *
     * 返回 HAL_OK 表示成功，否则初始化失败
     */
    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
        /* 初始化失败，进入死循环等待调试器介入 */
        while (1) {
        }
    }

    while (1) {
    }
}

/*---------------------------------------------------------------------------*
 * 忙等待延时函数（仅供教学演示使用）
 *---------------------------------------------------------------------------*/
static void busy_delay(volatile uint32_t n)
{
    while (n-- > 0U) {
        __NOP();
    }
}

/*---------------------------------------------------------------------------*
 * GPIO 和 EXTI 初始化
 *---------------------------------------------------------------------------*/
static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};  /* 结构体初始化为 0，避免未初始化字段的随机值 */

    /*
     * 使能 GPIO 时钟。
     * HAL 库使用宏 __HAL_RCC_xxx_CLK_ENABLE() 来打开时钟，
     * 本质上就是写 RCC->APB2ENR 或 RCC->APB1ENR 对应位。
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();  /* 对应 RCC->APB2ENR |= RCC_APB2ENR_IOPCEN */
    __HAL_RCC_GPIOA_CLK_ENABLE();  /* 对应 RCC->APB2ENR |= RCC_APB2ENR_IOPAEN */
    /* 注意：HAL 的 EXTI 初始化不依赖 AFIO 时钟手动开启，HAL_GPIO_Init 内部会处理 */

    /*
     * PC13 输出配置（板载 LED）
     * GPIO_MODE_OUTPUT_PP：推挽输出
     * GPIO_NOPULL：不使用上拉/下拉
     * GPIO_SPEED_FREQ_LOW：低速模式，适合 LED 这种简单控制
     *
     * 初始化后写 SET（高电平），对应 LED 灭（低电平点亮）
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /*
     * PA0 下降沿外部中断配置
     *
     * HAL_GPIO_Init() 内部完成的工作：
     * 1. 配置 GPIOA->CRL 为输入模式（MODE0=00, CNF0=10 上拉输入）
     * 2. 配置内部上拉（写 GPIOA->BSRR.BS0）
     * 3. 通过 AFIO->EXTICR[0] 选择 PA0 作为 EXTI0 的来源
     * 4. 设置 EXTI->IMR.MR0 = 1（中断不屏蔽）
     * 5. 设置 EXTI->FTSR.TR0 = 1（下降沿触发）
     * 6. 清除 EXTI->PR.PR0（清除可能的旧标志）
     *
     * GPIO_MODE_IT_FALLING = 下降沿触发的外部中断模式
     * GPIO_PULLUP = 内部上拉，确保未按下时为高电平
     *
     * 这一行封装了寄存器版中约 10 行配置代码！
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * 配置 EXTI0 的 NVIC 优先级
     * HAL_NVIC_SetPriority(IRQn, PreemptPriority, SubPriority)
     * - 第一个参数 0：抢占优先级（数字越小越高）
     * - 第二个参数 0：子优先级（本课优先级分组下无效）
     *
     * 所以 EXTI0 抢占优先级 = 0（最高）
     * TIM2 抢占优先级 = 2（较低）
     * EXTI0 可以抢占 TIM2
     */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/*---------------------------------------------------------------------------*
 * TIM2 初始化
 *---------------------------------------------------------------------------*/
static void tim2_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();  /* 打开 TIM2 时钟 ← 对应 RCC->APB1ENR.TIM2EN */

    /*
     * TIM2 句柄配置
     *
     * Instance：选择使用哪个定时器
     * Prescaler：预分频值（对应 TIM2->PSC）
     * CounterMode：计数模式（向上计数、向下计数、中央对齐）
     * Period：自动重装值（对应 TIM2->ARR）
     * ClockDivision：时钟分频（用于死区时间等高级功能）
     * AutoReloadPreload：自动重装预装载（更新事件时是否立即生效 ARR 的新值）
     *
     * 计数频率计算：
     *   定时器时钟 = 72MHz（原因见寄存器版注释）
     *   Prescaler = 7200 - 1 → 72MHz / 7200 = 10kHz
     *   Period = 20000 - 1 → 10kHz / 20000 = 0.5Hz → 每 2 秒一次中断
     */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 7200U - 1U;           /* PSC 值 */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP; /* 向上计数 */
    htim2.Init.Period = 20000U - 1U;             /* ARR 值 */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;  /* TDTS = Tck_tim */
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; /* 立即生效 */
    /*
     * HAL_TIM_Base_Init() 调用后内部写 PSC/ARR/CR1 等寄存器。
     * 如果这里失败，后面的 HAL_TIM_Base_Start_IT() 即使被调用，
     * 也没有可靠的定时器配置可用，所以必须停下来调试。
     */
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
        while (1) {
        }
    }

    /*
     * TIM2 优先级设为 2，低于 EXTI0（优先级 0）
     * 所以当 TIM2 中断正在执行时，EXTI0 可以抢占
     *
     * 中断控制器在响应 IRQ 时会自动屏蔽同优先级或更低优先级的中断，
     * 所以 TIM2 执行时，不会再次被 TIM2 打断，
     * 但可以被更高优先级的 EXTI0 打断。
     */
    HAL_NVIC_SetPriority(TIM2_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

/*---------------------------------------------------------------------------*
 * EXTI 回调函数（用户业务逻辑入口）
 *---------------------------------------------------------------------------*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /*
     * 这个函数被谁调用？
     *
     * 调用链：
     *   PA0 按下 → EXTI0 触发 → CPU 进入 EXTI0_IRQHandler()
     *   → EXTI0_IRQHandler() 调用 HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)
     *   → HAL_GPIO_EXTI_IRQHandler() 检查并清除 EXTI->PR.PR0
     *   → 然后调用这个 HAL_GPIO_EXTI_Callback(GPIO_PIN_0)
     *
     * 所以这里只需要写业务逻辑，不需要清标志！
     * 这就是 HAL 设计的"IRQHandler 清标志 + Callback 写业务"模式。
     */
    if (GPIO_Pin == GPIO_PIN_0) {
        /*
         * 反向短脉冲（和寄存器版完全相同的逻辑）：
         *   - 平时 LED 灭，按键会让它短亮一下
         *   - TIM2 长亮期间按键，会让它短灭一下
         *   - 这个"长亮中被插入的短灭"就是抢占的证据
         */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        busy_delay(900000U);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

/*---------------------------------------------------------------------------*
 * TIM 周期事件回调函数（用户业务逻辑入口）
 *---------------------------------------------------------------------------*/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    /*
     * 这个函数被谁调用？
     *
     * 调用链：
     *   TIM2 更新事件 → CPU 进入 TIM2_IRQHandler()
     *   → TIM2_IRQHandler() 调用 HAL_TIM_IRQHandler(&htim2)
     *   → HAL_TIM_IRQHandler() 检查并清除 TIM2->SR.UIF
     *   → 判断是更新事件，调用这个 HAL_TIM_PeriodElapsedCallback()
     *
     * 同样，这里只写业务逻辑，不清标志。
     *
     * 参数 htim 是触发事件的定时器句柄，
     * 可以用来区分是哪个定时器触发（如果有多个定时器使用同一个回调）。
     */
    if (htim->Instance == TIM2) {
        /*
         * LED 长亮窗口
         * HAL_GPIO_WritePin(PIN, GPIO_PIN_RESET) = 输出低电平 = LED 亮
         * busy_delay(5000000U) 创造可观察的抢占窗口
         */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED 亮 */
        busy_delay(5000000U);  /* 长窗口，等待 EXTI0 抢占 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED 灭 */
    }
}

/*---------------------------------------------------------------------------*
 * 中断服务函数（硬件入口）
 *---------------------------------------------------------------------------*/

/*
 * EXTI0_IRQHandler() 是硬件中断向量表中的入口。
 * 它本身不处理业务，而是委托给 HAL_GPIO_EXTI_IRQHandler()。
 * HAL 内部会检查标志、清除标志，然后调用用户的回调函数。
 *
 * 这是一种"分层设计"：
 *   硬件层：EXTI0_IRQHandler（在启动文件中定义，由硬件直接跳转）
 *   HAL 层：HAL_GPIO_EXTI_IRQHandler（检查清除标志）
 *   用户层：HAL_GPIO_EXTI_Callback（写业务逻辑）
 */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/*
 * SysTick 中断服务函数
 * HAL_Init() 内部配置了 SysTick 每 1ms 产生一次中断，
 * HAL_IncTick() 递增一个全局 tick 计数器，
 * 供 HAL_Delay() 和 HAL_GetTick() 使用。
 *
 * 注意：SysTick 中断优先级默认是最低的（15），
 * 所以不会影响本课的 EXTI0（优先级 0）和 TIM2（优先级 2）的抢占实验。
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/*---------------------------------------------------------------------------*
 * 系统时钟配置：8MHz HSE → PLL ×9 → 72MHz
 *---------------------------------------------------------------------------*/
static void system_clock_72mhz_init(void)
{
    /*
     * HAL 的系统时钟配置使用结构体 RCC_OscInitTypeDef 和 RCC_ClkInitTypeDef，
     * 而不是像寄存器版那样直接操作 RCC->CFGR 等寄存器。
     * 但最终写入的硬件寄存器是完全相同的。
     *
     * RCC_OscInitTypeDef：配置振荡器（HSE、HSI、PLL 等）
     * RCC_ClkInitTypeDef：配置系统时钟、AHB/APB 分频等
     */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* 配置 HSE（8MHz）和 PLL（×9 → 72MHz） */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;     /* 选择配置 HSE */
    osc.HSEState = RCC_HSE_ON;                       /* 使能 HSE */
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;        /* HSE 预分频：不分频（8MHz） */
    osc.PLL.PLLState = RCC_PLL_ON;                   /* 使能 PLL */
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;           /* PLL 输入源选择 HSE */
    osc.PLL.PLLMUL = RCC_PLL_MUL9;                   /* PLL 倍频：×9 → 72MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) {
        }
    }

    /* 配置系统时钟和总线分频 */
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;      /* SYSCLK = PLL 输出 = 72MHz */
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;             /* AHB 不分频 → HCLK = 72MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;              /* APB1 二分频 → PCLK1 = 36MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;              /* APB2 不分频 → PCLK2 = 72MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        while (1) {
        }
    }
}
