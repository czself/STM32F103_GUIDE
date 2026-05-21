#include "stm32f1xx_hal.h"

/*
 * 本文件是"HAL版 SPI1 主模式回环实验"。
 *
 * 本课目标：
 * 1. 用 HAL 初始化 SPI1 为主模式、Mode 0
 * 2. 使用 HAL_SPI_TransmitReceive() 做单字节同步收发
 * 3. 通过 PA7 -> PA6 跳线验证回环结果
 *
 * 与寄存器版的对应关系：
 *   HAL_SPI_Init + SPI_HandleTypeDef    → SPI1->CR1 各配置位
 *   HAL_SPI_TransmitReceive()           → spi1_transfer_byte()
 *
 * 理解寄存器版有助于理解 HAL 函数内部在做什么。
 * 理解 HAL 版有助于在实际项目中快速开发。
 */

#define SPI_TEST_PERIOD_MS 1000U

static SPI_HandleTypeDef hspi1;

static void system_clock_72mhz_init(void);
static void led_pc13_init(void);
static void led_on(void);
static void led_toggle(void);
static void spi1_gpio_init(void);
static void spi1_init(void);
static void error_handler(void);

int main(void)
{
    uint8_t tx_byte = 0xA5U;
    uint8_t rx_byte = 0U;

    HAL_Init();

    system_clock_72mhz_init();
    led_pc13_init();
    spi1_gpio_init();
    spi1_init();

    while (1) {
        /*
         * HAL_SPI_TransmitReceive() —— SPI 同步收发
         *
         * 参数说明：
         *   第 1 个参数：SPI 句柄指针
         *   第 2 个参数：发送数据缓冲区
         *   第 3 个参数：接收数据缓冲区
         *   第 4 个参数：数据长度（字节数）
         *   第 5 个参数：超时时间
         *
         * 这是最符合 SPI 本质的 API：
         *   发 1 字节 → 自动收 1 字节
         *   发 N 字节 → 自动收 N 字节
         *
         * 对应寄存器版 spi1_transfer_byte() 中的：
         *   while(!TXE); DR = data;
         *   while(!RXNE); rx = DR;
         *   while(BSY);
         */
        if (HAL_SPI_TransmitReceive(&hspi1, &tx_byte, &rx_byte, 1U, HAL_MAX_DELAY) != HAL_OK) {
            error_handler();
        }

        if (rx_byte == tx_byte) {
            led_toggle();
        } else {
            led_on();
        }

        if (tx_byte == 0xA5U) {
            tx_byte = 0x3CU;
        } else {
            tx_byte = 0xA5U;
        }

        HAL_Delay(SPI_TEST_PERIOD_MS);
    }
}

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

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) error_handler();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) error_handler();
}

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

static void led_on(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

static void led_toggle(void)
{
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

/*
 * spi1_gpio_init —— HAL 版配置 SPI1 引脚
 *
 * PA5(SCK) + PA7(MOSI)：复用推挽输出 → GPIO_MODE_AF_PP
 * PA6(MISO)：输入 → GPIO_MODE_INPUT
 */
static void spi1_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA5 = SCK, PA7 = MOSI */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA6 = MISO */
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/*
 * spi1_init —— HAL 版 SPI1 初始化
 *
 * 各配置项与寄存器版的对应关系：
 *
 * Mode = SPI_MODE_MASTER          → CR1.MSTR = 1
 * Direction = SPI_DIRECTION_2LINES → 全双工（默认）
 * DataSize = SPI_DATASIZE_8BIT     → CR1.DFF = 0（8 位）
 * CLKPolarity = SPI_POLARITY_LOW   → CR1.CPOL = 0（Mode 0）
 * CLKPhase = SPI_PHASE_1EDGE       → CR1.CPHA = 0（Mode 0）
 * NSS = SPI_NSS_SOFT               → CR1.SSM = 1
 * BaudRatePrescaler = /8           → CR1.BR = 010
 * FirstBit = SPI_FIRSTBIT_MSB      → CR1.LSBFIRST = 0
 */
static void spi1_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance = SPI1;

    /*
     * Mode：主模式
     *   对应寄存器版：CR1.MSTR = 1
     */
    hspi1.Init.Mode = SPI_MODE_MASTER;

    /*
     * Direction：全双工（2 线）
     *   SPI_DIRECTION_2LINES → 同时使用 MOSI 和 MISO
     *   对应寄存器版：CR1.BIDIMODE=0, CR1.RXONLY=0
     */
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;

    /*
     * DataSize：8 位数据
     *   对应寄存器版：CR1.DFF = 0
     */
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;

    /*
     * CLKPolarity：时钟极性 = 低
     *   对应寄存器版：CR1.CPOL = 0（空闲时 SCK 为低）
     */
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;

    /*
     * CLKPhase：时钟相位 = 第一个边沿
     *   对应寄存器版：CR1.CPHA = 0（第一个边沿采样）
     */
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;

    /*
     * NSS：软件管理
     *   对应寄存器版：CR1.SSM = 1, CR1.SSI = 1
     */
    hspi1.Init.NSS = SPI_NSS_SOFT;

    /*
     * BaudRatePrescaler：波特率预分频 /8
     *   对应寄存器版：CR1.BR = 010（72MHz / 8 = 9MHz）
     */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;

    /*
     * FirstBit：高位先发送（MSB first）
     *   对应寄存器版：CR1.LSBFIRST = 0
     */
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;

    /* 以下配置本课用默认值 */
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;

    /*
     * HAL_SPI_Init() 内部做了什么？
     *   1. 如果定义了 HAL_SPI_MspInit 回调，调用它
     *   2. 根据上述配置写 CR1（MSTR、BR、CPOL、CPHA、SSM、LSBFIRST 等）
     *   3. 写 CR2（使能 RXNE 中断等，本课不需要中断）
     *   4. 写 CR1 的 SPE = 1（使能 SPI）
     *
     * 对应寄存器版 spi1_init() 中的所有操作。
     */
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        error_handler();
    }
}

static void error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}