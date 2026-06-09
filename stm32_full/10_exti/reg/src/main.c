#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 EXTI 外部中断”。
 *
 * 目标：
 * - 使用 PA0 按键触发 EXTI0 中断
 * - 每次按下按键，就翻转一次 PC13 板载 LED 状态
 *
 * 硬件假设：
 * - PA0 接按键一端
 * - 按键另一端接 GND
 * - PA0 使用内部上拉
 *
 * 因此按下按键时，PA0 会从高电平变成低电平，
 * 本课选择“下降沿触发中断”。
 */

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /*
     * 板载 LED 默认先熄灭。
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void key_pa0_input_pullup_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA0 配成输入上拉/下拉模式：
     * - MODE0 = 00
     * - CNF0  = 10
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;

    /*
     * 在 F103 中，输入上拉/下拉模式下：
     * - ODR0 = 1 -> 上拉
     * - ODR0 = 0 -> 下拉
     *
     * 这里通过 BSRR 间接把 ODR0 置 1，得到内部上拉。
     */
    GPIOA->BSRR = GPIO_BSRR_BS0;
}

static void exti0_init(void)
{
    /*
     * EXTI 映射属于 AFIO 体系，所以先打开 AFIO 时钟。
     */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    /*
     * 把 EXTI0 映射到 PA0。
     *
     * 对于 EXTI0 来说：
     * - 它可以来自 PA0 / PB0 / PC0 ...
     * 这里要明确指定为来自 A 端口。
     *
     * AFIO_EXTICR1 的 EXTI0 这 4 bit 写 0000 就表示 PA0。
     */
    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;

    /*
     * 允许 EXTI0 这条线产生中断请求。
     */
    EXTI->IMR |= EXTI_IMR_MR0;

    /*
     * 本课选择下降沿触发：
     * 按键按下时，PA0 从高电平变成低电平。
     */
    EXTI->FTSR |= EXTI_FTSR_TR0;

    /*
     * 不使用上升沿触发，避免松开按键时也触发。
     */
    EXTI->RTSR &= ~EXTI_RTSR_TR0;

    /*
     * 先清掉可能残留的挂起标志。
     */
    EXTI->PR = EXTI_PR_PR0;

    /*
     * 在 NVIC 中使能 EXTI0 中断。
     */
    NVIC_SetPriority(EXTI0_IRQn, 1U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI0_IRQHandler(void)
{
    /*
     * 先确认是不是 EXTI0 挂起了。
     */
    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        /*
         * 写 1 清除挂起标志。
         *
         * 注意：
         * 这里不是写 0 清除，而是“写 1 清除”。
         * 这是很多外设标志位常见的清除方式。
         */
        EXTI->PR = EXTI_PR_PR0;

        /*
         * 翻转板载 LED 状态。
         */
        if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    key_pa0_input_pullup_init();
    exti0_init();

    while (1) {
        /*
         * 主循环里不再轮询按键。
         * 逻辑完全交给外部中断来驱动。
         */
    }
}

