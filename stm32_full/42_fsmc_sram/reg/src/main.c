#include "stm32f1xx.h"

/*
 * 寄存器版：FSMC SRAM 教学模拟。
 *
 * 当前 BluePill 工程没有真实 FSMC SRAM，也没有配置 FSMC 控制器。
 * 本课用 volatile 数组模拟“可写、可读、可校验”的外部存储区。
 */

static volatile uint16_t g_fake_sram[256];
static volatile uint32_t g_sram_errors;

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

static void fake_sram_write_pattern(void)
{
    for (uint16_t index = 0U; index < 256U; index++) {
        g_fake_sram[index] = (uint16_t)(0xA500U | index);
    }
}

static void fake_sram_verify_pattern(void)
{
    g_sram_errors = 0U;

    for (uint16_t index = 0U; index < 256U; index++) {
        uint16_t expected = (uint16_t)(0xA500U | index);

        if (g_fake_sram[index] != expected) {
            g_sram_errors++;
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    fake_sram_write_pattern();
    fake_sram_verify_pattern();

    while (1) {
        if (g_sram_errors == 0U) {
            pc13_toggle();
        }

        delay_cycles(3600000U);
    }
}
