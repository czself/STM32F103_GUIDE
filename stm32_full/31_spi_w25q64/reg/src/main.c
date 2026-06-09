#include "stm32f1xx.h"

/* W25Q64 寄存器版：发送 0x9F 读取 JEDEC ID。 */
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

static void spi1_init(void){RCC->APB2ENR|=RCC_APB2ENR_IOPAEN|RCC_APB2ENR_SPI1EN;GPIOA->CRL&=~(GPIO_CRL_MODE4|GPIO_CRL_CNF4|GPIO_CRL_MODE5|GPIO_CRL_CNF5|GPIO_CRL_MODE6|GPIO_CRL_CNF6|GPIO_CRL_MODE7|GPIO_CRL_CNF7);GPIOA->CRL|=GPIO_CRL_MODE4_1|GPIO_CRL_MODE5_1|GPIO_CRL_CNF5_1|GPIO_CRL_CNF6_0|GPIO_CRL_MODE7_1|GPIO_CRL_CNF7_1;GPIOA->BSRR=GPIO_BSRR_BS4;SPI1->CR1=SPI_CR1_MSTR|SPI_CR1_SSM|SPI_CR1_SSI|SPI_CR1_BR_1|SPI_CR1_SPE;}
static uint8_t spi1_xfer(uint8_t b){while(!(SPI1->SR&SPI_SR_TXE)){}*(__IO uint8_t *)&SPI1->DR=b;while(!(SPI1->SR&SPI_SR_RXNE)){}return (uint8_t)SPI1->DR;}
static uint8_t w25q64_read_mid(void){uint8_t id;GPIOA->BRR=GPIO_BRR_BR4;spi1_xfer(0x9F);id=spi1_xfer(0xFF);spi1_xfer(0xFF);spi1_xfer(0xFF);while(SPI1->SR&SPI_SR_BSY){}GPIOA->BSRR=GPIO_BSRR_BS4;return id;}
int main(void){system_clock_72mhz_init();pc13_led_init();spi1_init();while(1){uint8_t mid=w25q64_read_mid();pc13_toggle();delay_cycles((mid==0xEFU)?720000U:3600000U);}}
