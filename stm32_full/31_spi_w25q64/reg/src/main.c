#include "stm32f1xx.h"

/*
 * 寄存器版：SPI1 读取 W25Q64 JEDEC ID。
 *
 * 30 已经讲过 SPI 回环和全双工收发。本课的新重点是访问真实 SPI 从机：
 * - PA4 作为 CS，拉低表示一笔事务开始，拉高表示事务结束
 * - 0x9F 是 W25Q64 的 JEDEC ID 命令
 * - 读数据时主机仍要继续发送 dummy byte，因为 SCK 只能由主机产生
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

static void w25q64_select(void)
{
    GPIOA->BRR = GPIO_BRR_BR4;
}

static void w25q64_release(void)
{
    GPIOA->BSRR = GPIO_BSRR_BS4;
}

static void spi1_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA4 = CS，普通推挽输出，由软件控制事务边界。
     * PA5 = SCK，PA7 = MOSI，复用推挽输出。
     * PA6 = MISO，输入，由 W25Q64 驱动。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE4 |
                    GPIO_CRL_CNF4 |
                    GPIO_CRL_MODE5 |
                    GPIO_CRL_CNF5 |
                    GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF6 |
                    GPIO_CRL_MODE7 |
                    GPIO_CRL_CNF7);

    GPIOA->CRL |= GPIO_CRL_MODE4_1;

    GPIOA->CRL |= GPIO_CRL_MODE5_1;
    GPIOA->CRL |= GPIO_CRL_CNF5_1;

    GPIOA->CRL |= GPIO_CRL_CNF6_0;

    GPIOA->CRL |= GPIO_CRL_MODE7_1;
    GPIOA->CRL |= GPIO_CRL_CNF7_1;

    w25q64_release();
}

static void spi1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /*
     * SPI1 主机、软件 NSS、Mode 0、8 位传输。
     * BR_1 表示分频，降低 SCK 速度，方便外设稳定响应。
     */
    SPI1->CR1 = SPI_CR1_MSTR |
                SPI_CR1_SSM |
                SPI_CR1_SSI |
                SPI_CR1_BR_1 |
                SPI_CR1_SPE;
}

static uint8_t spi1_transfer(uint8_t tx_byte)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {
    }

    *(__IO uint8_t *)&SPI1->DR = tx_byte;

    while ((SPI1->SR & SPI_SR_RXNE) == 0U) {
    }

    return (uint8_t)SPI1->DR;
}

static uint8_t w25q64_read_manufacturer_id(void)
{
    uint8_t manufacturer_id;

    /*
     * 一笔 JEDEC ID 事务：
     * CS 拉低 -> 发 0x9F -> 发 dummy 读 ID -> 继续读其余 ID 字节 -> 等 BSY 清 -> CS 拉高。
     */
    w25q64_select();

    (void)spi1_transfer(0x9FU);
    manufacturer_id = spi1_transfer(0xFFU);
    (void)spi1_transfer(0xFFU);
    (void)spi1_transfer(0xFFU);

    while ((SPI1->SR & SPI_SR_BSY) != 0U) {
    }

    w25q64_release();

    return manufacturer_id;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    spi1_gpio_init();
    spi1_init();

    while (1) {
        uint8_t manufacturer_id = w25q64_read_manufacturer_id();

        pc13_toggle();

        if (manufacturer_id == 0xEFU) {
            delay_cycles(720000U);
        } else {
            delay_cycles(3600000U);
        }
    }
}
