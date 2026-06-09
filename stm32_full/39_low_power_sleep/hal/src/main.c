#include "stm32f1xx_hal.h"

/*
 * ============================================================================
 * HAL 版低功耗基础实验：Sleep + EXTI 唤醒
 * ============================================================================
 *
 * ██████  HAL API 与寄存器版对照 ██████
 *
 * | HAL API                                       | 底层寄存器操作             |
 * |----------------------------------------------|----------------------------|
 * | HAL_PWR_EnterSLEEPMode(REGON, WFI)           | SCB->SCR清SLEEPDEEP + WFI |
 * | HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0)      | 写 NVIC->IPRx 优先级      |
 * | HAL_NVIC_EnableIRQ(EXTI0_IRQn)               | 写 NVIC->ISER 使能        |
 * | HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)         | 检查清除 EXTI->PR.PR0     |
 * | HAL_GPIO_EXTI_Callback()                     | 用户唤醒后业务逻辑         |
 *
 * HAL_PWR_EnterSLEEPMode() 有两个参数：
 *   - PWR_MAINREGULATOR_ON：主调节器保持开启（响应快，功耗较高）
 *   - PWR_SLEEPENTRY_WFI：使用 WFI 进入 Sleep（还有 WFE 选项）
 *
 * 和寄存器版的区别：HAL 封装了 SLEEPDEEP 和 WFI 的设置，
 * 底层执行的指令完全相同。
 *
 * ██████  唤醒链路 ██████
 *
 * 执行 WFI → CPU 内核时钟停止
 * → PA0 按下 → 下降沿 → EXTI0 产生中断
 * → CPU 恢复时钟 → 执行 EXTI0_IRQHandler
 * → 清 EXTI0 标志 → 调用 HAL_GPIO_EXTI_Callback()
 * → 回到 WFI 之后的下一条指令
 */

static volatile uint8_t g_wakeup = 0;

static void gpio_init(void);
static void led_blink(uint32_t times);

int main(void)
{
    HAL_Init();
    gpio_init();
    led_blink(2U);

    while (1) {
        g_wakeup = 0U;
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

        if (g_wakeup != 0U) {
            led_blink(1U);
        }
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

static void led_blink(uint32_t times)
{
    for (uint32_t i = 0; i < times; i++) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(120U);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(120U);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0) {
        g_wakeup = 1U;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
