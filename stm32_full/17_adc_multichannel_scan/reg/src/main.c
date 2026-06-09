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

static volatile uint16_t g_adc0, g_adc1;
static void adc_scan_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 | GPIO_CRL_MODE1 | GPIO_CRL_CNF1); /* PA0/PA1 模拟输入。 */
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_CONT;
    ADC1->SMPR2 = ADC_SMPR2_SMP0 | ADC_SMPR2_SMP1;
    ADC1->SQR1 = ADC_SQR1_L_0; /* 2 个转换，L 写入 N-1。 */
    ADC1->SQR3 = (0U << 0) | (1U << 5);
    ADC1->CR2 |= ADC_CR2_RSTCAL; while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {}
    ADC1->CR2 |= ADC_CR2_CAL; while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {}
    ADC1->CR2 |= ADC_CR2_SWSTART;
}
int main(void){ system_clock_72mhz_init(); pc13_led_init(); adc_scan_init(); while(1){ while((ADC1->SR & ADC_SR_EOC)==0U){} g_adc0=ADC1->DR; while((ADC1->SR & ADC_SR_EOC)==0U){} g_adc1=ADC1->DR; if(g_adc0>g_adc1) pc13_toggle(); delay_cycles(720000U); } }
