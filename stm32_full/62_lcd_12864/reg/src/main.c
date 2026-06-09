#include "stm32f1xx.h"

/*
 * 12864 LCD 串口镜像寄存器版。
 *
 * 目标：保持和 HAL 版相同的协议入口：
 *   [0xAA] [0x02] [1024 bytes bitmap]
 *   [0xAA] [0x01] [64 bytes text]
 *
 * 本寄存器版重点展示三条底层链路：
 *   1. USART1 以 921600 接收 PC 端帧
 *   2. SPI1 以主机模式向 ST7920 串行口发送数据
 *   3. PB0/PB1 控制 LCD CS/RST，PC13 表示收到完整帧
 *
 * 为了让本课能作为底层对照，代码保留最小可运行协议框架。
 */

#define FRAME_HEAD 0xAAU
#define MODE_TEXT  0x01U
#define MODE_BMP   0x02U
#define ACK_BYTE   0x55U

static uint8_t g_frame[1024];

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPAEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    GPIOB->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 | GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
    GPIOB->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_MODE1_1;
    GPIOB->BSRR = GPIO_BSRR_BS0 | GPIO_BSRR_BS1;
}

static void usart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1 | GPIO_CRH_CNF10_0;
    USART1->BRR = 0x004E; /* 72MHz / 921600 约等于 78.125 */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t usart1_rx(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0U) {}
    return (uint8_t)USART1->DR;
}

static void usart1_tx(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {}
    USART1->DR = b;
}

static void spi1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_IOPAEN;
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOA->CRL |= GPIO_CRL_MODE5_1 | GPIO_CRL_CNF5_1 |
                  GPIO_CRL_MODE7_1 | GPIO_CRL_CNF7_1;
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                SPI_CR1_BR_1 | SPI_CR1_SPE;
}

static void spi1_write(uint8_t b)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {}
    *(__IO uint8_t *)&SPI1->DR = b;
    while ((SPI1->SR & SPI_SR_BSY) != 0U) {}
}

static void lcd_select(uint8_t on)
{
    if (on) GPIOB->BRR = GPIO_BRR_BR0;
    else GPIOB->BSRR = GPIO_BSRR_BS0;
}

static void lcd_send_raw(uint8_t b)
{
    lcd_select(1);
    spi1_write(b);
    lcd_select(0);
}

static void lcd_send_frame_preview(const uint8_t *data, uint16_t len)
{
    /*
     * ST7920 串行完整刷新需要命令/地址/数据拆包。
     * 本寄存器版先保留底层 SPI 发送链路：收到 PC 帧后把前 64 字节送入 LCD 串口，
     * 用作和 HAL 版完整刷新代码对照的最小底层实验。
     */
    uint16_t n = (len > 64U) ? 64U : len;
    for (uint16_t i = 0; i < n; ++i) {
        lcd_send_raw(data[i]);
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();
    usart1_init();
    spi1_init();

    while (1) {
        uint8_t head = usart1_rx();
        if (head != FRAME_HEAD) continue;
        uint8_t mode = usart1_rx();
        uint16_t len = (mode == MODE_BMP) ? 1024U : ((mode == MODE_TEXT) ? 64U : 0U);
        if (len == 0U) continue;
        for (uint16_t i = 0; i < len; ++i) {
            g_frame[i] = usart1_rx();
        }
        lcd_send_frame_preview(g_frame, len);
        GPIOC->ODR ^= GPIO_ODR_ODR13;
        usart1_tx(ACK_BYTE);
    }
}
