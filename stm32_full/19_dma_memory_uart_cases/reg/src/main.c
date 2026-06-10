#include "stm32f1xx.h"

/*
 * 寄存器版：DMA 内存拷贝与 USART1 发送。
 *
 * 18 已经讲过 ADC -> DMA -> RAM，这属于“外设到内存”。
 * 本课换两个方向：
 * - DMA1_Channel1：RAM -> RAM，演示内存到内存
 * - DMA1_Channel4：RAM -> USART1->DR，演示内存到外设
 */

static uint8_t g_src[16] = "DMA UART demo\n";
static uint8_t g_dst[16];

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

static void usart1_tx_pin_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /*
     * PA9 是 USART1_TX。
     * 发送脚必须配置为复用推挽输出，电平由 USART1 外设驱动。
     */
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOA->CRH |= GPIO_CRH_MODE9_1;
    GPIOA->CRH |= GPIO_CRH_CNF9_1;
}

static void usart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /*
     * USART1 在本课只发送，波特率 115200。
     * 这里使用 72MHz PCLK2 计算 BRR，保持示例简单。
     */
    USART1->BRR = 72000000U / 115200U;

    /*
     * DMAT 是 USART 侧的 DMA 请求开关。
     * 没有它，DMA1_Channel4 即使配置好了，USART1 也不会向 DMA 要数据。
     */
    USART1->CR3 = USART_CR3_DMAT;

    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void dma_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
}

static void dma_mem_copy(void)
{
    /*
     * Channel1 用作 MEM2MEM。
     *
     * CPAR/CMAR 在 MEM2MEM 场景里可以理解成“源地址/目的地址”：
     * - CPAR = g_src
     * - CMAR = g_dst
     * DIR=1 表示从 CPAR 指向的位置搬到 CMAR 指向的位置。
     */
    DMA1_Channel1->CCR = 0U;
    DMA1->IFCR = DMA_IFCR_CTCIF1;

    DMA1_Channel1->CPAR = (uint32_t)g_src;
    DMA1_Channel1->CMAR = (uint32_t)g_dst;
    DMA1_Channel1->CNDTR = sizeof(g_src);

    DMA1_Channel1->CCR = DMA_CCR_MINC |
                         DMA_CCR_PINC |
                         DMA_CCR_DIR |
                         DMA_CCR_MEM2MEM |
                         DMA_CCR_PL_0 |
                         DMA_CCR_EN;

    while ((DMA1->ISR & DMA_ISR_TCIF1) == 0U) {
    }

    DMA1->IFCR = DMA_IFCR_CTCIF1;
}

static void dma_uart_send(void)
{
    /*
     * USART1_TX 使用 DMA1_Channel4。
     *
     * 这次不是 MEM2MEM：
     * - 外设地址固定为 USART1->DR，所以 PINC 不能开
     * - 内存地址 g_dst 要逐字节递增，所以 MINC 要开
     * - DIR=1 表示内存到外设
     */
    DMA1_Channel4->CCR = 0U;
    DMA1->IFCR = DMA_IFCR_CTCIF4;

    DMA1_Channel4->CPAR = (uint32_t)&USART1->DR;
    DMA1_Channel4->CMAR = (uint32_t)g_dst;
    DMA1_Channel4->CNDTR = sizeof(g_dst);

    DMA1_Channel4->CCR = DMA_CCR_MINC |
                         DMA_CCR_DIR |
                         DMA_CCR_PL_0 |
                         DMA_CCR_EN;

    while ((DMA1->ISR & DMA_ISR_TCIF4) == 0U) {
    }

    DMA1->IFCR = DMA_IFCR_CTCIF4;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    usart1_tx_pin_init();
    usart1_init();
    dma_init();

    while (1) {
        dma_mem_copy();
        dma_uart_send();

        pc13_toggle();
        delay_cycles(7200000U);
    }
}
