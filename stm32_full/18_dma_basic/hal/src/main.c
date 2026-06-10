#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL版 ADC + DMA 基础实验"。
 *
 * 本课目标：
 * 1. 用 HAL 初始化 ADC1 和 DMA1_Channel1
 * 2. 使用 HAL_ADC_Start_DMA() 启动"ADC 持续采样 + DMA 自动搬运"
 * 3. 让主循环直接读取内存变量，不再手动读 ADC 数据寄存器
 *
 * 与寄存器版的对应关系：
 *   HAL_ADC_Start_DMA() → ADC1->CR2.CONT + ADC1->CR2.DMA + SWSTART + DMA 配置
 *   __HAL_LINKDMA()     → 将 DMA 句柄与 ADC 句柄关联，Start_DMA 才能知道用哪个 DMA 通道
 *
 * 与上一课（16_adc_interrupt HAL 版）的区别：
 *   中断版：HAL_ADC_Start_IT() → 中断 → 回调中读值
 *   DMA 版：HAL_ADC_Start_DMA() → DMA 自动搬 → 内存变量直接可用
 */

/*
 * ADC 句柄和 DMA 句柄
 *
 * 使用 ADC + DMA 时，需要两个句柄：
 *   ADC_HandleTypeDef：控制 ADC 的初始化、校准、启动
 *   DMA_HandleTypeDef：控制 DMA 通道的配置
 *
 * 两者通过 __HAL_LINKDMA() 关联。
 */
static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;

/*
 * HAL_ADC_Start_DMA() 需要一个内存缓冲区地址。
 * 本课最简单，只用一个变量保存"最新一次采样值"。
 */
static volatile uint32_t g_adc_value = 0U;

/* 函数前置声明 */
static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void pa1_adc_input_init(void);
static void dma1_channel1_init(void);
static void adc1_init(void);
static void error_handler(void);

/*
 * main —— 主函数
 *
 * HAL 版 ADC + DMA 的典型流程：
 *   1. 初始化 DMA（配置通道参数）
 *   2. 初始化 ADC（配置时钟、通道、采样时间）
 *   3. 调用 HAL_ADC_Start_DMA() 启动
 *   4. 主循环直接读内存变量
 *
 * HAL_ADC_Start_DMA() 内部做了什么？
 *   1. 检查参数合法性
 *   2. 将目标缓冲区地址和长度存入句柄
 *   3. 配置 DMA（CPAR = ADC1->DR, CMAR = 传入的地址, CNDTR = 传入的长度）
 *   4. 使能 ADC 的 DMA 请求（CR2.DMA = 1）
 *   5. 如果配置了连续模式，启动连续转换
 *   6. 返回 HAL_OK
 *
 * 对应寄存器版 adc1_dma_start() 中的操作：
 *   DMA1_Channel1->CCR |= DMA_CCR_EN;
 *   ADC1->CR2 |= EXTTRIG | SWSTART;
 */
int main(void)
{
    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    pa1_adc_input_init();
    dma1_channel1_init();
    adc1_init();

    /*
     * 这是本课 HAL 版最核心的一句：
     *
     * HAL_ADC_Start_DMA(ADC 句柄, 目标缓冲地址, 缓冲长度)
     *
     * 参数说明：
     *   第 1 个参数：ADC 句柄指针
     *   第 2 个参数：DMA 写入的内存地址（目标缓冲区）
     *   第 3 个参数：缓冲区大小（传输计数）
     *
     * 本课：目标地址是 &g_adc_value，长度是 1
     * （下一课 20_adc_dma 中会用到数组和更大的长度）
     *
     * 它的效果可以理解为"一键启动 ADC + DMA 全自动流水线"：
     *   1. 启动 DMA（使能通道）
     *   2. 告诉 DMA 把 ADC 数据搬到 g_adc_value
     *   3. 启动 ADC 转换（连续模式）
     */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&g_adc_value, 1U) != HAL_OK) {
        error_handler();
    }

    while (1) {
        /*
         * DMA 在后台不断刷新 g_adc_value。
         * 主循环只需要读取这个变量即可。
         *
         * 这条链路上 CPU 的参与度为零：
         *   CPU 不需要启动转换、不需要等待 EOC、不需要读 DR。
         *   一切由 ADC + DMA 硬件自动完成。
         */
        if (g_adc_value > 2048U) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
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
 * dma1_channel1_init —— HAL 版 DMA1 通道 1 初始化
 *
 * 与寄存器版的对应关系：
 *   hdma_adc1.Instance = DMA1_Channel1;
 *   hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;   → DIR = 0
 *   hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;       → PINC = 0
 *   hdma_adc1.Init.MemInc = DMA_MINC_DISABLE;          → MINC = 0
 *   hdma_adc1.Init.PeriphDataAlignment = HALFWORD;     → PSIZE = 01
 *   hdma_adc1.Init.MemDataAlignment = HALFWORD;        → MSIZE = 01
 *   hdma_adc1.Init.Mode = DMA_CIRCULAR;                → CIRC = 1
 *
 * 关键：__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)
 *   这行把 ADC 句柄的 DMA_Handle 成员指向 hdma_adc1。
 *   这样 HAL_ADC_Start_DMA() 内部就知道要使用哪个 DMA 通道。
 *   如果不做这步关联，Start_DMA 无法配置 DMA。
 */
