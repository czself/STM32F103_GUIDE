#include "stm32f1xx.h"

/*
 * 普通 PWM 接灯条 DIN 观察实验。
 *
 * 目的：
 * - PA0 输出普通 PWM，不发送 WS2812/SK6812 协议数据。
 * - 你可以把 PA0 接到灯条 DIN，看灯条对普通 PWM 的反应。
 *
 * 预期：
 * - 数字灯条大概率不亮、乱闪、乱色，或者只偶尔闪一下。
 * - 这说明 DIN 需要专用数据时序，不是普通亮度 PWM。
 *
 * 接线：
 * - 灯条 5V  -> 外部 5V 电源 +
 * - 灯条 GND -> 外部 5V 电源 -
 * - STM32 GND -> 外部 5V 电源 -
 * - PA0 -> 220R~470R -> 灯条 DIN
 */

static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}

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

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void pa0_pwm_pin_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
}

static void pc13_led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void tim2_pwm_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 普通 1kHz PWM：
     * 72MHz / 72 = 1MHz
     * 1MHz / 1000 = 1kHz
     */
    TIM2->PSC = 72U - 1U;
    TIM2->ARR = 1000U - 1U;
    TIM2->CCR1 = 0U;

    TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
    TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM2->CCMR1 |= TIM_CCMR1_OC1PE;

    TIM2->CCER |= TIM_CCER_CC1E;
    TIM2->EGR |= TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
}

int main(void)
{
    static const uint16_t duties[] = {
        0U,
        100U,
        250U,
        500U,
        750U,
        900U,
        1000U,
        500U,
    };
    uint32_t index = 0U;

    system_clock_72mhz_init();
    pc13_led_init();
    pa0_pwm_pin_init();
    tim2_pwm_init();

    while (1) {
        /*
         * 每隔一段时间切换普通 PWM 占空比。
         * 这不是灯条协议，只是为了观察 DIN 对普通 PWM 的反应。
         */
        TIM2->CCR1 = duties[index];

        if (duties[index] == 0U) {
            GPIOC->BSRR = GPIO_BSRR_BS13;
        } else {
            GPIOC->BRR = GPIO_BRR_BR13;
        }

        index++;
        if (index >= (sizeof(duties) / sizeof(duties[0]))) {
            index = 0U;
        }

        delay(6000000U);
    }
}
