#include "stm32f1xx.h"

/*
 * 寄存器版：调试工具链与断点观察。
 *
 * 前面很多课程用 LED 证明程序在跑，本课多看一层：
 * - 断点会让 CPU 停在某一行，LED 也会跟着停
 * - Watch 可以观察普通 RAM 变量和 GPIOC->ODR 这类外设寄存器
 * - DWT->CYCCNT 可以记录 CPU 周期数，用来感受两段代码之间跑了多久
 */

static volatile uint32_t g_breakpoint_counter;
static volatile uint32_t g_last_odr;
static volatile uint32_t g_cycle_counter;

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

static void debug_counter_init(void)
{
    /*
     * DWT 属于 Cortex-M3 内核调试单元，不需要 RCC 外设时钟。
     * 但 CYCCNT 计数前必须先打开 CoreDebug 的 trace 总开关 TRCENA。
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /*
     * 清零后再使能，Watch 里看到的 g_cycle_counter 就更容易从 0 开始理解。
     */
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    debug_counter_init();

    while (1) {
        /*
         * 适合在这一行或下一行打断点：
         * CPU 停住时，LED 停在当前状态，Watch 也能看到计数停在哪一轮。
         */
        g_breakpoint_counter++;

        /*
         * 读 GPIOC->ODR 到 RAM 变量。
         * 这样既能观察外设寄存器，也能观察普通 volatile 变量。
         */
        g_last_odr = GPIOC->ODR;
        g_cycle_counter = DWT->CYCCNT;

        pc13_toggle();
        delay_cycles(3600000U);
    }
}
