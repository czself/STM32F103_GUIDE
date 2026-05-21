#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL版 ADC 轮询采样"。
 *
 * 目标：
 * 1. 使用 PA1 作为 ADC1_IN1 输入
 * 2. 用 HAL 的轮询流程读取 ADC 值（Start → PollForConversion → GetValue）
 * 3. 根据结果控制 PC13 LED 亮灭
 *
 * 与寄存器版的差异：
 *   寄存器版：直接操作 ADC1->CR2、ADC1->SR、ADC1->DR 等寄存器
 *   HAL 版：通过 ADC_HandleTypeDef 句柄，调用 HAL 函数间接操作
 *
 * 理解寄存器版有助于理解 HAL 函数内部在做什么。
 * 理解 HAL 版有助于在实际项目中快速开发。
 */

/*
 * ADC 句柄变量
 *
 * ADC_HandleTypeDef 是 HAL 中 ADC 外设的抽象句柄，
 * 包含了 Instance（外设基地址）、Init（初始化参数）、
 * Lock（锁状态）、State（状态）、DMA_Handle（关联的 DMA 句柄）等。
 */
static ADC_HandleTypeDef hadc1;

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void pa1_adc_input_init(void);
static void adc1_init(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * HAL 版 ADC 轮询采样的三步曲：
 *   1. HAL_ADC_Start()     → 启动转换
 *   2. HAL_ADC_PollForConversion() → 等待完成（带超时）
 *   3. HAL_ADC_GetValue()  → 读取结果
 *
 * 与寄存器版的对应关系：
 *   Start()            → ADC1->CR2 |= EXTTRIG | SWSTART
 *   PollForConversion() → while (!(SR & EOC))
 *   GetValue()         → return ADC1->DR
 */
int main(void)
{
    uint32_t adc_value;

    /*
     * HAL_Init() 是使用 HAL 库的第一步。
     * 内部做两件事：
     *   1. 配置 Flash 预取缓冲
     *   2. 配置 SysTick 为 1ms 中断（供 HAL_Delay 使用）
     */
    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    adc1_init();

    while (1) {
        /*
         * 第 1 步：启动一次 ADC 转换
         *
         * HAL_ADC_Start() 内部做了什么：
         *   1. 清除 ADC 状态标志（如 EOC）
         *   2. 设置 CR2 的 ADON 位启动转换
         *   3. 如果是软件触发，设置 SWSTART
         *
         * 参数：句柄指针 &hadc1
         * 返回：HAL_OK 或 HAL_ERROR
         */
        if (HAL_ADC_Start(&hadc1) != HAL_OK) {
            error_handler();
        }

        /*
         * 第 2 步：轮询等待转换完成
         *
         * HAL_ADC_PollForConversion() 内部做了什么：
         *   1. 循环检查 SR 寄存器的 EOC 位
         *   2. 如果超时（第二个参数 100ms）还没等到，返回 HAL_TIMEOUT
         *   3. 如果 EOC 置位，返回 HAL_OK
         *
         * 第二个参数：超时时间（单位 ms）
         *   这里设为 100ms。ADC 转换本身只需要几十微秒，
         *   设 100ms 只是作为一个"安全边界"。
         *   如果 100ms 后 EOC 还没置位，说明硬件可能出了问题。
         *
         * 对应寄存器版：
         *   while ((ADC1->SR & ADC_SR_EOC) == 0U) {}
         */
        if (HAL_ADC_PollForConversion(&hadc1, 100U) != HAL_OK) {
            error_handler();
        }

        /*
         * 第 3 步：读取转换结果
         *
         * HAL_ADC_GetValue() 内部做了什么：
         *   直接读取 hadc1->Instance->DR（即 ADC1->DR）
         *
         * 对应寄存器版：
         *   return (uint16_t)ADC1->DR;
         *
         * 返回 uint32_t，但有效值只有低 16 位（12 位数据 + 4 位 0）。
         */
        adc_value = HAL_ADC_GetValue(&hadc1);

        /*
         * 用阈值判断，控制 LED
         */
        if (adc_value > 2048U) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED 亮 */
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED 灭 */
        }

        /*
         * HAL_Delay 是 HAL 库提供的毫秒级延时函数。
         * 它依赖 SysTick 中断（HAL_Init 时已配置为 1ms 一次）。
         *
         * 相比寄存器版的软件延时（delay），HAL_Delay 更精确。
         */
        HAL_Delay(20);
    }
}

/*
 * system_clock_72mhz_init —— HAL 版时钟配置
 *
 * 通过 RCC 结构体 + HAL 函数完成，结果与寄存器版完全相同：
 *   SYSCLK = 72MHz, HCLK = 72MHz, PCLK1 = 36MHz, PCLK2 = 72MHz
 */
static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 配置振荡器（HSE → PLL）
     *
     * OscillatorType：指定要配置哪些振荡器
     * HSEState：打开 HSE
     * HSEPredivValue：HSE 预分频（STM32F103 有独立的预分频器）
     * PLL.PLLState：打开 PLL
     * PLL.PLLSource：PLL 时钟源 = HSE
     * PLL.PLLMUL：PLL 倍频 = x9 → 72MHz
     */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;   /* HSE 不分频 */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;      /* PLL 输入 = HSE */
    osc.PLL.PLLMUL = RCC_PLL_MUL9;              /* x9 → 72MHz */

    /*
     * HAL_RCC_OscConfig 对应寄存器版：
     *   RCC->CR  |= RCC_CR_HSEON;
     *   while (!HSERDY);
     *   RCC->CFGR |= PLLSRC | PLLMULL9;
     *   RCC->CR  |= PLLON;
     *   while (!PLLRDY);
     */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置系统时钟、AHB、APB1、APB2 分频
     *
     * SYSCLKSource：系统时钟源 = PLL
     * AHBCLKDivider：AHB 不分频
     * APB1CLKDivider：APB1 2分频
     * APB2CLKDivider：APB2 不分频
     * FLASH_LATENCY_2：72MHz 需要 2 个 Flash 等待周期
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
     * HAL_RCC_ClockConfig 对应寄存器版：
     *   RCC->CFGR |= HPRE_DIV1 | PPRE1_DIV2 | PPRE2_DIV1 | SW_PLL;
     *   while (SWS != PLL);
     *   另外还会更新 SystemCoreClock 全局变量。
     */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        error_handler();
    }
}

