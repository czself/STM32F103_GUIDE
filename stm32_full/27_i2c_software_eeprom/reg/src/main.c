#include "stm32f1xx.h"

/*
 * 寄存器版：软件 I2C 写 EEPROM。
 *
 * 26 已经讲过硬件 I2C1 的 START/ADDR/TXE/BTF。
 * 本课的新重点是：不用 I2C 外设，只用 GPIO 手工制造 SCL/SDA 时序。
 *
 * 当前代码只演示写序列：
 * START -> 0xA0 -> 0x00 -> 0x5A -> STOP
 *
 * 注意边界：这里没有读取 ACK，也没有读回校验。
 * PC13 翻转只能说明程序持续输出波形，不能单独证明 EEPROM 一定写成功。
 */

#define SCL_PIN_MASK GPIO_BSRR_BS6
#define SDA_PIN_MASK GPIO_BSRR_BS7

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

static void soft_i2c_delay(void)
{
    delay_cycles(120U);
}

static void scl_release(void)
{
    GPIOB->BSRR = SCL_PIN_MASK;
}

static void scl_low(void)
{
    GPIOB->BRR = GPIO_BRR_BR6;
}

static void sda_release(void)
{
    GPIOB->BSRR = SDA_PIN_MASK;
}

static void sda_low(void)
{
    GPIOB->BRR = GPIO_BRR_BR7;
}

static void soft_i2c_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /*
     * PB6/PB7 配成开漏输出。
     * 软件 I2C 的“输出高”不是强推高，而是释放总线，由上拉电阻拉高。
     */
    GPIOB->CRL &= ~(GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF6 |
                    GPIO_CRL_MODE7 |
                    GPIO_CRL_CNF7);

    GPIOB->CRL |= GPIO_CRL_MODE6;
    GPIOB->CRL |= GPIO_CRL_CNF6;
    GPIOB->CRL |= GPIO_CRL_MODE7;
    GPIOB->CRL |= GPIO_CRL_CNF7;

    sda_release();
    scl_release();
}

static void i2c_start(void)
{
    /*
     * START：SCL 为高时，SDA 从高变低。
     */
    sda_release();
    scl_release();
    soft_i2c_delay();

    sda_low();
    soft_i2c_delay();

    scl_low();
}

static void i2c_stop(void)
{
    /*
     * STOP：SCL 为高时，SDA 从低变高。
     */
    sda_low();
    scl_release();
    soft_i2c_delay();

    sda_release();
    soft_i2c_delay();
}

static void i2c_write_bit(uint8_t bit_is_one)
{
    /*
     * I2C 写 bit 的顺序：
     * 1. SCL 低时准备 SDA
     * 2. 拉高 SCL，让从机采样这一位
     * 3. 再拉低 SCL，准备下一位
     */
    if (bit_is_one != 0U) {
        sda_release();
    } else {
        sda_low();
    }

    soft_i2c_delay();
    scl_release();
    soft_i2c_delay();
    scl_low();
}

static void i2c_write_byte(uint8_t byte)
{
    for (uint8_t bit = 0U; bit < 8U; bit++) {
        i2c_write_bit((byte & 0x80U) != 0U);
        byte <<= 1;
    }

    /*
     * 第 9 个时钟本应读取 ACK。
     * 本课为了保持最小写波形，只释放 SDA 并给出 ACK 时钟，不判断从机是否拉低。
     */
    sda_release();
    soft_i2c_delay();
    scl_release();
    soft_i2c_delay();
    scl_low();
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    soft_i2c_gpio_init();

    while (1) {
        i2c_start();
        i2c_write_byte(0xA0U);
        i2c_write_byte(0x00U);
        i2c_write_byte(0x5AU);
        i2c_stop();

        pc13_toggle();
        delay_cycles(7200000U);
    }
}
