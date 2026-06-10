#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 PWM 进阶”。
 *
 * 08 已经学过 PWM 的基本链路：
 * PA0 复用输出 -> TIM2_CH1 -> ARR 决定周期 -> CCR1 决定占空比。
 *
 * 本课的新重点不是再配一遍 PWM，而是观察“占空比更新策略”：
 * - 线性地每次加同样的 CCR1，亮度变化会显得生硬
 * - 分区间选择不同步长，可以让暗部更细腻、高亮区不拖沓
 * - 到达 0% / 100% 边界时改变方向，形成呼吸灯循环
 *
 * 硬件接法：
 * - PA0 -> 220Ω 电阻 -> LED 正极
 * - LED 负极 -> GND
 */

static void delay(volatile uint32_t count)
{
    /* 这里只负责放慢呼吸变化；精确延时不是本课重点。 */
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
    /* 复用输出链路在 08 已讲过：PA0 要交给 TIM2_CH1 驱动。 */
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
     * 本函数只处理“下一步占空比是多少”，不直接碰 TIM2->CCR1。
     *
     * 这样主循环可以保持清楚：
     * 1. 把当前 duty 写入 CCR1
     * 2. 根据边界决定呼吸方向
     * 3. 算出下一次 duty
     *
     * 设计思路：
     * - duty 很小时，用 5 的小步长，避免 LED 从全灭突然跳亮
     * - duty 进入中段后，步长加大，让呼吸节奏不要太慢
     * - 接近高亮时再收一点，避免到顶端时变化太突然
     *
     * 这不是严格的亮度校正算法，只是给初学者看的“非线性更新”示例。
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
        /*
         * 变亮方向不能超过 ARR+1 对应的满占空比范围。
         * 本课 ARR = 999，因此把 duty 夹在 1000 以内。
         */
        if ((uint32_t)duty + step >= 1000U) {
            return 1000U;
        }
        return (uint16_t)(duty + step);
    }

    if (duty <= step) {
        /* 变暗方向到达底部时直接回到 0，避免无符号数下溢。 */
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
         *
         * 这里每轮只写一次 CCR1，定时器硬件会在后台持续输出 PWM。
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
         *
         * 注意这里先判断边界，再计算下一步 duty，
         * 可以避免在 0 和 1000 附近来回越界。
         */
        if (duty >= 1000U) {
            direction = -1;
        } else if (duty == 0U) {
            direction = 1;
        }

        duty = next_duty(duty, direction);
    }
}
