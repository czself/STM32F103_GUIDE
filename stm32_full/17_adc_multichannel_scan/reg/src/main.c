#include "stm32f1xx.h"

/*
 * 寄存器版：ADC 多通道扫描。
 *
 * 15/16 已经讲过单通道 ADC 的启动、等待 EOC、读取 DR。
 * 本课的新重点是规则组扫描：
 * - PA0 对应 ADC1_IN0，放在规则组第 1 个转换
 * - PA1 对应 ADC1_IN1，放在规则组第 2 个转换
 * - 软件读取 DR 的顺序必须和规则组顺序一致
 */

static volatile uint16_t g_adc0;
static volatile uint16_t g_adc1;

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

static void adc_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA0/PA1 都要进入模拟输入模式。
     * 如果仍按普通 GPIO 输入配置，数字输入缓冲会干扰模拟采样，也不符合 ADC 输入链路。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 |
                    GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 |
                    GPIO_CRL_CNF1);
}

static void adc_scan_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /*
     * ADC 时钟不能直接用 72MHz。
     * PCLK2=72MHz，ADCPRE=/6 后 ADC 时钟为 12MHz，落在 F103 允许范围内。
     */
    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;

    /*
     * SCAN=1：规则组不再只转换一个通道，而是按 SQR 里写的顺序扫描多个通道。
     */
    ADC1->CR1 = ADC_CR1_SCAN;

    /*
     * CONT=1：连续扫描。一次规则组完成后，下一轮会继续开始。
     * EXTTRIG=1：允许软件触发规则组转换。
     *
     * F103 的规则组软件启动不是只写 SWSTART 就一定生效，
     * 还要先打开 EXTTRIG 这道“触发允许门”。
     */
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_CONT;
    ADC1->CR2 |= ADC_CR2_EXTTRIG;

    /*
     * 给 CH0/CH1 都设置较长采样时间，适合电位器这类源阻抗不低的输入。
     */
    ADC1->SMPR2 = ADC_SMPR2_SMP0 | ADC_SMPR2_SMP1;

    /*
     * SQR1.L 写的是“转换个数 - 1”。
     * L=1 表示规则组里有 2 个转换。
     */
    ADC1->SQR1 = ADC_SQR1_L_0;

    /*
     * SQR3 决定前 6 个规则转换的通道号。
     * SQ1=0：第 1 次转换采 ADC_IN0，也就是 PA0。
     * SQ2=1：第 2 次转换采 ADC_IN1，也就是 PA1。
     */
    ADC1->SQR3 = (0U << 0) | (1U << 5);

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {
    }

    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {
    }

    /*
     * SWSTART：启动第一轮扫描。
     * 因为上面已经打开 CONT，第一轮完成后 ADC 会继续下一轮扫描。
     */
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

static uint16_t adc_wait_and_read(void)
{
    while ((ADC1->SR & ADC_SR_EOC) == 0U) {
    }

    /*
     * 读取 DR 会取出当前转换结果，并推进到下一次读取。
     * 在扫描模式下，变量赋值顺序必须跟 SQR3 的 rank 顺序一致。
     */
    return (uint16_t)ADC1->DR;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    adc_gpio_init();
    adc_scan_init();

    while (1) {
        g_adc0 = adc_wait_and_read();
        g_adc1 = adc_wait_and_read();

        if (g_adc0 > g_adc1) {
            pc13_toggle();
        }

        delay_cycles(720000U);
    }
}
