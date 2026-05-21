#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL 版 输入捕获"。
 *
 * 目标：
 * 1. 用 TIM2_CH1 在 PA0 输出 1kHz PWM
 * 2. 用导线把 PA0 接到 PA6
 * 3. 用 TIM3_CH1 在 PA6 进行输入捕获
 * 4. 计算相邻捕获值差，并判断频率是否正确
 *
 * 整体链路与寄存器版完全相同：
 *   TIM2 (PWM 输出 1kHz) → PA0 ──杜邦线── PA6 → TIM3_CH1 (输入捕获)
 *
 * 与寄存器版的区别：
 *   寄存器版直接操作内存映射的寄存器地址。
 *   HAL 版通过结构体（句柄）封装外设，通过函数调用间接操作寄存器。
 *
 *   理解寄存器版有助于理解 HAL 内部做了什么。
 *   理解 HAL 版有助于在实际项目中更高效地开发。
 */

/*
 * 全局变量
 *
 * 注意 HAL 版使用的是 uint32_t，而不是 uint16_t。
 * 因为 HAL_TIM_ReadCapturedValue() 返回 uint32_t。
 *
 * volatile 关键字的原因与寄存器版相同：
 *   中断中修改，主循环中读取，不要被编译器优化掉。
 */
static volatile uint32_t g_last_capture = 0U;     /* 上一次捕获到的 CCR1 值 */
static volatile uint32_t g_period_ticks = 0U;     /* 本次捕获值 - 上次捕获值 = 周期 */
static volatile uint8_t  g_capture_ready = 0U;    /* 中断置 1，主循环检测处理 */

/*
 * 句柄（Handle）变量
 *
 * HAL 将外设抽象为"句柄"结构体。每个外设对应一个句柄。
 * 句柄包含了外设的基地址（Instance）、初始化参数（Init）、
 * 锁状态（Lock）、状态（State）等信息。
 *
 * 初始化时填充 Init 成员，然后调用 HAL_TIM_PWM_Init / HAL_TIM_IC_Init
 * 将这些配置写入实际寄存器。
 */
static TIM_HandleTypeDef htim2;    /* TIM2 句柄 —— PWM 输出 */
static TIM_HandleTypeDef htim3;    /* TIM3 句柄 —— 输入捕获 */

/* 函数前置声明 —— 因为 main 先调用它们 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void gpio_pa0_pwm_init(void);
static void gpio_pa6_input_init(void);
static void tim2_pwm_init(void);
static void tim3_input_capture_init(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * 与寄存器版的 main 流程完全对应：
 *   system_clock_72mhz_init  → 同左
 *   led_pc13_init            → 同左
 *   gpio_pa0_pwm_init        → 对应 tim2_pwm_output_init 中的 GPIO 部分
 *   gpio_pa6_input_init      → 对应 pa6_input_pin_init
 *   tim2_pwm_init            → 对应 tim2_pwm_output_init 中的 TIM 部分
 *   tim3_input_capture_init  → 对应 tim3_input_capture_init
 *
 * 区别：
 *   HAL 版将"启动"（Start）与"初始化"（Init）分开。
 *   初始化只配寄存器，启动使能输出/中断/计数器。
 *
 *   寄存器版则在 init 函数的最后一行直接 CEN = 1 启动了。
 */
