#include "stm32f1xx.h"

/*
 * 这个文件是“寄存器版 LED 点灯”。
 *
 * 目标不是为了写出最短代码，而是为了把下面这条链路看清楚：
 * 1. 先开 GPIOC 时钟
 * 2. 再把 PC13 配成输出模式
 * 3. 再往输出寄存器写高低电平
 * 4. 最终让板载 LED 亮灭
 *
 * 注意：
 * BluePill 上常见的板载 LED 接在 PC13，而且通常是“低电平点亮”。
 * 也就是说：
 * - PC13 输出 0 -> LED 亮
 * - PC13 输出 1 -> LED 灭
 */

static void delay(volatile uint32_t count)
{
    /*
     * 这是一个最原始的空转延时。
     *
     * 它的作用只有一个：
     * 让 LED 的亮灭速度慢下来，肉眼能看见。
     *
     * 为什么这里不用更正规的毫秒延时？
     * 因为这节课的重点是 GPIO，不是 SysTick 或定时器。
     * 后面我们会专门讲更准确的延时实现。
     *
     * 参数 count 越大，循环次数越多，延时越长。
     * volatile 的作用是告诉编译器：这个变量不要随便优化掉。
     */
    while (count--) {
        /*
         * __NOP() 是“空操作”指令，意思是什么也不做，只占用一个很短的执行时间。
         *
         * 放在这里的目的：
         * - 让循环体里确实有一条指令
         * - 避免某些优化场景下循环被过度简化
         */
        __NOP();
    }
}

static void gpio_pc13_init(void)
{
    /*
     * 第 1 步：打开 GPIOC 外设时钟。
     *
     * RCC 是“复位与时钟控制”模块。
     * RCC->APB2ENR 是 APB2 总线外设时钟使能寄存器。
     *
     * GPIOC 挂在 APB2 上，所以如果你想用 GPIOC，
     * 就必须先把 APB2ENR 里的 GPIOC 时钟使能位置 1。
     *
     * 这句代码的含义：
     * - 读取 RCC->APB2ENR 当前值
     * - 把 IOPCEN 这一位 OR 成 1
     * - 再写回去
     *
     * IOPCEN = IO Port C Enable
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /*
     * 第 2 步：配置 PC13 为“通用推挽输出”。
     *
     * 在 STM32F103 中：
     * - CRL 配置引脚 0~7
     * - CRH 配置引脚 8~15
     *
     * 因为 PC13 属于 8~15，所以它要在 GPIOC->CRH 中配置。
     *
     * 每个引脚占 4 个 bit：
     * - MODE[1:0]：决定输入/输出以及输出速度
     * - CNF[1:0] ：决定输入/输出类型
     *
     * 我们要的目标配置是：
     * - MODE13 = 10 -> 输出模式，最大速度 2MHz
     * - CNF13  = 00 -> 通用推挽输出
     *
     * 所以整体 4 bit 目标值相当于 0010。
     */

    /*
     * 先清掉 PC13 对应的 4 个配置位。
     *
     * 为什么要先清零？
     * 因为寄存器里原来可能有别的值，直接 OR 可能会残留旧配置。
     *
     * GPIO_CRH_MODE13 表示 PC13 的 MODE 那两位
     * GPIO_CRH_CNF13  表示 PC13 的 CNF 那两位
     *
     * 用按位取反再与运算，效果就是把这 4 位清零，其他位保持不变。
     */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);

    /*
     * 再把 MODE13 配成 10。
     *
     * GPIO_CRH_MODE13_1 表示 MODE13 的高位为 1、低位为 0，
     * 也就是二进制 10。
     *
     * 因为前面已经把 CNF13 清成 00，所以现在组合结果就是：
     * MODE13 = 10
     * CNF13  = 00
     *
     * 最终得到“普通推挽输出，2MHz”模式。
     */
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /*
     * 第 3 步：先让 PC13 输出高电平。
     *
     * GPIOC->BSRR 是“置位/复位寄存器”中的置位部分写法。
     * 往 BS13 这个位写 1，表示把 PC13 输出置高。
     *
     * 因为 BluePill 常见板子的 LED 是低电平点亮，
     * 所以这里先输出高电平，相当于“先让 LED 熄灭”。
     */
    GPIOC->BSRR = GPIO_BSRR_BS13;
}

int main(void)
{
    /*
     * 先完成 GPIO 初始化。
     * 如果不初始化，后面直接写电平通常没有意义。
     */
    gpio_pc13_init();

    while (1) {
        /*
         * GPIOC->BRR 是“位复位寄存器”。
         * 往 BR13 写 1，表示把 PC13 拉低。
         *
         * 对于 BluePill 常见板载 LED：
         * PC13 = 0 -> LED 亮
         */
        GPIOC->BRR = GPIO_BRR_BR13;

        /* 等一会儿，让“亮”的状态维持一段时间。 */
        delay(600000U);

        /*
         * 再把 PC13 拉高。
         *
         * GPIO_BSRR_BS13 表示“把 13 号引脚置高”。
         * 对于这块板子来说：
         * PC13 = 1 -> LED 灭
         */
        GPIOC->BSRR = GPIO_BSRR_BS13;

        /* 再等一会儿，让“灭”的状态维持一段时间。 */
        delay(600000U);
    }
}
