#include "stm32f1xx.h"

/*
 * 本文件是“寄存器版 按键输入控制 LED”。
 *
 * 目标：
 * - 使用 PA0 读取一个外接按键状态
 * - 使用 PC13 控制 BluePill 板载 LED
 * - 按下按键时点亮 LED，松开按键时熄灭 LED
 *
 * 硬件假设：
 * - PA0 接按键一端
 * - 按键另一端接 GND
 * - PA0 使用内部上拉
 *
 * 所以逻辑是：
 * - PA0 = 1 -> 按键松开
 * - PA0 = 0 -> 按键按下
 */

static void led_pc13_init(void)
{
    /*
     * 先打开 GPIOC 时钟。
     * 因为 PC13 属于 GPIOC，如果 GPIOC 没有时钟，后面的寄存器配置不会正常工作。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * PC13 属于 8~15 号引脚，所以要配置在 CRH 中。
     *
     * 目标模式：
     * - MODE13 = 10 -> 输出模式，最大速度 2MHz
     * - CNF13  = 00 -> 通用推挽输出
     */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /*
     * BluePill 常见板载 LED 为低电平点亮。
     * 先输出高电平，让 LED 初始为熄灭状态。
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

static void key_pa0_init(void)
{
    /*
     * 打开 GPIOA 时钟。
     * 因为按键输入接在 PA0，PA0 属于 GPIOA。
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /*
     * PA0 属于 0~7 号引脚，所以要配置在 CRL 中。
     *
     * 对于 STM32F103 的“输入上拉/下拉”模式：
     * - MODE0 = 00 -> 输入模式
     * - CNF0  = 10 -> 上拉/下拉输入
     *
     * 关键点：
     * 这里只是在 CRL 里把“输入类型”设置成“上拉/下拉输入”这一大类。
     * 但“到底是上拉还是下拉”，这一步还没决定。
     *
     * 也就是说，这两句代码做完以后，PA0 只是进入了：
     * - 输入模式
     * - 并且允许使用内部上拉/下拉
     *
     * 但具体是上拉还是下拉，要由后面的 ODR0 决定。
     *
     * PA0 对应的 4 bit 最终目标是：
     * - CNF0[1:0]  = 10
     * - MODE0[1:0] = 00
     *
     * 如果按 bit3..bit0 写出来，就是 1000。
     */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);

    /*
     * GPIO_CRL_CNF0_1 表示把 CNF0 这两位中的高位置 1、低位保持 0，
     * 也就是把 CNF0 配成二进制 10。
     *
     * 因为前面已经把 MODE0 和 CNF0 全清零了：
     * - MODE0 现在还是 00
     * - 这里只再把 CNF0 改成 10
     *
     * 所以最终得到：
     * - MODE0 = 00
     * - CNF0  = 10
     *
     * 这就进入了“输入上拉/下拉模式”。
     */
    GPIOA->CRL |= GPIO_CRL_CNF0_1;

    /*
     * 这一步是 F103 的关键特性：
     * 当引脚已经配置成“输入上拉/下拉”模式后，
     * ODR 对应位决定到底是上拉还是下拉。
     *
     * 规则：
     * - ODR0 = 1 -> 上拉
     * - ODR0 = 0 -> 下拉
     *
     * 注意：
     * 这里代码虽然写的是 BSRR，不是直接写 ODR，
     * 但它的效果就是“把 ODR 的第 0 位置 1”。
     *
     * 原因是：
     * - BSRR 是 GPIO 的“置位/复位寄存器”
     * - 往 BSRR 的 BS0 位写 1
     * - 等价于把 ODR0 置为 1
     *
     * 也就是说：
     * GPIOA->BSRR = GPIO_BSRR_BS0;
     *
     * 本质效果等价于：
     * GPIOA->ODR |= GPIO_ODR_ODR0;
     *
     * 只是 BSRR 这种写法更常见，也更安全，
     * 因为它不需要“先读 ODR，再改 bit0，再写回去”。
     *
     * 本课要的是“内部上拉”，所以这里把 ODR0 置 1。
     */
    GPIOA->BSRR = GPIO_BSRR_BS0;
}

static uint8_t key_is_pressed(void)
{
    /*
     * GPIOA->IDR 是输入数据寄存器。
     * 读取 PA0 当前输入电平时，本质就是看 IDR 的第 0 位。
     *
     * GPIO_IDR_IDR0 是掩码宏，表示 IDR 中的 bit0。
     *
     * 如果 bit0 为 0，说明 PA0 当前为低电平。
     * 在本课接线方式下，低电平表示按键按下。
     */
    if ((GPIOA->IDR & GPIO_IDR_IDR0) == 0U) {
        return 1U;
    }

    return 0U;
}

int main(void)
{
    led_pc13_init();
    key_pa0_init();

    while (1) {
        /*
         * 如果按键按下：
         * - PA0 被拉低
         * - key_is_pressed() 返回 1
         * - 将 PC13 拉低，点亮 LED
         */
        if (key_is_pressed()) {
            GPIOC->BRR = GPIO_BRR_BR13;
        } else {
            /*
             * 如果按键松开：
             * - PA0 由于内部上拉保持高电平
             * - key_is_pressed() 返回 0
             * - 将 PC13 拉高，熄灭 LED
             */
            GPIOC->BSRR = GPIO_BSRR_BS13;
        }
    }
}
