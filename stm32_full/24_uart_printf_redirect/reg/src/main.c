#include "stm32f1xx.h"

#include <stdio.h>

/*
 * 寄存器版：printf 重定向到 USART1。
 *
 * 前面已经学过 USART1 轮询发送，本课的新重点不是再写一个发送字符串函数，
 * 而是把 C 标准库的 printf 字符流接到 USART1：
 *
 * printf() -> 标准库逐字符调用 fputc() -> 等 TXE -> 写 USART1->DR -> PA9 输出
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

static void uart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_USART1EN |
                    RCC_APB2ENR_AFIOEN;

    /*
     * PA9 是 USART1_TX，必须配置成复用推挽输出。
     * PA10 是 USART1_RX，本课主要观察 printf 发送，但保留 RX 输入配置。
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 |
                    GPIO_CRH_CNF9 |
                    GPIO_CRH_MODE10 |
                    GPIO_CRH_CNF10);

    GPIOA->CRH |= GPIO_CRH_MODE9_1;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    /*
     * 本课使用 72MHz PCLK2 和 115200 波特率。
     * 这里保留简化写法，重点放在 printf -> fputc -> USART 的链路。
     */
    USART1->BRR = 72000000U / 115200U;

    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

int fputc(int ch, FILE *stream)
{
    (void)stream;

    /*
     * printf 格式化后会多次调用 fputc，每次交给我们一个字符。
     * TXE=1 表示 USART1->DR 可以接收下一个待发送字节。
     */
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }

    USART1->DR = (uint8_t)ch;

    return ch;
}

int main(void)
{
    uint32_t count = 0U;

    system_clock_72mhz_init();
    pc13_led_init();
    uart1_init();

    while (1) {
        /*
         * 这里看起来只是普通 printf。
         * 真正输出到串口，是因为上面的 fputc 已经把字符出口接到了 USART1。
         */
        printf("printf redirect count=%lu\r\n", (unsigned long)count);
        count++;

        pc13_toggle();
        delay_cycles(7200000U);
    }
}