int main(void)
{
    /*
     * HAL_Init —— HAL 库的"大管家"初始化
     *
     * 这个函数内部做了两件事：
     *   1. 配置 Flash 预取缓冲（类似寄存器版的 FLASH->ACR）
     *   2. 配置 SysTick 为 1ms 中断
     *      SysTick 是 HAL 库的"心跳"——HAL_Delay()、超时机制都依赖它
     *
     * 如果 main 中不先调 HAL_Init()，后面很多 HAL 函数会工作不正常。
     */
    HAL_Init();

    system_clock_72mhz_init();   /* 配时钟到 72MHz（通过 HAL RCC 函数） */
    led_pc13_init();             /* 配 PC13 LED（通过 HAL GPIO 函数） */
    gpio_pa0_pwm_init();         /* 配 PA0 为复用推挽输出（PWM 引脚） */
    gpio_pa6_input_init();       /* 配 PA6 为浮空输入（捕获引脚） */
    tim2_pwm_init();             /* 配 TIM2 为 PWM 输出模式 */
    tim3_input_capture_init();   /* 配 TIM3 为输入捕获模式 */

    /*
     * 启动 TIM2 的 PWM 输出
     *
     * 对应寄存器版：
     *   TIM2->CCER |= TIM_CCER_CC1E;   ← 已在 Init 中配置
     *   TIM2->CR1  |= TIM_CR1_CEN;     ← 由 HAL_TIM_PWM_Start 完成
     *
     * HAL_TIM_PWM_Start 内部会：
     *   1. 设置 CR1.CEN = 1
     *   2. 等待更新事件完成
     *   3. 返回 HAL_OK 或 HAL_ERROR
     */
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) {
        /*
         * 启动失败 → 进入错误处理
         */
        error_handler();
    }

    /*
     * 启动 TIM3 的输入捕获（中断模式）
     *
     * 对应寄存器版：
     *   TIM3->DIER |= TIM_DIER_CC1IE;   ← 由 Start_IT 内部完成
     *   TIM3->SR   &= ~TIM_SR_CC1IF;    ← 由 Start_IT 内部完成
     *   TIM3->CR1  |= TIM_CR1_CEN;      ← 由 Start_IT 内部完成
     *
     * 把这三步合并到一个函数调用中，是 HAL 的封装思想。
     */
    if (HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 主循环逻辑与寄存器版完全相同
         */
        if (g_capture_ready != 0U) {
            /*
             * 检查周期是否在 990~1010 范围内
             */
            if ((g_period_ticks >= 990U) && (g_period_ticks <= 1010U)) {
                /*
                 * 频率正确 → LED 亮（低电平）
                 *
                 * HAL_GPIO_WritePin 参数：
                 *   端口：GPIOC
                 *   引脚：GPIO_PIN_13
                 *   电平：GPIO_PIN_RESET = 0 = 低电平
                 */
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            } else {
                /*
                 * 频率不正确 → LED 灭（高电平）
                 *
                 * GPIO_PIN_SET = 1 = 高电平
                 */
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            }
        }
    }
}

/*
 * system_clock_72mhz_init —— HAL 版时钟配置
 *
 * 与寄存器版的区别：
 *   寄存器版直接操作 RCC->CR、RCC->CFGR 等寄存器。
 *   HAL 版使用 RCC_OscInitTypeDef 和 RCC_ClkInitTypeDef 两个结构体，
 *   然后调用 HAL_RCC_OscConfig 和 HAL_RCC_ClockConfig 完成配置。
 *
 * 最终结果完全相同：SYSCLK = 72MHz, HCLK = 72MHz, PCLK1 = 36MHz, PCLK2 = 72MHz。
 */
