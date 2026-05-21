#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 SysTick 毫秒节拍”。
 *
 * 目标：
 * 1. 配置系统时钟到 72MHz
 * 2. 使用 SysTick 建立 1ms 节拍
 * 3. 自己维护一个毫秒计数变量
 * 4. 基于这个计数变量实现 delay_ms()
 * 5. 用它来稳定闪烁 PC13 LED
 */

static volatile uint32_t g_ms_ticks = 0;

static void led_pc13_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    GPIOC->BSRR = GPIO_BSRR_BS13;
}

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

static void systick_1ms_init(void)
{
    /*
     * 第 1 步：设置重装值。
     *
     * 当前系统时钟假设为 72MHz。
     * 1ms 对应时钟数：
     * 72,000,000 / 1000 = 72,000
     *
     * SysTick 从 LOAD 递减到 0，因此通常写 72000 - 1。
     */
    SysTick->LOAD = 72000U - 1U;

    /*
     * 第 2 步：清空当前计数值。
     *
     * 这样可以避免带着未知初值开始计数。
     */
    SysTick->VAL = 0U;

    /*
     * 第 3 步：配置并启动 SysTick。
     *
     * SysTick_CTRL_CLKSOURCE_Msk：
     * - 选择处理器时钟 HCLK 作为 SysTick 时钟源
     *
     * SysTick_CTRL_TICKINT_Msk：
     * - 允许计数到 0 时产生中断
     *
     * SysTick_CTRL_ENABLE_Msk：
     * - 启动 SysTick 计数器
     */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void)
{
    /*
     * 每次 SysTick 计数到 0 并触发中断时，都会进入这里。
     *
     * 因为本课把周期设置成 1ms，
     * 所以这里每进来一次，就代表过去了 1ms。
     */
    g_ms_ticks++;
}

static void delay_ms(uint32_t ms)
{
    /*
     * 记录开始时间。
     */
    uint32_t start = g_ms_ticks;

    /*
     * 一直等到“当前毫秒数 - 开始毫秒数 >= 目标延时值”。
     *
     * 这里使用无符号减法，可以自然处理计数器回绕问题。
     */
    while ((g_ms_ticks - start) < ms) {
    }
}

int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();
    systick_1ms_init();

    while (1) {
        GPIOC->BRR = GPIO_BRR_BR13;
        delay_ms(500U);

        GPIOC->BSRR = GPIO_BSRR_BS13;
        delay_ms(500U);
    }
}

