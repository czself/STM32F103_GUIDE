#include "stm32f1xx.h"

/*
 * 寄存器版：I2C 读写 MPU6050 寄存器。
 *
 * 前面已经学过 I2C 总线流程和 OLED/EEPROM 的写入模型。
 * 本课的新重点是“传感器内部寄存器访问”：
 * - 写 PWR_MGMT_1(0x6B)=0x00，把 MPU6050 从睡眠中唤醒
 * - 读 WHO_AM_I(0x75)，确认器件在线且地址正确
 *
 * 读寄存器不是直接读从机地址，而是先写寄存器地址，再重复起始切到读方向。
 */

#define MPU_ADDR_W 0xD0U
#define MPU_ADDR_R 0xD1U

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

static void i2c1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    GPIOB->CRL &= ~(GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF6 |
                    GPIO_CRL_MODE7 |
                    GPIO_CRL_CNF7);

    GPIOB->CRL |= GPIO_CRL_MODE6 | GPIO_CRL_CNF6;
    GPIOB->CRL |= GPIO_CRL_MODE7 | GPIO_CRL_CNF7;

    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0U;
    I2C1->CR2 = 36U;
    I2C1->CCR = 180U;
    I2C1->TRISE = 37U;
    I2C1->CR1 = I2C_CR1_PE;
}

static void i2c_start_and_address(uint8_t address)
{
    I2C1->CR1 |= I2C_CR1_START;
    while ((I2C1->SR1 & I2C_SR1_SB) == 0U) {
    }

    I2C1->DR = address;
    while ((I2C1->SR1 & I2C_SR1_ADDR) == 0U) {
    }

    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

static void i2c_write_byte(uint8_t byte)
{
    while ((I2C1->SR1 & I2C_SR1_TXE) == 0U) {
    }

    I2C1->DR = byte;
}

static void mpu_write_register(uint8_t reg, uint8_t value)
{
    i2c_start_and_address(MPU_ADDR_W);
    i2c_write_byte(reg);
    i2c_write_byte(value);

    while ((I2C1->SR1 & I2C_SR1_BTF) == 0U) {
    }

    I2C1->CR1 |= I2C_CR1_STOP;
}

static uint8_t mpu_read_register(uint8_t reg)
{
    uint8_t value;

    /*
     * 第 1 段：写寄存器地址。
     * 这里还不 STOP，因为后面要用重复起始继续读同一个器件。
     */
    i2c_start_and_address(MPU_ADDR_W);
    i2c_write_byte(reg);
    while ((I2C1->SR1 & I2C_SR1_BTF) == 0U) {
    }

    /*
     * 单字节读取前关闭 ACK。
     * 这样读完这个字节后，主机不会继续请求下一个字节。
     */
    I2C1->CR1 &= ~I2C_CR1_ACK;

    /*
     * 第 2 段：重复起始，切到读方向。
     */
    i2c_start_and_address(MPU_ADDR_R);

    I2C1->CR1 |= I2C_CR1_STOP;
    while ((I2C1->SR1 & I2C_SR1_RXNE) == 0U) {
    }

    value = (uint8_t)I2C1->DR;

    I2C1->CR1 |= I2C_CR1_ACK;
    return value;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    i2c1_init();
    delay_cycles(720000U);

    mpu_write_register(0x6BU, 0x00U);

    while (1) {
        uint8_t id = mpu_read_register(0x75U);

        pc13_toggle();

        if (id == 0x68U) {
            delay_cycles(720000U);
        } else {
            delay_cycles(3600000U);
        }
    }
}
