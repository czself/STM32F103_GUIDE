#include "stm32f1xx.h"

/*
 * 寄存器版：核心架构与内存映射观察。
 *
 * 前几课已经学过点灯、GPIO 和 72MHz 时钟配置，所以这些初始化这里只短注。
 * 本课真正要看的，是“地址不是普通数字”，而是 CPU 访问 Flash、SRAM、
 * 外设寄存器和内核外设的入口。
 */

static volatile uint32_t g_sram_counter;
static volatile uint32_t g_debug_words[5];

static void system_clock_72mhz_init(void)
{
    /* 72MHz 运行前先配置 Flash 等待周期；这部分在时钟树课已经展开讲过。 */
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
    /* LED 只是运行现象，不是本课重点；PC13 推挽输出的细节前面已经讲过。 */
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

static void memory_map_sample_init(void)
{
    /*
     * g_debug_words 在 SRAM 中，调试器 watch 它时看到的是“变量内容”。
     * 下面写进去的值，大多是芯片手册定义好的地址常量。
     *
     * 这样做的目的不是让 LED 显示这些数，而是让你在调试器里同时看到：
     * - SRAM 变量本身有地址
     * - 变量里也可以存放另一个地址
     * - 外设寄存器就是映射到固定地址上的硬件窗口
     */
    g_debug_words[0] = SCB->CPUID;

    /*
     * 0x08000000 附近通常是片内 Flash。
     * 程序代码烧录后主要就放在这里，CPU 从这里取指令执行。
     */
    g_debug_words[1] = FLASH_BASE;

    /*
     * 0x20000000 附近是 SRAM。
     * 普通全局变量、栈、堆通常会落在这片地址范围里。
     */
    g_debug_words[2] = SRAM_BASE;

    /*
     * 0x40000000 附近是外设地址空间。
     * 访问这个范围不是在读写普通内存，而是在和 RCC、GPIO、TIM 等硬件寄存器交互。
     */
    g_debug_words[3] = PERIPH_BASE;

    /*
     * GPIOC_BASE 是 GPIOC 外设寄存器组的起始地址。
     * 前几课写 GPIOC->CRH、GPIOC->BSRR，本质就是从这个基地址向后偏移访问寄存器。
     */
    g_debug_words[4] = GPIOC_BASE;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    memory_map_sample_init();

    while (1) {
        /*
         * 这个变量不断自增，方便在调试器里观察 SRAM 中的变量会实时变化。
         * 如果暂停程序，你可以看它的地址是否位于 SRAM_BASE 附近。
         */
        g_sram_counter++;

        pc13_toggle();
        delay_cycles(3600000U);
    }
}
