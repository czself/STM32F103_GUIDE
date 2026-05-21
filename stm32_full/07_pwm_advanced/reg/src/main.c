#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 PWM 进阶”。
 *
 * 目标：
 * 1. 继续使用 TIM2_CH1 在 PA0 输出 1kHz PWM
 * 2. 通过更合理的占空比更新策略，实现更自然的呼吸灯
 *
 * 硬件接法：
 * - PA0 -> 220Ω 电阻 -> LED 正极
 * - LED 负极 -> GND
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

static void tim2_pwm_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 继续沿用上一课的 PWM 基础参数：
     * - 定时器输入时钟 = 72MHz
     * - PSC = 72 - 1  -> 1MHz
     * - ARR = 1000 - 1 -> 1kHz PWM
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

static uint16_t next_duty(uint16_t duty, int8_t direction)
{
    /*
     * 本函数根据当前亮度区间，决定下一步增减多少。
     *
     * 设计思路：
     * - 暗部变化更细：避免刚亮起来时太突兀
     * - 中间区域变化适中：让整体节奏不要太拖
     * - 高亮区域可以稍大：因为人眼在高亮区的微小变化不那么明显
     */
    uint16_t step;

    if (duty < 120U) {
        step = 5U;
    } else if (duty < 400U) {
        step = 15U;
    } else if (duty < 750U) {
        step = 25U;
    } else {
        step = 12U;
    }

    if (direction > 0) {
        if ((uint32_t)duty + step >= 1000U) {
            return 1000U;
        }
        return (uint16_t)(duty + step);
    }

    if (duty <= step) {
        return 0U;
    }
    return (uint16_t)(duty - step);
}

int main(void)
{
    uint16_t duty = 0U;
    int8_t direction = 1;

    system_clock_72mhz_init();
    pa0_pwm_pin_init();
    tim2_pwm_init();

    while (1) {
        /*
         * 更新当前 PWM 占空比。
         * TIM2->CCR1 越大，一个周期内高电平越长，LED 越亮。
         */
        TIM2->CCR1 = duty;

        /*
         * 给亮度变化一点时间，让人眼看出“渐变”。
         */
        delay(120000U);

        /*
         * 到达边界时改变方向：
         * - 到最亮后开始变暗
         * - 到最暗后开始变亮
         */
        if (duty >= 1000U) {
            direction = -1;
        } else if (duty == 0U) {
            direction = 1;
        }

        duty = next_duty(duty, direction);
    }
}