static void dma1_channel1_init(void)
{
    /*
     * 开启 DMA1 时钟
     * DMA1 挂在 AHB 总线上
     */
    __HAL_RCC_DMA1_CLK_ENABLE();

    /*
     * 指定 DMA 使用的硬件通道。
     * 对 STM32F103 的 ADC1 来说，固定对应 DMA1_Channel1。
     */
    hdma_adc1.Instance = DMA1_Channel1;

    /*
     * Direction = 外设到内存
     *   对应寄存器版：CCR.DIR = 0
     */
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;

    /*
     * PeriphInc = 外设地址不自增
     *   因为源始终是 ADC1->DR，地址不变。
     *   对应寄存器版：CCR.PINC = 0
     */
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;

    /*
     * MemInc = 内存地址不自增
     *   因为本课只保存一个最新采样值，始终写同一个变量。
     *   对应寄存器版：CCR.MINC = 0
     *   （下一课 20_adc_dma 中会设为 ENABLE）
     */
    hdma_adc1.Init.MemInc = DMA_MINC_DISABLE;

    /*
     * PeriphDataAlignment / MemDataAlignment = 半字 16 位
     *   ADC 结果是 12 位，按 16 位处理最合适。
     *   对应寄存器版：CCR.PSIZE = 01, CCR.MSIZE = 01
     */
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;

    /*
     * Mode = 循环模式
     *   传完后不会停止，持续接收后续 ADC 数据。
     *   对应寄存器版：CCR.CIRC = 1
     */
    hdma_adc1.Init.Mode = DMA_CIRCULAR;

    /*
     * Priority = 高优先级
     *   对应寄存器版：CCR.PL = 10
     */
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;

    /*
     * HAL_DMA_Init() 内部做了什么？
     *   根据上述结构体成员的配置，写入 DMA 通道的寄存器：
     *   - CCR（方向、模式、宽度、优先级等）
     *
     * 注意：此时 DMA 尚未使能（CCR.EN = 0），
     * 使能由 HAL_ADC_Start_DMA() 在启动时完成。
     */
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
        error_handler();
    }

    /*
     * ★ 关键：关联 ADC 句柄和 DMA 句柄
     *
     * __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)：
     *   宏展开相当于：
     *     hadc1.DMA_Handle = &hdma_adc1;
     *
     * 这样做的意义：
     *   当调用 HAL_ADC_Start_DMA() 时，HAL 可以通过
     *   hadc1.DMA_Handle 找到 hdma_adc1，从而知道：
     *   - 用哪个 DMA 通道（DMA1_Channel1）
     *   - 它的配置是什么
     *   然后自动配置 CPAR = &ADC1->DR, CMAR = 传入的缓冲区地址
     *
     * 如果不做 __HAL_LINKDMA：
     *   HAL_ADC_Start_DMA() 不知道要用哪个 DMA 通道，返回 HAL_ERROR。
     */
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
}

/*
 * adc1_init —— HAL 版 ADC1 初始化（DMA 模式）
 *
 * 与中断版（16_adc_interrupt）的关键变化：
 *   1. ContinuousConvMode = ENABLE（连续转换）
 *   2. 不需要使能 EOCIE（中断）
 *   3. 不需要配置 ADC 的 NVIC
 *
 * 注意：HAL_ADC_Start_DMA() 会用 DMA 中断回调维护 HAL 内部状态，
 * 但本课主循环只读持续刷新的内存变量，不依赖 DMA 完成回调做业务逻辑。
 * 如果后续要在半传输/传输完成回调里处理数据，需要手动配置 DMA NVIC
 * 并提供对应的 DMA IRQHandler。
 */
static void adc1_init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /*
     * 开时钟 + 分频
     */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    /*
     * 填充 Init 成员
     *
     * ContinuousConvMode = ENABLE：
     *   连续转换模式，启动后 ADC 不断采样。
     *   对应寄存器版：CR2.CONT = 1
     *
     * 其他配置与轮询/中断版相同：
     *   单通道、软件触发、右对齐
     */
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        error_handler();
    }

    /*
     * STM32F1 HAL 不会在 HAL_ADC_Init() 中自动校准 ADC。
     * 这里显式执行 RSTCAL + CAL，和寄存器版的校准步骤对齐。
     */
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) {
        error_handler();
    }

    /*
     * 配置通道 1，最长采样时间
     */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        error_handler();
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

void SysTick_Handler(void)
{
    HAL_IncTick();
}
