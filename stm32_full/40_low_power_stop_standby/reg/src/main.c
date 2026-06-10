#include "stm32f1xx.h"

/*
 * 寄存器版：Stop 模式与 PA0 唤醒。
 *
 * 39 已经讲过 Sleep：CPU 停一下，外设时钟基本还在。
 * 本课的新重点是 Stop：
 * - 进入前设置 SLEEPDEEP
 * - PWR->CR.PDDS=0 表示进 Stop，不是 Standby
 * - PA0/EXTI0 作为唤醒源
 * - 唤醒后必须重新配置 72MHz 系统时钟
 */

static volatile uint8_t g_woken;

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

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void pc13_led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void pc13_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- != 0U) {
        __NOP();
    }
}

static void pa0_exti_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    /*
     * PA0 内部上拉，按下接 GND，所以下降沿作为唤醒事件。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;

    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0;
    EXTI->IMR |= EXTI_IMR_MR0;
    EXTI->FTSR |= EXTI_FTSR_TR0;
    EXTI->PR = EXTI_PR_PR0;

    NVIC_SetPriority(EXTI0_IRQn, 2U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0U) {
        EXTI->PR = EXTI_PR_PR0;
        g_woken = 1U;
    }
}

static void enter_stop_mode(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;

    /*
     * PDDS=0 选择 Stop；如果置 1，就会进入 Standby，唤醒行为完全不同。
     * CWUF 写 1 清除旧唤醒标志，避免带着历史状态进入低功耗。
     */
    PWR->CR &= ~PWR_CR_PDDS;
    PWR->CR |= PWR_CR_CWUF;

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    pa0_exti_init();

    while (1) {
        g_woken = 0U;
        enter_stop_mode();

        /*
         * Stop 唤醒后 PLL 时钟不再保持为原来的 72MHz 配置。
         * 所以回到主循环后先恢复系统时钟，再做 LED 反馈。
         */
        system_clock_72mhz_init();

        if (g_woken != 0U) {
            pc13_toggle();
            delay_cycles(3600000U);
        }
    }
}
