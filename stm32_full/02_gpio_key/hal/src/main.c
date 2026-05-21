#include "stm32f1xx_hal.h"

/*
 * 本文件是“HAL版 按键输入控制 LED”。
 *
 * 实现目标：
 * - PA0 作为按键输入
 * - PC13 作为 LED 输出
 * - 按下按键时点亮 LED，松开按键时熄灭 LED
 *
 * 硬件接法：
 * - PA0 接按键一端
 * - 按键另一端接 GND
 * - PA0 启用内部上拉
 *
 * 因此：
 * - 按键松开时，PA0 为高电平
 * - 按键按下时，PA0 为低电平
 */

static void led_pc13_init(void);
static void key_pa0_init(void);

int main(void)
{
    /*
     * 先初始化 HAL 基础运行环境。
     * 后续所有 HAL API 都是在这个基础上使用的。
     */
    HAL_Init();

    led_pc13_init();
    key_pa0_init();

    while (1) {
        /*
         * HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) 的作用：
         * 读取 PA0 当前的输入电平。
         *
         * 返回 GPIO_PIN_RESET 表示低电平。
         * 在本课接线里，低电平意味着按键按下。
         */
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
            /*
             * 板载 LED 常为低电平点亮，
             * 所以输出低电平让 LED 亮。
             */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        } else {
            /*
             * 按键松开时，PA0 为高电平。
             * 让 PC13 输出高电平，LED 熄灭。
             */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
    }
}

static void led_pc13_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 打开 GPIOC 时钟。
     * 这是对 PC13 进行配置和控制的前提条件。
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 配置 PC13 为普通推挽输出。
     */
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 初始输出高电平，让 LED 熄灭。
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

static void key_pa0_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 打开 GPIOA 时钟。
     * 因为 PA0 属于 GPIOA。
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * 配置 PA0 为输入模式，并启用内部上拉。
     *
     * GPIO_MODE_INPUT 表示这是一个普通输入引脚。
     * GPIO_PULLUP 表示给它一个默认高电平，避免引脚悬空。
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}
