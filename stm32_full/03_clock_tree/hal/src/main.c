#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 时钟树配置”。
 *
 * 目标：
 * 1. 使用 HAL 标准接口把系统时钟配置成 72MHz
 * 2. 再闪烁 PC13 LED，作为系统正常运行的现象
 *
 * 本课重点是理解：
 * - HAL 怎样描述振荡源配置
 * - HAL 怎样描述系统时钟和总线分频
 * - HAL 的时钟配置本质上仍然是在完成底层 RCC 和 Flash 配置
 */

static void led_pc13_init(void);
static void system_clock_72mhz_init(void);
static void error_handler(void);

int main(void)
{
    /*
     * 先初始化 HAL 基础环境。
     * 后续 HAL 的时钟配置和延时能力都建立在这个基础上。
     */
    HAL_Init();

    /*
     * 把系统时钟配置为常见的 72MHz。
     */
    system_clock_72mhz_init();

    /*
     * 初始化 LED 输出。
     */
    led_pc13_init();

    while (1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(500);

        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(500);
    }
}

static void system_clock_72mhz_init(void)
{
    /*
     * RCC_OscInitTypeDef 用来描述：
     * - 使用哪个振荡源
     * - HSE 开不开
     * - PLL 开不开
     * - PLL 输入来自哪里
     * - PLL 倍频是多少
     */
    RCC_OscInitTypeDef osc = {0};

    /*
     * RCC_ClkInitTypeDef 用来描述：
     * - 系统时钟源选哪个
     * - AHB 分频是多少
     * - APB1 分频是多少
     * - APB2 分频是多少
     */
    RCC_ClkInitTypeDef clk = {0};

    /*
     * 第 1 部分：配置振荡源和 PLL。
     *
     * 目标：
     * - 打开 HSE
     * - 让 PLL 使用 HSE 作为输入
     * - 把 PLL 倍频设为 x9
     *
     * 这样就能从 8MHz 得到 72MHz。
     */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        error_handler();
    }

    /*
     * 第 2 部分：配置系统时钟和总线分频。
     *
     * 目标：
     * - 系统时钟选择 PLL 输出
     * - AHB  不分频 -> 72MHz
     * - APB1 分频 2 -> 36MHz
     * - APB2 不分频 -> 72MHz
     *
     * 同时传入 FLASH_LATENCY_2，告诉 HAL：
     * 72MHz 运行时，Flash 需要 2 个等待周期。
     */
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

static void led_pc13_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 打开 GPIOC 时钟，为配置 PC13 做准备。
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 把 PC13 配成普通推挽输出。
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 先置高，让板载 LED 默认熄灭。
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

static void error_handler(void)
{
    /*
     * 如果时钟配置失败，就停在这里。
     * 这样至少不会继续带着错误配置往下运行。
     */
    __disable_irq();
    while (1) {
    }
}
