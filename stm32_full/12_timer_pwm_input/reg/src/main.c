#include "stm32f1xx.h"

/*
 * 寄存器版：TIM PWM 输入模式。
 *
 * 11 已经讲过“普通输入捕获”：记录边沿时间戳，再由软件相减。
 * 本课的新重点是 PWM 输入模式：
 * - 只接 PA6 一个输入信号
 * - TIM3_CH1 捕获上升沿，得到整个周期
 * - TIM3_CH2 捕获同一个 TI1 的下降沿，得到高电平时间
 * - 每个上升沿用 reset mode 把 CNT 清零
 *
 * 因此，软件读 CCR1/CCR2 就能直接拿到周期和高电平宽度。
 */

static volatile uint32_t g_period_ticks;
static volatile uint32_t g_high_ticks;

static void system_clock_72mhz_init(void)
{
    /* 时钟树前面已经讲过；这里保留 72MHz 作为 1MHz 定时器计数的前提。 */
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
    /* PC13 只做运行心跳，GPIO 输出细节前面已经讲过。 */
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

static void pa6_pwm_input_pin_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA6 是 TIM3_CH1 的输入脚。
     * 本课测量外部 PWM，所以 PA6 配成浮空输入；外部信号源必须和板子共地。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6);
    GPIOA->CRL |= GPIO_CRL_CNF6_0;
}

static void tim3_pwm_input_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /*
     * TIM3 以 1MHz 计数，1 个 tick 就是 1us。
     * 后面 g_period_ticks 和 g_high_ticks 可以直接按微秒理解。
     */
    TIM3->PSC = 72U - 1U;
    TIM3->ARR = 0xFFFFU;

    /*
     * PWM 输入模式的关键：两个捕获通道看同一个 TI1 输入。
     *
     * CC1S = 01：CH1 直连 TI1，用来捕获上升沿，得到周期。
     * CC2S = 10：CH2 间接连接 TI1，用来捕获下降沿，得到高电平时间。
     */
    TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_CC2S);
    TIM3->CCMR1 |= TIM_CCMR1_CC1S_0;
    TIM3->CCMR1 |= TIM_CCMR1_CC2S_1;

    /*
     * CH1 捕获上升沿，CH2 捕获下降沿。
     * 上升沿到来时，CCR1 记录周期；下降沿到来时，CCR2 记录从上升沿开始到下降沿的时间。
     */
    TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P);
    TIM3->CCER |= TIM_CCER_CC1E;
    TIM3->CCER |= TIM_CCER_CC2E;
    TIM3->CCER |= TIM_CCER_CC2P;

    /*
     * 从模式 reset 是 PWM 输入模式能“直接读周期”的关键。
     * TS = TI1FP1：触发源来自 TI1 的滤波后上升沿。
     * SMS = reset mode：每个触发到来时，把 CNT 清零。
     */
    TIM3->SMCR &= ~(TIM_SMCR_TS | TIM_SMCR_SMS);
    TIM3->SMCR |= TIM_SMCR_TS_2 | TIM_SMCR_TS_0;
    TIM3->SMCR |= TIM_SMCR_SMS_2;

    TIM3->SR = 0U;
    TIM3->CR1 |= TIM_CR1_CEN;
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    pa6_pwm_input_pin_init();
    tim3_pwm_input_init();

    while (1) {
        /*
         * CC1IF 表示 CH1 捕获到新的上升沿。
         * 此时 CCR1 是周期，CCR2 是同一周期内下降沿对应的高电平时间。
         */
        if ((TIM3->SR & TIM_SR_CC1IF) != 0U) {
            g_period_ticks = TIM3->CCR1;
            g_high_ticks = TIM3->CCR2;

            TIM3->SR &= ~TIM_SR_CC1IF;
            pc13_toggle();
        }
    }
}
