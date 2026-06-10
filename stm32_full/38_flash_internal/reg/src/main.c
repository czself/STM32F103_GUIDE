#include "stm32f1xx.h"

/*
 * 寄存器版：内部 Flash 最后一页写入半字。
 *
 * 本课的新重点是 Flash 不是 RAM：
 * - 写之前必须解锁
 * - 写之前必须按页擦除
 * - 擦除和编程都要等待 BSY 清零
 * - 用完后重新上锁，避免误写
 */

#define FLASH_LAST_PAGE 0x0800FC00UL
#define FLASH_TEST_VALUE 0x1234U

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

static uint16_t flash_read_test_value(void)
{
    return *(__IO uint16_t *)FLASH_LAST_PAGE;
}

static void flash_wait_not_busy(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
    }
}

static void flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

static void flash_erase_last_page(void)
{
    flash_wait_not_busy();

    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = FLASH_LAST_PAGE;
    FLASH->CR |= FLASH_CR_STRT;

    flash_wait_not_busy();

    FLASH->CR &= ~FLASH_CR_PER;
}

static void flash_program_halfword(uint32_t address, uint16_t value)
{
    flash_wait_not_busy();

    FLASH->CR |= FLASH_CR_PG;
    *(__IO uint16_t *)address = value;

    flash_wait_not_busy();

    FLASH->CR &= ~FLASH_CR_PG;
}

static void flash_write_demo(void)
{
    flash_unlock();
    flash_erase_last_page();
    flash_program_halfword(FLASH_LAST_PAGE, FLASH_TEST_VALUE);

    /*
     * 写完重新上锁。真实工程里还要考虑掉电保护、磨损均衡和页内数据备份。
     */
    FLASH->CR |= FLASH_CR_LOCK;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();

    if (flash_read_test_value() != FLASH_TEST_VALUE) {
        flash_write_demo();
    }

    while (1) {
        pc13_toggle();

        if (flash_read_test_value() == FLASH_TEST_VALUE) {
            delay_cycles(720000U);
        } else {
            delay_cycles(3600000U);
        }
    }
}
