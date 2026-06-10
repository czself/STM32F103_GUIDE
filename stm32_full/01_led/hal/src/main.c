#include "stm32f1xx_hal.h"

/*
 * 这个文件是“HAL版 LED 点灯”。
 *
 * 它和寄存器版完成的是同一件事：
 * - 让 BluePill 板载 LED 周期性闪烁
 *
 * 但写法不同：
 * - 寄存器版：你自己直接操作 RCC、GPIO 寄存器
 * - HAL版：你通过 HAL 提供的 API 告诉库“我要怎么配”，HAL 帮你完成底层配置
 *
 * 这里仍然建议你一边看 HAL 代码，一边脑中对照寄存器版：
 * - 开时钟 -> 对应哪个 API
 * - 配模式 -> 对应哪个 API
 * - 写高低电平 -> 对应哪个 API
 */

static void gpio_pc13_init(void);

int main(void)
{
    /*
     * HAL_Init() 是 HAL 工程常见的入口初始化函数。
     *
     * 它主要做 HAL 运行环境的基础准备工作。
     * 对本课你先记住一点就够了：
     * - 后面要用到 HAL_Delay()
     * - HAL_Delay() 依赖 HAL 的基础初始化
     * 所以先调用 HAL_Init() 是标准动作
     */
    HAL_Init();

    /*
     * 初始化 GPIOC 的 PC13，把它配置成输出模式。
     */
    gpio_pc13_init();

    while (1) {
        /*
         * HAL_GPIO_WritePin() 的作用：
         * 控制某个 GPIO 引脚输出高电平还是低电平。
         *
         * 参数含义：
         * - 第 1 个参数 GPIOC：操作哪个 GPIO 端口
         * - 第 2 个参数 GPIO_PIN_13：操作哪个引脚
         * - 第 3 个参数 GPIO_PIN_RESET：输出低电平
         *
         * 因为 BluePill 常见板子的 LED 是低电平点亮，
         * 所以这句执行后 LED 会亮。
         */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

        /*
         * HAL_Delay(500) 表示阻塞延时约 500ms。
         * 这里的目的只是让亮灭节奏肉眼可见。
         */
        HAL_Delay(500);

        /*
         * GPIO_PIN_SET 表示输出高电平。
         * 对这块板子来说，高电平通常对应 LED 熄灭。
         */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

        /* 维持熄灭状态约 500ms。 */
        HAL_Delay(500);
    }
}

static void gpio_pc13_init(void)
{
    /*
     * GPIO_InitTypeDef 是 HAL 定义的 GPIO 初始化结构体。
     *
     * 你可以把它理解成一张“配置表”：
     * 我们先把想要的 GPIO 配置写进这张表，
     * 再交给 HAL_GPIO_Init() 去真正应用。
     *
     * = {0} 的意思是把整个结构体先清零，
     * 避免里面残留未初始化的随机值。
     */
    GPIO_InitTypeDef gpio = {0};

    /*
     * __HAL_RCC_GPIOC_CLK_ENABLE() 是 HAL 宏，用来打开 GPIOC 时钟。
     *
     * 它的底层本质，仍然是在改 RCC 的时钟使能寄存器。
     * 也就是说，HAL 并没有绕开底层，只是把“开时钟”这一步包装起来了。
     *
     * 如果你不先开时钟，后面的 GPIO 初始化通常不会正常生效。
     */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 指定要配置的是 13 号引脚。
     */
    gpio.Pin = GPIO_PIN_13;

    /*
     * GPIO_MODE_OUTPUT_PP：
     * - OUTPUT：输出模式
     * - PP：Push Pull，推挽输出
     *
     * 这相当于告诉 HAL：
     * “我要把 PC13 配成普通推挽输出”
     */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;

    /*
     * GPIO_NOPULL 表示不使用上拉/下拉。
     *
     * 对本课这个普通输出点灯场景来说，这不是最关键的参数，
     * 但 HAL 的统一接口会把它放在结构体里一起描述。
     */
    gpio.Pull = GPIO_NOPULL;

    /*
     * GPIO_SPEED_FREQ_LOW 表示选择较低的输出速度等级。
     *
     * 点灯不需要高速翻转，所以低速已经足够。
     */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /*
     * HAL_GPIO_Init(GPIOC, &gpio) 的意思是：
     * 把上面 gpio 结构体里描述的配置，应用到 GPIOC 这个端口上。
     *
     * 对于本课来说，它最终会把 PC13 配成输出模式。
     */
    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 初始化后先输出高电平，让 LED 先处于熄灭状态。
     *
     * 参数含义：
     * - GPIOC：操作 GPIOC 端口
     * - GPIO_PIN_13：操作 13 号引脚
     * - GPIO_PIN_SET：输出高电平
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}
void SysTick_Handler(void)
{
    /*
     * HAL_Init() 会配置 SysTick，HAL_Delay() 依赖这里的 tick 递增。
     *
     * 如果这个中断处理函数缺失，在一些“只有 main.c 的最小工程”里，
     * CPU 会掉进启动文件的默认中断死循环。
     *
     * 现象通常是：
     * - 程序刚启动时像是运行了一下
     * - 然后就卡住
     * - 如果点灯，可能表现成“常亮”或“常灭”
     */
    HAL_IncTick();
}