/*
 * led_pc13_init —— HAL 版初始化 PC13 LED
 *
 * GPIO_InitTypeDef 是 HAL 的通用 GPIO 配置结构体，
 * HAL_GPIO_Init 内部会根据引脚号自动选择 CRL 或 CRH 寄存器。
 */
static void led_pc13_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 开启 GPIOC 时钟
     * 宏展开等价于 RCC->APB2ENR |= RCC_APB2ENR_IOPCEN
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 配置 PC13：通用推挽输出
     *
     * Pin：GPIO_PIN_13 或 GPIO_PIN_ALL
     * Mode：GPIO_MODE_OUTPUT_PP → 推挽输出
     * Pull：GPIO_NOPULL → 无上下拉
     * Speed：GPIO_SPEED_FREQ_LOW → 低速
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 初始高电平 → LED 灭
     * GPIO_PIN_SET = 高电平
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

/*
 * pa1_adc_input_init —— HAL 版配置 PA1 为模拟输入
 *
 * GPIO_MODE_ANALOG 对应寄存器版的 MODE=00, CNF=00。
 * HAL 中 "模拟输入" 是一个独立的 Mode 枚举值。
 *
 * 注意：作为 ADC 输入时，不需要配置复用功能（AF）。
 * 与定时器 PWM 输出不同（需要 AF_PP），
 * ADC 输入只关注意模拟电平，复用功能配置对它没有意义。
 */
static void pa1_adc_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;   /* 模拟输入模式 */
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * adc1_init —— HAL 版 ADC1 初始化
 *
 * 初始化过程分为两步：
 *   1. 填充 hadc1.Init 成员 → 调用 HAL_ADC_Init()
 *   2. 填充 sConfig 结构体 → 调用 HAL_ADC_ConfigChannel()
 *
 * 两步分离的设计：
 *   Init 处理"ADC 整体的时基和模式"
 *   ConfigChannel 处理"当前要采哪个通道、采样时间"
 */
static void adc1_init(void)
{
    /*
     * ADC_ChannelConfTypeDef —— 通道配置结构体
     *
     * 包含通道号、规则组中的排位、采样时间。
     */
    ADC_ChannelConfTypeDef sConfig = {0};

    /*
     * 第 1 步：开启 ADC1 时钟
     */
    __HAL_RCC_ADC1_CLK_ENABLE();

    /*
     * 第 2 步：ADC 时钟分频
     *
     * __HAL_RCC_ADC_CONFIG 是一个宏，用于设置 RCC->CFGR 的 ADCPRE 位。
     * RCC_ADCPCLK2_DIV6：PCLK2 / 6 = 12MHz
     *
     * 对应寄存器版：
     *   RCC->CFGR &= ~RCC_CFGR_ADCPRE;
     *   RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
     */
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    /*
     * 第 3 步：填充句柄的 Init 成员
     *
     * Instance：指定外设 = ADC1
     *
     * ScanConvMode（扫描模式）：
     *   ADC_SCAN_DISABLE → 不扫描，只用一个通道
     *   如果多个通道需要轮流采集，才需要打开扫描模式。
     *
     * ContinuousConvMode（连续转换模式）：
     *   DISABLE → 单次转换，每次要手动启动
     *   本课是轮询方式，用单次转换即可。
     *
     * DiscontinuousConvMode（间断模式）：
     *   DISABLE → 关闭，本课不需要
     *
     * ExternalTrigConv（触发源）：
     *   ADC_SOFTWARE_START → 通过软件启动
     *
     * DataAlign（数据对齐）：
     *   ADC_DATAALIGN_RIGHT → 右对齐（12 位结果在 bit 0~11）
     *
     * NbrOfConversion（规则组通道数）：
     *   1 → 规则组中只包含 1 个通道
     */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    /*
     * HAL_ADC_Init() 内部做了什么？
     *   1. 开启 ADC 上电（ADON = 1）
     *   2. 如果定义了 MspInit 回调，调用它（可用于配置 NVIC、DMA 等）
     *   3. 执行 ADC 校准（CAL）
     *
     * 注意：HAL_ADC_Init 会帮我们做校准，这是寄存器版中手动做的。
     */
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        error_handler();
    }

    /*
     * 第 4 步：配置规则组通道
     *
     * Channel：ADC_CHANNEL_1 → 通道 1（PA1）
     * Rank：ADC_REGULAR_RANK_1 → 规则组第 1 位
     * SamplingTime：ADC_SAMPLETIME_239CYCLES_5 → 最长采样时间
     *
     * 对应寄存器版：
     *   ADC1->SQR3 |= 1;
     *   ADC1->SMPR2 |= ADC_SMPR2_SMP1;
     */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    /*
     * HAL_ADC_ConfigChannel() 内部做了什么？
     *   1. 写 SQR 寄存器（设置通道在规则组中的位置）
     *   2. 写 SMPR 寄存器（设置采样时间）
     */
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        error_handler();
    }
}

/*
 * error_handler —— 错误处理函数
 *
 * 任何 HAL 函数返回非 HAL_OK 时调用。
 * 关闭中断后死循环，等待调试器介入。
 */
static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}