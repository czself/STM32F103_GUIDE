#include "stm32f1xx.h"

/*
 * 环境入门寄存器版：用最小代码确认“能编译、能烧录、能运行”。
 *
 * 这节课不急着把 RCC/GPIO 每一位都讲透，后面 01~03 会专门展开。
 * 这里先建立一条最短验证链路：
 * - 把系统时钟切到常用 72MHz
 * - 把 PC13 配成输出
 * - 周期性翻转 PC13，观察板载 LED 是否闪烁
 */
static void system_clock_72mhz_init(void)
{
    /* 72MHz 运行前需要配置 Flash 等待周期；时钟树课会细讲原因。 */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* 启动外部 8MHz HSE，并等待它稳定。 */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /* 清掉会影响系统时钟来源、总线分频和 PLL 倍频的字段。 */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    /* HSE x9 得到 72MHz；APB1 必须分到 36MHz，避免超过芯片限制。 */
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR |= RCC_CFGR_PLLMULL9;

    /* 打开 PLL，等待锁定后再把系统时钟切过去。 */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void pc13_led_init(void)
{
    /* PC13 属于 GPIOC，使用前先打开 GPIOC 时钟。 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /* 先清 PC13 的 4 个配置位，再设为 2MHz 通用推挽输出。 */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /* BluePill 常见板载 LED 低电平点亮；先输出高电平表示熄灭。 */
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

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();

    while (1) {
        pc13_toggle();
        delay_cycles(3600000U);
    }
}
