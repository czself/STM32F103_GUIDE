#include "stm32f1xx.h"

/* 内部 FLASH 寄存器版：向 64KB Flash 最后一页写入 0x1234。 */
static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
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

#define FLASH_LAST_PAGE 0x0800FC00UL
static void flash_wait(void){while(FLASH->SR & FLASH_SR_BSY){} }
static void flash_unlock(void){if(FLASH->CR & FLASH_CR_LOCK){FLASH->KEYR=0x45670123U;FLASH->KEYR=0xCDEF89ABU;}}
static void flash_write_demo(void){flash_unlock();flash_wait();FLASH->CR|=FLASH_CR_PER;FLASH->AR=FLASH_LAST_PAGE;FLASH->CR|=FLASH_CR_STRT;flash_wait();FLASH->CR&=~FLASH_CR_PER;FLASH->CR|=FLASH_CR_PG;*(__IO uint16_t*)FLASH_LAST_PAGE=0x1234U;flash_wait();FLASH->CR&=~FLASH_CR_PG;FLASH->CR|=FLASH_CR_LOCK;}
int main(void){system_clock_72mhz_init();pc13_led_init();if(*(__IO uint16_t*)FLASH_LAST_PAGE!=0x1234U){flash_write_demo();}while(1){pc13_toggle();delay_cycles((*(__IO uint16_t*)FLASH_LAST_PAGE==0x1234U)?720000U:3600000U);}}
