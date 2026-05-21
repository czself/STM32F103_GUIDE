#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 时钟树配置”。
 *
 * 目标：
 * 1. 将 STM32F103 的系统时钟配置为常见的 72MHz
 * 2. 再使用 PC13 闪烁 LED，作为程序稳定运行的直观现象
 *
 * 本课重点不是点灯，而是：
 * - HSE 是什么
 * - PLL 是什么
 * - 为什么要等待 ready 标志位
 * - 为什么 APB1 必须分频
 * - 为什么 72MHz 前要先配置 Flash 等待周期
 */

static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}

static void led_pc13_init(void)
{
    /*
     * 打开 GPIOC 时钟，给 PC13 作为 LED 输出做准备。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * PC13 属于 8~15 号引脚，所以要在 CRH 中配置。
     *
     * 目标模式：
     * - MODE13 = 10 -> 输出模式，最大速度 2MHz
     * - CNF13  = 00 -> 通用推挽输出
     */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /*
     * BluePill 常见板载 LED 为低电平点亮。
     * 先输出高电平，保持默认熄灭。
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void system_clock_72mhz_init(void)
{
    /*
     * 第 1 步：配置 Flash 等待周期。
     *
     * 当系统频率升高后，CPU 访问 Flash 的速度要求也更高。
     * 如果 72MHz 时仍沿用较低频率下的 Flash 配置，系统可能不稳定。
     *
     * 对 F103 的常见 72MHz 配置来说：
     * - 需要 2 个等待周期
     *
     * 同时打开预取缓冲，提高顺序读取效率。
     */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /*
     * 第 2 步：打开 HSE。
     *
     * HSE 是外部高速时钟，本课默认来自板上的 8MHz 晶振。
     * HSEON 位置 1 后，开始启动外部晶振。
     */
    RCC->CR |= RCC_CR_HSEON;

    /*
     * 第 3 步：等待 HSE 稳定。
     *
     * HSERDY = 1 表示外部时钟已经稳定，可以继续后续配置。
     * 如果不等它稳定，就继续拿它给 PLL 用，时钟链路就不完整。
     */
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /*
     * 第 4 步：配置总线分频和 PLL 来源。
     *
     * 目标配置：
     * - AHB  = SYSCLK / 1  -> HCLK  = 72MHz
     * - APB1 = HCLK / 2    -> PCLK1 = 36MHz
     * - APB2 = HCLK / 1    -> PCLK2 = 72MHz
     * - PLL 来源 = HSE
     * - PLL 倍频 = x9
     *
     * 之所以 APB1 要 /2，是因为 F103 的 APB1 最大只能 36MHz。
     */
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

    /*
     * 第 5 步：打开 PLL。
     *
     * 现在 PLL 的输入已经被配置为 HSE，倍频也配置成 x9，
     * 所以打开后目标输出就是 72MHz。
     */
    RCC->CR |= RCC_CR_PLLON;

    /*
     * 第 6 步：等待 PLL 稳定锁定。
     *
     * PLLRDY = 1 才表示 PLL 输出可以作为稳定系统时钟使用。
     */
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    /*
     * 第 7 步：把系统时钟源切换到 PLL。
     *
     * SW 位用于“选择想切到哪个系统时钟源”。
     * 这里把它设置为 PLL。
     */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    /*
     * 第 8 步：等待真正切换完成。
     *
     * SWS 位表示“当前实际正在使用的系统时钟源”。
     * 只有当它显示 PLL 时，才说明系统真的已经切过去了。
     */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }

    /*
     * 到这里，时钟树常见配置已经完成：
     * - SYSCLK = 72MHz
     * - HCLK   = 72MHz
     * - PCLK1  = 36MHz
     * - PCLK2  = 72MHz
     */
}

int main(void)
{
    system_clock_72mhz_init();
    led_pc13_init();

    while (1) {
        /*
         * LED 亮：PC13 输出低电平。
         */
        GPIOC->BRR = GPIO_BRR_BR13;
        delay(1200000U);

        /*
         * LED 灭：PC13 输出高电平。
         */
        GPIOC->BSRR = GPIO_BSRR_BS13;
        delay(1200000U);
    }
}

