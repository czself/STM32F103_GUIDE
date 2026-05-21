#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 PWM 基础”。
 *
 * 目标：
 * 1. 使用 TIM2_CH1 在 PA0 输出 PWM
 * 2. PWM 频率配置为 1kHz
 * 3. 在主循环中动态修改 CCR1，观察 LED 亮度变化
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
    /*
     * 打开 GPIOA 时钟和 AFIO 时钟。
     *
     * GPIOA 用于操作 PA0。
     * AFIO 代表复用功能相关体系，本课使用定时器通道输出，保留它更清晰。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /*
     * PA0 属于 0~7 号引脚，所以配置在 CRL。
     *
     * 本课目标模式：
     * - MODE0 = 10 -> 输出模式，最大速度 2MHz
     * - CNF0  = 10 -> 复用推挽输出
     *
     * 这说明这个引脚的电平将由“复用外设”控制，
     * 本课里这个复用外设就是 TIM2_CH1。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOA->CRL |= GPIO_CRL_MODE0_1 | GPIO_CRL_CNF0_1;
}

static void tim2_pwm_init(void)
{
    /*
     * 打开 TIM2 时钟。
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /*
     * 当前配置下 TIM2 输入时钟按 72MHz 计算。
     *
     * 目标 PWM 频率：1kHz
     *
     * 先设 PSC = 72 - 1：
     * 72MHz / 72 = 1MHz
     *
     * 再设 ARR = 1000 - 1：
     * 1MHz / 1000 = 1kHz
     */
    TIM2->PSC = 72U - 1U;
    TIM2->ARR = 1000U - 1U;

    /*
     * 初始占空比设为 25%。
     *
     * ARR = 999，相当于一个周期共 1000 个计数。
     * CCR1 = 250 表示前 250 个计数输出高电平。
     * 因此占空比约为 25%。
     */
    TIM2->CCR1 = 250U;

    /*
     * 配置通道 1 为输出模式，并选择 PWM mode 1。
     *
     * CC1S = 00 -> 通道 1 作为输出
     * OC1M = 110 -> PWM mode 1
     * OC1PE = 1  -> 允许 CCR1 预装载
     */
    TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
    TIM2->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM2->CCMR1 |= TIM_CCMR1_OC1PE;

    /*
     * 打开通道 1 输出。
     *
     * CC1E = Capture/Compare 1 output enable
     */
    TIM2->CCER |= TIM_CCER_CC1E;

    /*
     * 产生一次更新事件，让 PSC/ARR/CCR 等预装载值立即生效。
     */
    TIM2->EGR |= TIM_EGR_UG;

    /*
     * 启动定时器计数器。
     */
    TIM2->CR1 |= TIM_CR1_CEN;
}

int main(void)
{
    uint16_t duty = 0U;
    int16_t step = 50;

    system_clock_72mhz_init();
    pa0_pwm_pin_init();
    tim2_pwm_init();

    while (1) {
        /*
         * 动态修改 CCR1，相当于动态修改占空比。
         *
         * duty 的取值范围控制在 0~1000 之间，
         * 对应 0%~100% 附近的占空比。
         */
        TIM2->CCR1 = duty;

        delay(180000U);

        if ((int32_t)duty + step >= 1000) {
            duty = 1000U;
            step = -50;
        } else if ((int32_t)duty + step <= 0) {
            duty = 0U;
            step = 50;
        } else {
            duty = (uint16_t)((int32_t)duty + step);
        }
    }
}

