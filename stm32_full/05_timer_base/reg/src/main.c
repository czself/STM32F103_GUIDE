#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 定时器基础”。
 *
 * 目标：
 * 1. 配置系统时钟到 72MHz
 * 2. 使用 TIM2 产生 1 秒一次的更新中断
 * 3. 在 TIM2 中断中翻转 PC13 LED
 *
 * 本课重点：
 * - 理解 PSC / ARR / CNT
 * - 理解更新事件和更新中断
 * - 理解定时器时钟为什么在当前配置下是 72MHz
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

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void tim2_base_init(void)
{
    /*
     * 打开 TIM2 外设时钟。
     *
     * TIM2 挂在 APB1 上，所以它的时钟使能位在 APB1ENR 中。
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 当前时钟树配置下：
     * - PCLK1 = 36MHz
     * - 但由于 APB1 分频不是 1，而是 /2
     * - 所以 TIM2 的实际输入时钟 = 2 x PCLK1 = 72MHz
     *
     * 我们希望最终得到 1Hz 更新事件。
     *
     * 先设 PSC = 7200 - 1：
     * 72MHz / 7200 = 10kHz
     *
     * 再设 ARR = 10000 - 1：
     * 10kHz / 10000 = 1Hz
     *
     * 也就是每 1 秒产生一次更新事件。
     */
    TIM2->PSC = 7200U - 1U;
    TIM2->ARR = 10000U - 1U;

    /*
     * 清更新标志。
     *
     * 这样可以避免带着旧标志位直接进入中断。
     */
    TIM2->SR &= ~TIM_SR_UIF;

    /*
     * 允许更新中断。
     *
     * UIE = Update Interrupt Enable
     */
    TIM2->DIER |= TIM_DIER_UIE;

    /*
     * 在 NVIC 中使能 TIM2 中断。
     *
     * 外设自己允许中断还不够，
     * 还必须让 NVIC 接收这个中断请求。
     */
    NVIC_SetPriority(TIM2_IRQn, 1U);
    NVIC_EnableIRQ(TIM2_IRQn);

    /*
     * 启动计数器。
     *
     * CEN = Counter Enable
     */
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    /*
     * 先确认是不是更新事件导致的中断。
     */
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        /*
         * 清除更新中断标志。
         *
         * 如果不清掉，下次会持续认为中断还没处理完。
         */
        TIM2->SR &= ~TIM_SR_UIF;

        /*
         * 这里用 ODR 判断当前 LED 状态，再翻转它。
         *
         * 对 BluePill 常见板子：
         * - PC13 低电平亮
         * - PC13 高电平灭
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
    tim2_base_init();

    while (1) {
        /*
         * 本课主循环里什么都不做。
         * LED 翻转完全依赖硬件定时器中断。
         */
    }
}

