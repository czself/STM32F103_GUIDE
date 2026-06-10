#include "stm32f1xx.h"

/*
 * 寄存器版：BKP 备份寄存器。
 *
 * 前面看门狗课程已经用 RCC->CSR 判断复位来源。
 * 本课的新重点是备份域：BKP->DR1 这类寄存器在某些复位后仍能保存少量状态。
 *
 * 访问 BKP 前必须做两件事：
 * - 打开 PWR/BKP 时钟
 * - 设置 PWR->CR.DBP，允许写备份域
 */

#define MAGIC 0xA55AU

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

static void backup_domain_enable_write(void)
{
    /*
     * BKP 挂在 APB1，总线时钟不开时无法访问。
     * PWR 负责备份域写保护控制，所以 PWR 时钟也必须打开。
     */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    RCC->APB1ENR |= RCC_APB1ENR_BKPEN;

    /*
     * DBP = Disable Backup domain write Protection。
     * 不置位 DBP 时，写 BKP->DR1 可能不会真正生效。
     */
    PWR->CR |= PWR_CR_DBP;
}

static void backup_marker_init(void)
{
    if (BKP->DR1 != MAGIC) {
        BKP->DR1 = MAGIC;
    }
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    backup_domain_enable_write();
    backup_marker_init();

    while (1) {
        pc13_toggle();

        if (BKP->DR1 == MAGIC) {
            delay_cycles(720000U);
        } else {
            delay_cycles(3600000U);
        }
    }
}
