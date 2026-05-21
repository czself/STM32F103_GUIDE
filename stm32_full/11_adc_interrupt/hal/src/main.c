#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL版 ADC 中断采样"。
 *
 * 目标：
 * 1. 使用 PA1 作为 ADC1_IN1 输入
 * 2. 用 HAL 的中断方式获取 ADC 结果
 * 3. 在转换完成回调中更新 LED
 *
 * 与上一课（HAL 轮询版）的区别：
 *   轮询版：HAL_ADC_Start → HAL_ADC_PollForConversion → HAL_ADC_GetValue
 *   中断版：HAL_ADC_Start_IT → HAL_ADC_IRQHandler（分发）→ 回调中读结果
 *
 * HAL 中断链路：
 *   main 中调用 HAL_ADC_Start_IT()
 *     → 启动 ADC + 使能 EOC 中断 + 设 NVIC
 *   ADC 转换完成后：
 *     → 硬件 -> ADC1_2_IRQHandler() -> HAL_ADC_IRQHandler() 分发
 *     → HAL 检测到 EOC → 调用 HAL_ADC_ConvCpltCallback()
 *     → 用户在回调中读取结果、控制 LED、再次启动
 */

/*
 * ADC 句柄
 */
static ADC_HandleTypeDef hadc1;

/*
 * 全局变量 —— 中断和主循环共享
 * volatile 防止编译器优化掉对它的读写
 */
static volatile uint32_t g_adc_value = 0U;

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void pa1_adc_input_init(void);
static void adc1_init(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * 与 HAL 轮询版的关键区别：
 *   轮询版用 HAL_ADC_Start() → HAL_ADC_PollForConversion()
 *   中断版用 HAL_ADC_Start_IT() —— 启动后立即返回，不等转换完成
 *
 * HAL_ADC_Start_IT() 做了什么？
 *   1. 清除 EOC 等状态标志
 *   2. 写 CR1.EOCIE = 1（使能转换完成中断）
 *   3. 写 CR2 启动转换（SWSTART）
 *   4. 返回 HAL_OK
 */
int main(void)
{
    HAL_Init();
    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    adc1_init();

    /*
     * 启动第一轮 ADC 中断采样。
     *
     * 与 HAL_ADC_Start() 不同：
     *   Start()：只启动转换
     *   Start_IT()：启动转换 + 使能 EOC 中断
     *
     * 对应寄存器版：ADC1->CR1 |= EOCIE; ADC1->CR2 |= EXTTRIG | SWSTART;
     * 但还包含 NVIC 配置（已在 adc1_init 中完成）。
     */
    if (HAL_ADC_Start_IT(&hadc1) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * 主循环里不需要轮询等待。
         * ADC 完成后会自动触发中断，最终进入回调函数。
         *
         * 这里可以放其他任务。
         */
    }
}

/*
 * system_clock_72mhz_init —— HAL 版时钟配置
 */
static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        error_handler();
    }
}

/*
 * led_pc13_init —— HAL 版初始化 PC13 LED
 */
static void led_pc13_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

/*
 * pa1_adc_input_init —— HAL 版配置 PA1 为模拟输入
 */
static void pa1_adc_input_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * adc1_init —— HAL 版 ADC1 初始化（中断模式）
 *
 * 与轮询版相比的差异：
 *   1. 额外配置了 NVIC（使能 ADC1_2_IRQn 中断通道）
 *   2. 中断使能（EOCIE）在 Start_IT 中完成，不在 init 中
 *
 * HAL_ADC_Init() 内部做了什么：
 *   1. 上电（ADON = 1）
 *   2. 如果定义了 HAL_ADC_MspInit 回调，调用它（可在其中配 NVIC）
 *   3. 执行校准
 *
 * 本课没有用 MspInit 回调，而是直接在 adc1_init 最后手动配置 NVIC。
 */
static void adc1_init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /*
     * 第 1 步：开时钟 + 分频
     */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    /*
     * 第 2 步：填充句柄 Init 成员
     * 与轮询版相同：单通道、单次转换、软件触发、右对齐
     */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        error_handler();
    }

    /*
     * 第 3 步：配置通道（通道 1 + 最长采样时间）
     */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        error_handler();
    }

    /*
     * ★ 第 4 步：配置 NVIC
     *
     * 为什么需要这步？
     *   HAL_ADC_Start_IT() 内部会设置 EOCIE（分闸），
     *   但不会自动配置 NVIC（总闸）。所以我们要手动配置。
     *
     * 注意：如果使用 HAL 的 MspInit 机制，NVIC 配置可以放在那里。
     * 本课为了清晰，直接在 init 函数中配置。
     */
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
}

/*
 * ADC1_2_IRQHandler —— ADC 中断入口
 *
 * HAL 的中断处理模式：
 *   用户不能在 ADC1_2_IRQHandler 中直接写业务逻辑。
 *   必须调用 HAL_ADC_IRQHandler(&hadc1)，由它完成：
 *     1. 检测 EOC 标志
 *     2. 清除 EOC 标志
 *     3. 调用 HAL_ADC_ConvCpltCallback() 回调
 *
 * 为什么 HAL 要这样设计？
 *   因为一个中断入口可能对应多个中断源（例如 ADC1 和 ADC2 共用），
 *   HAL 需要统一检测和处理，再通过回调把"用户逻辑"和"底层中断处理"分离。
 */
void ADC1_2_IRQHandler(void)
{
    /*
     * HAL_ADC_IRQHandler 内部会检查 SR.EOC 标志，
     * 如果置位则调用 HAL_ADC_ConvCpltCallback()，
     * 然后清除标志。
     */
    HAL_ADC_IRQHandler(&hadc1);
}

/*
 * HAL_ADC_ConvCpltCallback —— ADC 转换完成回调
 *
 * 这是 HAL 定义的"弱函数"，用户可以在自己的代码中重新实现它。
 * HAL_ADC_IRQHandler 检测到 EOC 后，会在合适的时机调用这个回调。
 *
 * 对应寄存器版 ADC1_2_IRQHandler 中的：
 *   g_adc_value = ADC1->DR;
 *   判断阈值 → 控制 LED
 *   adc1_start_conversion();  // 再启动下一次
 *
 * 参数 hadc：触发回调的 ADC 句柄指针
 *   当多个 ADC（如 ADC1 和 ADC2）都使用中断时，
 *   通过 hadc->Instance 可以区分是哪个 ADC 完成了转换。
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    /*
     * 判断确实是 ADC1 完成转换
     * （如果项目中也用了 ADC2 中断，这个判断就很重要）
     */
    if (hadc->Instance == ADC1) {

        /*
         * 读取转换结果
         * HAL_ADC_GetValue() 等价于：hadc->Instance->DR
         * 即读 ADC1 的数据寄存器。
         */
        g_adc_value = HAL_ADC_GetValue(hadc);

        /*
         * 根据结果控制 LED
         */
        if (g_adc_value > 2048U) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }

        /*
         * ★ 关键：再启动下一次转换
         *
         * 因为本课配置的是"单次转换"模式，
         * 转换一次后就停住了，如果不再次调用 Start_IT，
         * ADC 只会转一次。
         *
         * 相当于寄存器版中的 adc1_start_conversion()。
         *
         * 这样就形成持续采样链：
         *   Start_IT → 转换完成 → 中断 → 回调 → Start_IT → 转换完成 → ...
         */
        if (HAL_ADC_Start_IT(hadc) != HAL_OK) {
            error_handler();
        }
    }
}

/*
 * error_handler —— 错误处理函数
 */
static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}