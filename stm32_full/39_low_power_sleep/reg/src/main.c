#include "stm32f1xx.h"

/*
 * ============================================================================
 * 寄存器版低功耗基础实验：Sleep 模式 + EXTI 唤醒
 * ============================================================================
 *
 * ██████  本课核心知识点 ██████
 *
 * 1. STM32 的低功耗模式层级
 *    - Sleep（最浅）：CPU 停止执行，外设时钟保持，唤醒最快
 *    - Stop（中等）：1.8V 域所有时钟停止，保留 SRAM 和寄存器内容
 *    - Standby（最深）：SRAM 不保留，需要从头启动（只有复位/WKUP 引脚唤醒）
 *    本课只讲 Sleep，因为最简单安全，不会丢失时钟配置
 *
 * 2. WFI（Wait For Interrupt）
 *    - 一条 Cortex-M3 汇编指令
 *    - 执行后 CPU 进入低功耗等待状态，直到有已使能中断到来
 *    - 中断到来时 CPU 自动唤醒，执行中断服务函数，然后继续运行
 *
 * 3. SLEEPDEEP 位（SCB->SCR 寄存器）
 *    - SLEEPDEEP = 0：WFI 进入 Sleep 模式
 *    - SLEEPDEEP = 1：WFI 进入 Stop/Standby（需要额外配置电压调节器等）
 *    本课确保 SLEEPDEEP = 0，只用最安全的 Sleep
 *
 * 4. 唤醒条件
 *    - 必须有一个在 NVIC 中已使能的中断
 *    - 中断信号到达 CPU 时自动唤醒 WFI 状态
 *    - 本课使用 PA0 → EXTI0 下降沿中断
 *
 * 5. SysTick 对 Sleep 的影响
 *    - SysTick 中断每 1ms 触发一次
 *    - 如果 SysTick 没有禁用，WFI 会被 SysTick 频繁唤醒
 *    - 这会导致 Sleep 无效（CPU 持续被 SysTick 唤醒）
 *    - 在裸机 HAL 中，SysTick 每 1ms 唤醒一次（对低功耗有影响）
 *
 * ██████  Demo 演示现象 ██████
 *
 * - 上电后 LED 快闪 2 次（表示初始化完成）
 * - CPU 进入 Sleep（电流降低，LED 灭）
 * - 每次按下 PA0：CPU 唤醒 → 中断翻转 LED → 主循环闪烁确认 → 再次 Sleep
 */

static volatile uint8_t g_wakeup = 0;

static void delay_busy(volatile uint32_t n)
{
    while (n-- > 0U) {
        __NOP();
    }
}

static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void led_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void led_blink(uint32_t times)
{
    for (uint32_t i = 0; i < times; i++) {
        led_toggle();
        delay_busy(700000U);
        led_toggle();
        delay_busy(700000U);
    }
}

static void exti0_wakeup_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;

    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;
    EXTI->IMR |= EXTI_IMR_MR0;
    EXTI->FTSR |= EXTI_FTSR_TR0;
    EXTI->PR = EXTI_PR_PR0;

    NVIC_SetPriority(EXTI0_IRQn, 1U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

static void enter_sleep_mode(void)
{
    /*
     * ██ SCB->SCR（System Control Register）██
     *
     * SCR 是 Cortex-M3 内核的系统控制寄存器，不在 NVIC 里。
     * SLEEPDEEP 位决定 WFI 后进入哪种低功耗模式：
     *   - 0：Sleep（最浅，CPU 停，外设不停）
     *   - 1：Stop 或 Standby（需要配合 PWR 寄存器配置）
     *
     * 这里明确清零 SLEEPDEEP，确保进入的是 Sleep 而不是更深度的模式。
     * 如果不清理，某些 HAL/库可能已经把它设为 1。
     *
     * WFI（Wait For Interrupt）：
     *   CPU 从下一条指令开始暂停，内核时钟门控关闭（停止执行），
     *   直到一个已使能的中断到来。中断信号会打开时钟门控，
     *   CPU 执行中断服务函数，然后回到 WFI 之后的下一条指令。
     */
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
}

void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        EXTI->PR = EXTI_PR_PR0;
        g_wakeup = 1U;
        led_toggle();
    }
}

int main(void)
{
    led_init();
    exti0_wakeup_init();
    led_blink(2U);

    while (1) {
        g_wakeup = 0U;
        enter_sleep_mode();

        if (g_wakeup != 0U) {
            led_blink(1U);
        }
    }
}
