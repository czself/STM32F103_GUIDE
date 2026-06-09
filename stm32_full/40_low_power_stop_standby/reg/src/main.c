#include "stm32f1xx.h"

/* Stop 模式寄存器版：PA0 EXTI 唤醒后恢复系统时钟。 */
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

static volatile uint8_t woken;
static void pa0_exti_init(void){RCC->APB2ENR|=RCC_APB2ENR_IOPAEN|RCC_APB2ENR_AFIOEN;GPIOA->CRL&=~(GPIO_CRL_MODE0|GPIO_CRL_CNF0);GPIOA->CRL|=GPIO_CRL_CNF0_1;GPIOA->BSRR=GPIO_BSRR_BS0;AFIO->EXTICR[0]&=~AFIO_EXTICR1_EXTI0;EXTI->IMR|=EXTI_IMR_MR0;EXTI->FTSR|=EXTI_FTSR_TR0;NVIC_EnableIRQ(EXTI0_IRQn);}
void EXTI0_IRQHandler(void){if(EXTI->PR&EXTI_PR_PR0){EXTI->PR=EXTI_PR_PR0;woken=1;}}
static void enter_stop(void){RCC->APB1ENR|=RCC_APB1ENR_PWREN;PWR->CR&=~PWR_CR_PDDS;PWR->CR|=PWR_CR_CWUF;SCB->SCR|=SCB_SCR_SLEEPDEEP_Msk;__WFI();SCB->SCR&=~SCB_SCR_SLEEPDEEP_Msk;}
int main(void){system_clock_72mhz_init();pc13_led_init();pa0_exti_init();while(1){woken=0;enter_stop();system_clock_72mhz_init();if(woken){pc13_toggle();delay_cycles(3600000U);}}}