static void system_clock_72mhz_init(void)
{
    /*
     * RCC_OscInitTypeDef —— 振荡器配置结构体
     *
     * 用于配置 HSE、HSI、PLL 等振荡器相关的参数。
     */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 配置 HSE → PLL → 72MHz
     *
     * OscillatorType：指定要配置哪些振荡器
     * HSEState：打开 HSE
     * HSEPredivValue：HSE 预分频（STM32F103 有专门的预分频器）
     * PLL.PLLState：打开 PLL
     * PLL.PLLSource：PLL 时钟源 = HSE
     * PLL.PLLMUL：PLL 倍频 = x9
     */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;          /* HSE 不分频 */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;             /* PLL 输入 = HSE */
    osc.PLL.PLLMUL = RCC_PLL_MUL9;                     /* x9 → 72MHz */

    /*
     * HAL_RCC_OscConfig 内部做什么？
     *   1. 打开 HSE，等待 HSERDY
     *   2. 配置 PLL 时钟源和倍频
     *   3. 打开 PLL，等待 PLLRDY
     *
     * 这对应寄存器版中的：
     *   RCC->CR  |= RCC_CR_HSEON;
     *   while (!(RCC->CR & RCC_CR_HSERDY));
     *   RCC->CFGR |= RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
     *   RCC->CR  |= RCC_CR_PLLON;
     *   while (!(RCC->CR & RCC_CR_PLLRDY));
     */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置系统时钟、AHB、APB1、APB2 分频
     *
     * ClockType：指定要配置哪些时钟
     * SYSCLKSource：系统时钟源 = PLL
     * AHBCLKDivider：AHB 不分频 → HCLK = SYSCLK = 72MHz
     * APB1CLKDivider：APB1 2分频 → PCLK1 = 36MHz
     * APB2CLKDivider：APB2 不分频 → PCLK2 = 72MHz
     *
     * 第二个参数 FLASH_LATENCY_2：72MHz 需要 2 个 Flash 等待周期
     */
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    /*
     * HAL_RCC_ClockConfig 内部做什么？
     *   1. 配置 AHB/APB1/APB2 分频
     *   2. 切换 SYSCLK 到 PLL，等待 SWS 确认
     *   3. 更新 HAL 内部维护的 SystemCoreClock 变量
     *   4. 重新配置 SysTick 以匹配新的时钟频率
     *
     * 这对应寄存器版中的：
     *   RCC->CFGR |= HPRE_DIV1 | PPRE1_DIV2 | PPRE2_DIV1 | SW_PLL;
     *   while ((RCC->CFGR & SWS) != SWS_PLL);
     */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        error_handler();
    }
}

/*
 * led_pc13_init —— HAL 版初始化 PC13 LED
 *
 * 对应寄存器版：
 *   RCC->APB2ENR |= IOPCEN;
 *   GPIOC->CRH &= ~(MODE13 | CNF13);
 *   GPIOC->CRH |= MODE13_1;
 *   GPIOC->BSRR = BS13;
 */
static void led_pc13_init(void)
{
    /*
     * GPIO_InitTypeDef —— GPIO 初始化配置结构体
     *
     * HAL 统一用这个结构体配置所有 GPIO，不再区分 CRL/CRH。
     * 函数内部会根据引脚号自动选择 CRL 或 CRH。
     */
    GPIO_InitTypeDef gpio = {0};

    /*
     * __HAL_RCC_GPIOC_CLK_ENABLE —— 开启 GPIOC 时钟
     *
     * 这是一个宏，展开后等价于：
     *   RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 配置 PC13
     *
     * Pin：指定引脚，可以是 GPIO_PIN_13 或 GPIO_PIN_ALL
     * Mode：GPIO_MODE_OUTPUT_PP → 通用推挽输出
     * Pull：GPIO_NOPULL → 无上拉/下拉
     * Speed：GPIO_SPEED_FREQ_LOW → 低速（LED 不需要高频切换）
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /*
     * HAL_GPIO_Init 内部做什么？
     *   根据 Pin 计算出在 CRL/CRH 中的位置，
     *   然后写入 MODE 和 CNF 位。
     *
     * 对应寄存器版：
     *   GPIOC->CRH &= ~(MODE13 | CNF13);
     *   GPIOC->CRH |= MODE13_1;   // 50MHz 推挽输出
     */
    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 初始输出高电平 → LED 灭
     *
     * HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET) 内部：
     *   如果电平是 SET → 写 BSRR
     *   如果电平是 RESET → 写 BRR
     *
     * 对应寄存器版：
     *   GPIOC->BSRR = GPIO_BSRR_BS13;
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

/*
 * gpio_pa0_pwm_init —— 配置 PA0 为 PWM 输出引脚
 *
 * TIM2_CH1 的第二功能映射到 PA0。
 * 作为 PWM 输出引脚，PA0 必须配置为"复用推挽输出"。
 *
 * 对应寄存器版：
 *   GPIOA->CRL |= MODE0_1 | CNF0_1;  // 50MHz 复用推挽
 */
