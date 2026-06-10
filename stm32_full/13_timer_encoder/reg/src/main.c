#include "stm32f1xx.h"

/*
 * 寄存器版：TIM3 编码器接口。
 *
 * 前面几课已经学过定时器输入捕获和 PWM 输入。
 * 本课的新重点是：TIM 可以把两路正交信号 A/B 相直接解码成计数器增减。
 *
 * 接线：
 * - PA6 = TIM3_CH1 = 编码器 A 相
 * - PA7 = TIM3_CH2 = 编码器 B 相
 *
 * 当旋钮转动时，TIM3->CNT 会自动增加或减少，软件只需要读取 CNT。
 */

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

static void pa6_pa7_encoder_pin_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA6/PA7 接机械编码器 A/B 相，常见模块输出开关信号。
     * 这里使用输入上拉，空闲时保持高电平，触点闭合时拉低。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 |
                    GPIO_CRL_CNF6 |
                    GPIO_CRL_MODE7 |
                    GPIO_CRL_CNF7);

    GPIOA->CRL |= GPIO_CRL_CNF6_1;
    GPIOA->CRL |= GPIO_CRL_CNF7_1;

    GPIOA->BSRR = GPIO_BSRR_BS6 | GPIO_BSRR_BS7;
}

static void tim3_encoder_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /*
     * 编码器模式下，CNT 不再靠内部固定频率自增。
     * A/B 相边沿到来时，硬件根据相位关系决定 CNT 增加还是减少。
     */
    TIM3->PSC = 0U;
    TIM3->ARR = 0xFFFFU;

    /*
     * CC1S = 01：CH1 输入来自 TI1，也就是 PA6/A 相。
     * CC2S = 01：CH2 输入来自 TI2，也就是 PA7/B 相。
     */
    TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_CC2S);
    TIM3->CCMR1 |= TIM_CCMR1_CC1S_0;
    TIM3->CCMR1 |= TIM_CCMR1_CC2S_0;

    /*
     * 不反相输入极性。
     * 如果你发现旋转方向和预期相反，可以交换 A/B 相接线，或调整输入极性。
     */
    TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P);

    /*
     * SMS = 011：encoder mode 3。
     * 这个模式同时使用 TI1 和 TI2 的边沿计数，分辨率比只看一路更高。
     */
    TIM3->SMCR &= ~TIM_SMCR_SMS;
    TIM3->SMCR |= TIM_SMCR_SMS_0;
    TIM3->SMCR |= TIM_SMCR_SMS_1;

    TIM3->CNT = 0U;
    TIM3->CR1 |= TIM_CR1_CEN;
}

int main(void)
{
    uint16_t last_count;

    system_clock_72mhz_init();
    pc13_led_init();
    pa6_pa7_encoder_pin_init();
    tim3_encoder_init();

    last_count = (uint16_t)TIM3->CNT;

    while (1) {
        uint16_t now_count = (uint16_t)TIM3->CNT;

        /*
         * CNT 一变化，就说明编码器产生了新的边沿。
         * 这里翻转 PC13 只是运行现象；真正要观察的是调试器里的 TIM3->CNT。
         */
        if (now_count != last_count) {
            last_count = now_count;
            pc13_toggle();
        }
    }
}
