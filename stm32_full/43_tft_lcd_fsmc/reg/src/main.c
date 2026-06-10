#include "stm32f1xx.h"

/*
 * 寄存器版：TFT LCD 与 FSMC 教学模拟。
 *
 * 当前代码没有真实 LCD，也没有配置 FSMC。
 * g_framebuffer 用来模拟 LCD GRAM，lcd_fill() 模拟整屏填充。
 */

#define LCD_W 32U
#define LCD_H 24U

static uint16_t g_framebuffer[LCD_W * LCD_H];

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

static void lcd_fill(uint16_t color)
{
    for (uint32_t pixel = 0U; pixel < (LCD_W * LCD_H); pixel++) {
        g_framebuffer[pixel] = color;
    }
}

int main(void)
{
    system_clock_72mhz_init();
    pc13_led_init();
    lcd_fill(0xF800U);

    while (1) {
        uint16_t next_color;

        if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
            next_color = 0x07E0U;
        } else {
            next_color = 0x001FU;
        }

        lcd_fill(next_color);
        pc13_toggle();
        delay_cycles(3600000U);
    }
}
