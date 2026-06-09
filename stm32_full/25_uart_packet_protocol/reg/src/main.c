#include "stm32f1xx.h"

/* USART 数据包寄存器版：解析 AA CMD DATA 55 四字节帧。 */
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

static void usart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
    GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
    GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1 | GPIO_CRH_CNF10_0;
    USART1->BRR = 0x0271; /* 72MHz / 115200 */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t usart1_read_byte(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0U) {
    }
    return (uint8_t)USART1->DR;
}

static void handle_packet(uint8_t cmd, uint8_t data)
{
    if (cmd == 0x01U) {
        if (data != 0U) GPIOC->BRR = GPIO_BRR_BR13;
        else GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

int main(void)
{
    enum { WAIT_HEAD, WAIT_CMD, WAIT_DATA, WAIT_TAIL } state = WAIT_HEAD;
    uint8_t cmd = 0, data = 0;
    system_clock_72mhz_init();
    pc13_led_init();
    usart1_init();
    while (1) {
        uint8_t b = usart1_read_byte();
        switch (state) {
        case WAIT_HEAD: state = (b == 0xAAU) ? WAIT_CMD : WAIT_HEAD; break;
        case WAIT_CMD:  cmd = b; state = WAIT_DATA; break;
        case WAIT_DATA: data = b; state = WAIT_TAIL; break;
        case WAIT_TAIL:
            if (b == 0x55U) handle_packet(cmd, data);
            state = WAIT_HEAD;
            break;
        }
    }
}
