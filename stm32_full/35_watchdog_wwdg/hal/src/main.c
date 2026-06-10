#include "stm32f1xx_hal.h"

/*
 * ============================================================================
 * HAL 版 WWDG（窗口看门狗）实验
 * ============================================================================
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                 | 底层寄存器操作                |
 * |-------------------------|-------------------------------|
 * | __HAL_RCC_WWDG_CLK_ENABLE() | RCC->APB1ENR.WWDGEN = 1    |
 * | HAL_WWDG_Init(&hwwdg)   | 写 WWDG->CFR 和 WWDG->CR     |
 * | HAL_WWDG_Refresh(&hwwdg)| WWDG->CR = WDGA|0x7F（重装） |
 *
 * 注意：HAL_WWDG_Init() 在窗口看门狗已启动的情况下会检查 WDGA 状态，
 * 如果 WWDG 已启动，它会在窗口内完成刷新。
 *
 * 和 IWDG 的区别在于：
 * - IWDG 的 Refresh 就是写 KR = 0xAAAA
 * - WWDG 的 Refresh 是写 CR = WDGA|0x7F（在窗口内才合法）
 */

static WWDG_HandleTypeDef hwwdg;  /* WWDG 句柄 */

static void gpio_init(void);
static void wwdg_init(void);
static uint8_t key_pressed(void);
static void led_blink(uint32_t times);
static void error_handler(void);

int main(void)
{
    uint8_t was_wwdg_reset;

    HAL_Init();
    gpio_init();

    /*
     * 检测复位来源（和 IWDG 类似，只是标志位不同）
     * RCC_FLAG_WWDGRST = CSR.WWDGRSTF
     */
    was_wwdg_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) ? 1U : 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    if (was_wwdg_reset) {
        led_blink(4U);  /* WWDG 复位指示 */
    }

    wwdg_init();  /* 配置并启动 WWDG */

    while (1) {
        if (key_pressed()) {
            /*
             * ██ 关键：等待进入合法窗口 ██
             *
             * HAL 版也要手动等待 T ≤ 0x50 后再调用 HAL_WWDG_Refresh()
             * HAL_WWDG_Refresh() 本身不检查是否在窗口内，
             * 所以如果 T > 0x50 时直接调用，WWDG 会检测到非法刷新并复位。
             *
             * HAL 库里虽然有内部检查，但最好的做法是在应用层就做等待，
             * 这样更安全，也更容易理解窗口的含义。
             */
            while ((WWDG->CR & WWDG_CR_T) > 0x50U) {
                /* 等待计数器从 0x7F 递减到 ≤ 0x50 */
            }
            if (HAL_WWDG_Refresh(&hwwdg) != HAL_OK) {  /* 在窗口内合法刷新 */
                error_handler();
            }
            led_blink(1U);
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED 亮 */
        }
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void wwdg_init(void)
{
    __HAL_RCC_WWDG_CLK_ENABLE();  /* 打开 WWDG 时钟 */

    /*
     * HAL_WWDG_Init() 内部完成以下序列：
     * 1. 写 WWDG->CFR（分频 + 窗口值 + EWI 配置）
     * 2. 写 WWDG->CR（WDGA=1 启动 + 初始计数器值）
     *
     * 注意：HAL_WWDG_Init() 只有在 WWDG 未启动时才写 CR
     * 如果 WWDG 已经启动（WDGA=1），Init 会跳过 CR 的写入
     * 所以 Init 必须在 WWDG 启动前调用
     *
     * 这里配置：
     *   Prescaler = WWDG_PRESCALER_8（PCLK1/4096/8）
     *   Window = 0x50（窗口上限）
     *   Counter = 0x7F（初始计数值，最大）
     *   EWIMode = 关闭（不使用提前唤醒中断）
     */
    hwwdg.Instance = WWDG;
    hwwdg.Init.Prescaler = WWDG_PRESCALER_8;  /* 分频 8 */
    hwwdg.Init.Window = 0x50U;                 /* 窗口值 0x50 */
    hwwdg.Init.Counter = 0x7FU;                /* 初始计数器 0x7F */
    hwwdg.Init.EWIMode = WWDG_EWI_DISABLE;    /* 不使用 EWI 中断 */
    if (HAL_WWDG_Init(&hwwdg) != HAL_OK) {     /* 配置并启动 */
        error_handler();
    }
}

static uint8_t key_pressed(void)
{
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
}

static void led_blink(uint32_t times)
{
    for (uint32_t i = 0; i < times; i++) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(80U);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(80U);
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

static void error_handler(void)
{
    /*
     * WWDG 初始化或窗口内刷新失败时，继续跑会掩盖真正问题。
     * 停在这里方便调试器查看 hwwdg 和 WWDG->CR/CFR。
     */
    __disable_irq();
    while (1) {
    }
}