static void gpio_pa0_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 开启 GPIOA 时钟
     * 等价于 RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * 配置 PA0
     *
     * Mode = GPIO_MODE_AF_PP → 复用推挽输出
     *   普通推挽输出（OUTPUT_PP）是 CPU 直接控制引脚。
     *   复用推挽输出（AF_PP）是外设（如定时器）控制引脚。
     *
     * 如果配成 OUTPUT_PP 而非 AF_PP，
     *   TIM2_CH1 的 PWM 信号就无法到达 PA0 引脚，引脚始终由 GPIO 控制。
     *
     * Pull = GPIO_NOPULL → 无需上拉/下拉
     * Speed = LOW → 1kHz 信号低速即可
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_AF_PP;         /* 复用推挽输出 */
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * gpio_pa6_input_init —— 配置 PA6 为浮空输入
 *
 * PA6 是 TIM3_CH1 的输入引脚。
 * 输入捕获不需要配置为复用功能——定时器模块会自动读取引脚电平。
 * 只需要把引脚设为浮空输入即可。
 *
 * 对应寄存器版：
 *   GPIOA->CRL |= CNF6_0;  // 浮空输入
 *
 * 注意：HAL 中没有"复用输入"这种模式。
 * 对于作为外设输入的引脚，统一使用 GPIO_MODE_INPUT。
 */
static void gpio_pa6_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 开启 GPIOA 时钟
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * 配置 PA6
     *
     * Mode = GPIO_MODE_INPUT → 输入模式
     *   等价于寄存器版中 MODE6=00（输入模式）。
     *
     * Pull = GPIO_NOPULL → 浮空输入
     *   等价于寄存器版中 CNF6=01（浮空输入）。
     *   如果设为 GPIO_PULLUP，对应 CNF6=10（上拉输入）。
     *
     * Speed 在输入模式下不生效，可以忽略。
     */
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * tim2_pwm_init —— HAL 版 TIM2 PWM 初始化
 *
 * 对应寄存器版 tim2_pwm_output_init 中 TIM 相关的部分：
 *   RCC->APB1ENR |= TIM2EN;
 *   TIM2->PSC = 71;
 *   TIM2->ARR = 999;
 *   TIM2->CCR1 = 500;
 *   TIM2->CCMR1 = PWM1 模式;
 *   TIM2->CCER |= CC1E;
 *   TIM2->EGR |= UG;
 *
 * 不包含 GPIO 配置部分（已在 gpio_pa0_pwm_init 中完成）。
 */
