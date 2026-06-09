#include "stm32f1xx.h"

/* MPU6050 寄存器版：用 I2C1 读取 WHO_AM_I。 */
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

#define MPU_ADDR_W 0xD0U
#define MPU_ADDR_R 0xD1U
static void i2c1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN; RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    GPIOB->CRL &= ~(GPIO_CRL_MODE6|GPIO_CRL_CNF6|GPIO_CRL_MODE7|GPIO_CRL_CNF7);
    GPIOB->CRL |= GPIO_CRL_MODE6|GPIO_CRL_CNF6|GPIO_CRL_MODE7|GPIO_CRL_CNF7;
    I2C1->CR1 = I2C_CR1_SWRST; I2C1->CR1 = 0; I2C1->CR2 = 36; I2C1->CCR = 180; I2C1->TRISE = 37; I2C1->CR1 = I2C_CR1_PE;
}
static void start_addr(uint8_t a){I2C1->CR1|=I2C_CR1_START;while(!(I2C1->SR1&I2C_SR1_SB)){}I2C1->DR=a;while(!(I2C1->SR1&I2C_SR1_ADDR)){}(void)I2C1->SR1;(void)I2C1->SR2;}
static void wr(uint8_t b){while(!(I2C1->SR1&I2C_SR1_TXE)){}I2C1->DR=b;}
static void mpu_write(uint8_t reg, uint8_t val){start_addr(MPU_ADDR_W);wr(reg);wr(val);while(!(I2C1->SR1&I2C_SR1_BTF)){}I2C1->CR1|=I2C_CR1_STOP;}
static uint8_t mpu_read(uint8_t reg){uint8_t v;start_addr(MPU_ADDR_W);wr(reg);while(!(I2C1->SR1&I2C_SR1_BTF)){}I2C1->CR1&=~I2C_CR1_ACK;start_addr(MPU_ADDR_R);I2C1->CR1|=I2C_CR1_STOP;while(!(I2C1->SR1&I2C_SR1_RXNE)){}v=(uint8_t)I2C1->DR;I2C1->CR1|=I2C_CR1_ACK;return v;}
int main(void){system_clock_72mhz_init();pc13_led_init();i2c1_init();delay_cycles(720000U);mpu_write(0x6B,0x00);while(1){uint8_t id=mpu_read(0x75);pc13_toggle();delay_cycles((id==0x68U)?720000U:3600000U);}}
