#include "stm32f1xx.h"

static void system_clock_72mhz_init(void)
{
    /* Flash 读指令也需要时间。72MHz 下必须配置 2 个等待周期，否则 CPU 可能取指不稳定。 */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    /* HSE 是板上 8MHz 外部晶振。先等 HSE 稳定，再把它送进 PLL。 */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);
    /* APB1 最高 36MHz，所以 HCLK=72MHz 时 PCLK1 必须二分频。 */
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}

static void pc13_led_init(void)
{
    /* GPIOC 在 APB2 总线上。不开 IOPCEN，PC13 的模式寄存器不会真正驱动硬件。 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    /* PC13 属于 8~15 号引脚，所以配置 CRH；先清 MODE/CNF，避免旧模式残留。 */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    /* MODE13=10 表示 2MHz 输出，CNF13=00 表示通用推挽输出。 */
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    /* BluePill 的 PC13 LED 通常低电平亮，初始化先输出高电平让它熄灭。 */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void pc13_toggle(void)
{
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- != 0U) { __NOP(); }
}

static uint8_t g_src[16] = "DMA UART demo\n";
static uint8_t g_dst[16];
static void app_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN | RCC_APB2ENR_AFIOEN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1;
    USART1->BRR = 72000000U / 115200U;
    USART1->CR3 = USART_CR3_DMAT;
    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}
static void dma_mem_copy(void)
{
    DMA1_Channel1->CCR = 0; DMA1_Channel1->CPAR = (uint32_t)g_src; DMA1_Channel1->CMAR = (uint32_t)g_dst; DMA1_Channel1->CNDTR = sizeof(g_src);
    DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_PINC | DMA_CCR_DIR | DMA_CCR_MEM2MEM | DMA_CCR_PL_0 | DMA_CCR_EN;
    while ((DMA1->ISR & DMA_ISR_TCIF1) == 0U) {} DMA1->IFCR = DMA_IFCR_CTCIF1;
}
static void dma_uart_send(void)
{
    DMA1_Channel4->CCR = 0; DMA1_Channel4->CPAR = (uint32_t)&USART1->DR; DMA1_Channel4->CMAR = (uint32_t)g_dst; DMA1_Channel4->CNDTR = sizeof(g_dst);
    DMA1_Channel4->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PL_0 | DMA_CCR_EN;
    while ((DMA1->ISR & DMA_ISR_TCIF4) == 0U) {} DMA1->IFCR = DMA_IFCR_CTCIF4;
}
int main(void){ system_clock_72mhz_init(); pc13_led_init(); app_init(); while(1){ dma_mem_copy(); dma_uart_send(); pc13_toggle(); delay_cycles(7200000U); } }