static void tim2_pwm_init(void)
{
    /*
     * TIM_OC_InitTypeDef —— 输出比较配置结构体
     *
     * 用于配置 PWM 的输出比较参数：模式、脉宽、极性等。
     */
    TIM_OC_InitTypeDef sConfigOC = {0};

    /*
     * 开启 TIM2 时钟
     * 等价于 RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
     */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /*
     * 填充 TIM2 句柄的 Init 成员
     *
     * Instance：指向 TIM2 外设的基地址
     *   HAL 通过这个指针访问 TIM2 的寄存器。
     *
     * Prescaler：预分频值 = 71
     *   TIM2 计数频率 = 72MHz / (71 + 1) = 1MHz
     *
     * Period：自动重装载值 = 999
     *   PWM 频率 = 1MHz / 1000 = 1kHz
     *
     * CounterMode：向上计数
     *   TIM_COUNTERMODE_UP → CNT 从 0 递增到 ARR
     *
     * ClockDivision：时钟分频
     *   TIM_CLOCKDIVISION_DIV1 → 不分频
     *   这是对定时器输入时钟的额外分频，通常用 DIV1。
     *
     * AutoReloadPreload：自动重装载预装载
     *   TIM_AUTORELOAD_PRELOAD_DISABLE → 禁止
     *   新 ARR 值立即生效（而不是等到更新事件）。
     */
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72U - 1U;
    htim2.Init.Period = 1000U - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    /*
     * HAL_TIM_PWM_Init —— 初始化 TIM2 为 PWM 模式
     *
     * 内部做了什么：
     *   1. 检查句柄参数
     *   2. 如果定义了 htim2.MspInit 回调，调用它
     *      （HAL 的 MspInit 机制用于配置 GPIO/RCC/NVIC，但本课没用）
     *   3. 根据 Init 成员写入 TIMx->CR1、TIMx->PSC、TIMx->ARR
     *   4. 产生 UG 更新事件加载新值（对应 EGR |= UG）
     */
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置通道 1 的输出比较参数
     *
     * OCMode = TIM_OCMODE_PWM1 → PWM 模式 1
     *   CNT < CCR1 时输出有效电平，CNT >= CCR1 时输出无效电平。
     *   对应寄存器版 OC1M = 110。
     *
     * Pulse = 500 → CCR1 的值 = 500
     *   占空比 = 500 / 1000 = 50%。
     *
     * OCPolarity = TIM_OCPOLARITY_HIGH → 有效电平为高
     *   即 CNT < CCR1 时输出高电平。
     *   对应寄存器版 CC1P = 0。
     *
     * OCFastMode = TIM_OCFAST_DISABLE
     *   快速模式用于特定场景（比如一个 CNT 周期内多次更新 CCR），本课不需要。
     */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 500U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    /*
     * HAL_TIM_PWM_ConfigChannel —— 配置通道参数
     *
     * 内部做了什么：
     *   1. 根据 OCMode 设置 CCMR1.OC1M
     *   2. 根据 Pulse 设置 CCR1
     *   3. 根据 OCPolarity 设置 CCER.CC1P
     *   4. 设置 CCER.CC1E = 1（输出使能）
     */
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    /*
     * 注意：与寄存器版不同，HAL 版没有在这里启动计数器。
     * 启动在 main 中通过 HAL_TIM_PWM_Start 完成。
     *
     * 这种"初始化与启动分离"的设计，允许你在初始化后、
     * 启动前做更多配置（如修改 CCR），更灵活。
     */
}

/*
 * tim3_input_capture_init —— HAL 版 TIM3 输入捕获初始化
 *
 * 对应寄存器版 tim3_input_capture_init 中的：
 *   RCC->APB1ENR |= TIM3EN;
 *   TIM3->PSC = 71;
 *   TIM3->ARR = 0xFFFF;
 *   TIM3->CCMR1 |= CC1S_0;
 *   TIM3->CCER |= CC1E;
 *   TIM3->CCER &= ~(CC1P | CC1NP);  // 上升沿
 *   TIM3->DIER |= CC1IE;  ← 由 Start_IT 完成
 *   TIM3->SR &= ~CC1IF;   ← 由 Start_IT 完成
 *   TIM3->CR1 |= CEN;     ← 由 Start_IT 完成
 *   NVIC 配置
 *
 * 与寄存器版的关键区别：
 *   寄存器版在函数最后 CEN = 1 启动了计数器。
 *   HAL 版把"启动"（CEN + DIER + 清标志）推迟到 HAL_TIM_IC_Start_IT。
 */
