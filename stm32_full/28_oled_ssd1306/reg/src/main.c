#include "stm32f1xx.h"

/* SSD1306 OLED 寄存器版：I2C1 发送初始化命令和一页测试图案。 */
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

#define OLED_ADDR 0x78U
static void i2c1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    GPIOB->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOB->CRL |= GPIO_CRL_MODE6 | GPIO_CRL_CNF6 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7;
    I2C1->CR1 = I2C_CR1_SWRST; I2C1->CR1 = 0;
    I2C1->CR2 = 36; I2C1->CCR = 180; I2C1->TRISE = 37; I2C1->CR1 = I2C_CR1_PE;
}
static void i2c1_start_addr(uint8_t addr)
{
    I2C1->CR1 |= I2C_CR1_START; while ((I2C1->SR1 & I2C_SR1_SB) == 0U) {}
    I2C1->DR = addr; while ((I2C1->SR1 & I2C_SR1_ADDR) == 0U) {}
    (void)I2C1->SR1; (void)I2C1->SR2;
}
static void i2c1_write(uint8_t b) { while ((I2C1->SR1 & I2C_SR1_TXE) == 0U) {} I2C1->DR = b; }
static void oled_write(uint8_t control, uint8_t value)
{
    i2c1_start_addr(OLED_ADDR); i2c1_write(control); i2c1_write(value);
    while ((I2C1->SR1 & I2C_SR1_BTF) == 0U) {} I2C1->CR1 |= I2C_CR1_STOP;
}
static void oled_cmd(uint8_t c) { oled_write(0x00U, c); }
static void oled_data(uint8_t d) { oled_write(0x40U, d); }
static void oled_init(void)
{
    static const uint8_t init[] = {0xAE,0x20,0x02,0xB0,0xC8,0x00,0x10,0x40,0x81,0x7F,0xA1,0xA6,0xA8,0x3F,0xD3,0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,0xDB,0x40,0x8D,0x14,0xAF};
    for (uint32_t i = 0; i < sizeof(init); ++i) oled_cmd(init[i]);
}
int main(void)
{
    system_clock_72mhz_init(); pc13_led_init(); i2c1_init(); delay_cycles(720000U); oled_init();
    oled_cmd(0xB0); oled_cmd(0x00); oled_cmd(0x10);
    for (uint8_t i = 0; i < 128; ++i) oled_data((i & 1U) ? 0xAAU : 0x55U);
    while (1) { pc13_toggle(); delay_cycles(3600000U); }
}
