#include "stm32f1xx.h"

/*
 * 寄存器版：12864 LCD 串口镜像显示的最小底层链路。
 *
 * 本课没有 FreeRTOS。代码只演示：
 * - USART1 以 921600 接收 PC 帧
 * - SPI1 以主机模式向 ST7920 串行口发送数据
 * - PB0/PB1 控制 LCD CS/RST，PC13 和 ACK 表示一帧处理完成
 *
 * HAL 版负责完整 ST7920 文本/位图刷新；本寄存器版保留可对照的底层预览链路。
 */

#define FRAME_HEAD 0xAAU
#define MODE_TEXT  0x01U
#define MODE_BMP   0x02U
#define ACK_BYTE   0x55U

#define TEXT_LEN   64U
#define BMP_LEN    1024U

static uint8_t g_frame[BMP_LEN];

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

static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPAEN;

    /* PC13：收到并处理完整帧后翻转，帮助确认协议链路走通。 */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;

    /*
     * PB0：LCD CS/RS。
     * PB1：LCD RST，低有效复位。
     */
    GPIOB->CRL &= ~(GPIO_CRL_MODE0 |
                    GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 |
                    GPIO_CRL_CNF1);
    GPIOB->CRL |= GPIO_CRL_MODE0_1;
    GPIOB->CRL |= GPIO_CRL_MODE1_1;
    GPIOB->BSRR = GPIO_BSRR_BS0 | GPIO_BSRR_BS1;
}

static void usart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* PA9 是 USART1_TX 复用推挽，PA10 是 USART1_RX 输入。 */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 |
                    GPIO_CRH_CNF9 |
                    GPIO_CRH_MODE10 |
                    GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;
    GPIOA->CRH |= GPIO_CRH_CNF10_0;

    /*
     * 目标串口速率是 921600，和本课 platformio.ini 的 monitor_speed 对齐。
     * 这是为了让 1024 字节位图帧传输不至于太慢。
     */
    USART1->BRR = 0x004EU;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t usart1_rx(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0U) {
    }

    return (uint8_t)USART1->DR;
}

static void usart1_tx(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }

    USART1->DR = b;
}

static void spi1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_IOPAEN;

    /*
     * PA5 = SPI1 SCK，接 LCD E/SCLK。
     * PA7 = SPI1 MOSI，接 LCD R/W/SID。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE5 |
                    GPIO_CRL_CNF5 |
                    GPIO_CRL_MODE7 |
                    GPIO_CRL_CNF7);
    GPIOA->CRL |= GPIO_CRL_MODE5_1;
    GPIOA->CRL |= GPIO_CRL_CNF5_1;
    GPIOA->CRL |= GPIO_CRL_MODE7_1;
    GPIOA->CRL |= GPIO_CRL_CNF7_1;

    /*
     * ST7920 不是标准 SPI 从机，但可以借 SPI1 产生 SCK/MOSI 波形。
     * BR_1 分频让通信速度保守一点，避免 LCD 来不及采样。
     */
    SPI1->CR1 = SPI_CR1_MSTR |
                SPI_CR1_SSM |
                SPI_CR1_SSI |
                SPI_CR1_BR_1 |
                SPI_CR1_SPE;
}

static void spi1_write(uint8_t b)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {
    }

    *(__IO uint8_t *)&SPI1->DR = b;

    while ((SPI1->SR & SPI_SR_BSY) != 0U) {
    }
}

static void lcd_select(uint8_t on)
{
    if (on != 0U) {
        GPIOB->BRR = GPIO_BRR_BR0;
    } else {
        GPIOB->BSRR = GPIO_BSRR_BS0;
    }
}

static void lcd_send_raw(uint8_t b)
{
    lcd_select(1U);
    spi1_write(b);
    lcd_select(0U);
}

static void lcd_send_frame_preview(const uint8_t *data, uint16_t len)
{
    /*
     * 完整 ST7920 显示需要把命令/数据按 0xF8/0xFA + 高低半字节拆成三字节。
     * 本寄存器版只保留底层 SPI 预览：收到 PC 帧后把前 64 字节送入 LCD 串口，
     * 用来验证 USART 收帧、SPI 发送、CS 控制这三层已经连起来。
     */
    uint16_t n = len;

    if (n > TEXT_LEN) {
        n = TEXT_LEN;
    }

    for (uint16_t i = 0; i < n; i++) {
        lcd_send_raw(data[i]);
    }
}

static uint16_t frame_payload_len(uint8_t mode)
{
    if (mode == MODE_TEXT) {
        return TEXT_LEN;
    }

    if (mode == MODE_BMP) {
        return BMP_LEN;
    }

    return 0U;
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();
    usart1_init();
    spi1_init();

    while (1) {
        uint8_t head = usart1_rx();

        if (head != FRAME_HEAD) {
            continue;
        }

        uint8_t mode = usart1_rx();
        uint16_t len = frame_payload_len(mode);

        if (len == 0U) {
            continue;
        }

        for (uint16_t i = 0; i < len; i++) {
            g_frame[i] = usart1_rx();
        }

        lcd_send_frame_preview(g_frame, len);

        GPIOC->ODR ^= GPIO_ODR_ODR13;
        usart1_tx(ACK_BYTE);
    }
}