static void tim3_input_capture_init(void)
{
    /*
     * TIM_IC_InitTypeDef —— 输入捕获配置结构体
     *
     * 包含捕获的极性、映射、预分频、滤波参数。
     * 对应寄存器版中 CCMR1 和 CCER 的配置。
     */
    TIM_IC_InitTypeDef sConfigIC = {0};

    /*
     * 开启 TIM3 时钟
     * 等价于 RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
     */
    __HAL_RCC_TIM3_CLK_ENABLE();

    /*
     * 填充 TIM3 句柄的 Init 成员
     *
     * Prescaler = 71 → 计数频率 = 1MHz
     * Period = 0xFFFF → 计数器范围 0~65535
     * CounterMode = 向上计数
     * ClockDivision = 不分频
     * AutoReloadPreload = 禁止预装载
     *
     * 这些参数的值与寄存器版完全一致。
     */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 72U - 1U;
    htim3.Init.Period = 0xFFFFU;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    /*
     * HAL_TIM_IC_Init —— 初始化 TIM3 为输入捕获模式
     *
     * 内部做了什么（与 HAL_TIM_PWM_Init 类似）：
     *   1. 检查句柄参数
     *   2. 调用 MspInit 回调（如果有）
     *   3. 配置时基（PSC + ARR + CR1）
     *   4. 产生 UG 更新事件加载新值
     */
    if (HAL_TIM_IC_Init(&htim3) != HAL_OK) {
        error_handler();
    }

    /*
     * 填充通道配置结构体
     *
     * ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING → 上升沿捕获
     *   对应寄存器版：CCER.CC1P = 0, CCER.CC1NP = 0
     *
     * ICSelection = TIM_ICSELECTION_DIRECTTI → 映射到 TI1
     *   对应寄存器版：CCMR1.CC1S = 01
     *   其他可选值：
     *     TIM_ICSELECTION_INDIRECTTI → 映射到 TI2（交叉）
     *     TIM_ICSELECTION_TRC → 映射到内部 TRC
     *
     * ICPrescaler = TIM_ICPSC_DIV1 → 不分频
     *   对应寄存器版：CCMR1.IC1PSC = 00
     *
     * ICFilter = 0 → 不滤波
     *   对应寄存器版：CCMR1.IC1F = 0000
     *   范围 0~15，值越大滤波采样越多。
     */
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0U;

    /*
     * HAL_TIM_IC_ConfigChannel —— 配置通道参数
     *
     * 内部做了什么：
     *   1. 根据 ICSelection 设置 CCMR1.CC1S
     *   2. 根据 ICPrescaler 设置 CCMR1.IC1PSC
     *   3. 根据 ICFilter 设置 CCMR1.IC1F
     *   4. 根据 ICPolarity 设置 CCER.CC1P 和 CCER.CC1NP
     *   5. 设置 CCER.CC1E = 1（使能捕获）
     *
     * 与寄存器版的对应：
     *   这一函数相当于：
     *     TIM3->CCMR1 |= TIM_CCMR1_CC1S_0;
     *     TIM3->CCER  &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);
     *     TIM3->CCER  |= TIM_CCER_CC1E;
     */
    if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置 NVIC（和寄存器版完全相同）
     *
     * HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0)：
     *   抢占优先级 = 1，子优先级 = 0
     *   STM32F103 默认 4 位抢占优先级，没有子优先级，
     *   但 HAL 的接口要求两个参数。
     *
     * HAL_NVIC_EnableIRQ(TIM3_IRQn)：
     *   使能 TIM3 中断通道。
     */
    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);

    /*
     * 没有在这里启动计数器！
     * 启动在 main 中通过 HAL_TIM_IC_Start_IT 完成。
     *
     * 寄存器版中 CEN=1、DIER.CC1IE=1、清 CC1IF 这三步，
     * 在 HAL 版中被封装到 HAL_TIM_IC_Start_IT() 中。
     */
}

/*
 * TIM3_IRQHandler —— TIM3 中断入口
 *
 * 与寄存器版的关键区别：
 *   寄存器版在这里直接判断 SR、读 CCR1、计算周期。
 *   HAL 版只做一件事：调用 HAL_TIM_IRQHandler。
 *
 * HAL_TIM_IRQHandler 内部会：
 *   1. 检测 SR 中的各种标志
 *   2. 对于置位的标志且已使能中断，调用对应的回调函数
 *   3. 清除标志
 *
 * 你不能直接在 TIM3_IRQHandler 中写处理逻辑，
 * 因为 HAL 会在清除标志后才返回——如果你先读了 CCR1，
 * HAL 可能已经把标志清掉了。
 */
void TIM3_IRQHandler(void)
{
    /*
     * HAL_TIM_IRQHandler —— HAL 的定时器中断分发函数
     *
     * 这个函数会检测 TIM3 的所有中断源：
     *   更新事件（UIF）→ HAL_TIM_PeriodElapsedCallback
     *   通道 1 捕获（CC1IF）→ HAL_TIM_IC_CaptureCallback ← 本课使用
     *   通道 2 捕获（CC2IF）→ HAL_TIM_IC_CaptureCallback
     *   等等
     *
     * 它会传入句柄指针，回调函数通过这个指针知道是哪个定时器。
     */
    HAL_TIM_IRQHandler(&htim3);
}

/*
 * HAL_TIM_IC_CaptureCallback —— 输入捕获回调函数
 *
 * 这是 HAL 的"弱定义"回调函数，你可以在用户代码中重新定义它。
 * 每次输入捕获事件发生后，HAL_TIM_IRQHandler 在合适的时机调用它。
 *
 * 参数 htim：触发捕获的定时器句柄指针
 *   通过 htim->Instance 判断是哪个定时器
 *   通过 htim->Channel 判断是哪个通道（HAL 内部设置）
 *
 * 对应寄存器版 TIM3_IRQHandler 中的：
 *   current_capture = CCR1;
 *   g_period_ticks = current - last;
 *   last = current;
 *   g_capture_ready = 1;
 *   清除 CC1IF（由 HAL 自动完成）
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    /*
     * 判断是 TIM3 的通道 1 捕获事件
     *
     * htim->Instance == TIM3：
     *   如果项目中有多个定时器都使用了输入捕获，这个判断能区分。
     *
     * htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1：
     *   HAL_TIM_IRQHandler 在调用回调前会设置 htim->Channel，
     *   指示当前是哪个通道触发了捕获。
     *
     *   注意：这不是 TIM3->SR 中的标志，而是 HAL 在句柄中临时设置的。
     *   不要把它和寄存器版的 CC1IF 搞混。
     */
    if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {

        /*
         * HAL_TIM_ReadCapturedValue —— 读取捕获值
         *
         * 原型：uint32_t HAL_TIM_ReadCapturedValue(&htim, TIM_CHANNEL_1)
         * 内部：return htim->Instance->CCR1;
         *
         * 等价于寄存器版的：TIM3->CCR1
         *
         * 返回 uint32_t 而非 uint16_t——HAL 保留了类型扩展空间。
         * 但实际值不会超过 0xFFFF，所以 g_period_ticks 计算仍然有效。
         */
        uint32_t current_capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

        /*
         * 计算周期：本次捕获值 - 上次捕获值
         *
         * 与寄存器版的逻辑完全相同：
         *   uint16_t 无符号减法自动处理回绕
         *
         * 这里强转为 uint16_t 与 g_period_ticks 类型匹配。
         */
        g_period_ticks = (uint16_t)(current_capture - g_last_capture);

        /*
         * 保存本次捕获值，供下次计算
         */
        g_last_capture = current_capture;

        /*
         * 通知主循环：有新数据了
         */
        g_capture_ready = 1U;

        /*
         * 不需要手动清除 CC1IF！
         * HAL_TIM_IRQHandler 在调用回调之前已经清除了标志。
         * 如果你在这里手动清除，反而可能因为竞争条件导致问题。
         */
    }

    /*
     * 如果 htim 不是 TIM3，说明有其他定时器触发了回调。
     * 这种情况不做处理，直接返回。
     */
}

/*
 * error_handler —— 错误处理函数
 *
 * 当任何 HAL 函数返回 HAL_OK 以外的值时调用。
 * 常见原因：参数错误、时钟未配置、外设忙等。
 *
 * 处理方式：
 *   1. 关闭全局中断（防止中断干扰调试）
 *   2. 进入死循环（等待调试器介入）
 *
 * 在调试器中，你可以挂上来查看调用栈，
 * 找出哪个 HAL 函数返回了错误。
 */
static void error_handler(void)
{
    /*
     * __disable_irq() —— 关闭全局中断
     *
     * 等价于在 CPU 的 PRIMASK 寄存器中设置禁止位。
     * 所有可屏蔽中断都被屏蔽，只剩下 NMI 和 HardFault。
     */
    __disable_irq();

    /*
     * 死循环，等待调试器介入
     */
    while (1) {
    }
}