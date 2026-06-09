# STM32 学习问题记录

这份文档专门记录学习过程中真实遇到的问题。

它不是按课程知识点写的，而是按“现象 -> 原因 -> 定位 -> 修复 -> 记住什么”来整理。

以后遇到新的问题，可以继续按这个格式追加。

## 问题 1：HAL 版 PWM 呼吸灯不工作，但寄存器版正常

### 1. 发生在哪

- 课程：`09_pwm_advanced`
- 工程：`hal/`
- 对照工程：`reg/`
- 芯片/开发板：STM32F103C8T6 Blue Pill
- PWM 输出：`TIM2_CH1 -> PA0`

### 2. 现象

寄存器版可以正常输出 PWM，LED 能呼吸变化。

HAL 版烧录后看起来没有效果：

- LED 不呼吸
- `__HAL_TIM_SET_COMPARE()` 好像没有起作用
- 容易误以为是 `CCR` 动态更新失败
- 也容易怀疑是 `GPIO_MODE_AF_PP`、`AFIO` 或 `TIM2_CH1` 配错

### 3. 最终原因

HAL 版使用了：

```c
HAL_Delay(25);
```

但当前最小 PlatformIO + STM32Cube 工程里，`main.c` 原本没有实现：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

`HAL_Delay()` 依赖 HAL 的毫秒 tick。

如果 `SysTick_Handler()` 没有调用 `HAL_IncTick()`，HAL tick 不会递增，`HAL_Delay()` 就无法正常结束。

于是程序第一次进入主循环时：

```c
duty = 0;
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
HAL_Delay(25);
```

执行结果就是：

- 先把 `CCR1` 设置成 `0`
- PWM 占空比变成 `0%`
- 然后卡在 `HAL_Delay(25)`
- 后面的 `duty = next_duty(...)` 永远执行不到

所以看起来像是 PWM 不工作，其实是程序卡在延时里了。

### 4. 为什么寄存器版没问题

寄存器版没有用 `HAL_Delay()`。

它使用的是简单忙等延时：

```c
static void delay(volatile uint32_t count)
{
    while (count--) {
        __NOP();
    }
}
```

这种延时不依赖 SysTick，也不依赖 HAL tick。

所以即使没有 `SysTick_Handler()`，寄存器版仍然可以继续往下执行，`TIM2->CCR1 = duty;` 会不断更新。

### 5. 正确修复

在 HAL 版 `main.c` 中补上：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

这样 `HAL_Delay()` 才能正常返回，主循环才能持续更新 `duty`，进而持续更新 `CCR1`。

### 6. 额外修复：F1 HAL 最小工程建议补基础 MSP 初始化

本次排查时也补了一个 F1 HAL 最小工程常用初始化：

```c
static void hal_msp_init_minimal(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}
```

并在 `HAL_Init()` 后调用：

```c
HAL_Init();
hal_msp_init_minimal();
```

这不是这次“卡住不呼吸”的直接原因，但对 STM32F1 的最小 HAL 工程是个好习惯：

- `AFIO` 和复用功能、中断映射、重映射相关
- `PWR` 后续低功耗、备份域等会用到
- `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 关闭 JTAG，保留 SWD，避免 JTAG 占用部分 GPIO

### 7. 本次排查过程

一开始先对比 HAL 版和寄存器版：

- `GPIOA` 时钟有没有开
- `PA0` 是否配置为复用推挽输出
- `TIM2` 时钟有没有开
- `PSC / ARR / CCR1` 是否一致
- `HAL_TIM_PWM_Start()` 是否调用
- `__HAL_TIM_SET_COMPARE()` 是否确实在主循环里执行

这些方向都合理，因为 PWM 不输出常见原因确实在这里。

但关键线索是：

- HAL 版用了 `HAL_Delay()`
- 寄存器版没用 `HAL_Delay()`
- 上一课 `08_pwm_basic/hal` 里有 `SysTick_Handler()`
- 当前 `09_pwm_advanced/hal` 里没有 `SysTick_Handler()`

所以真正的问题不是“PWM 配错”，而是“主循环没有继续跑”。

### 8. 以后遇到类似问题怎么快速判断

如果 HAL 工程出现：

- 程序像是只执行了一次
- LED 常亮或常灭
- `HAL_Delay()` 后面的代码没反应
- PWM 初始化没报错，但动态变化不发生
- 寄存器版正常，HAL 版卡住

优先检查：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

还可以临时把 `HAL_Delay()` 换成忙等延时验证：

```c
for (volatile uint32_t i = 0; i < 120000; i++) {
    __NOP();
}
```

如果换成忙等后程序开始动，就说明大概率是 HAL tick / SysTick 链路的问题。

### 9. 这次真正应该记住的东西

`__HAL_TIM_SET_COMPARE()` 没问题。

它确实是在改 `CCR1`。

但前提是代码必须能反复执行到它。

这次 HAL 版的问题链路是：

```text
缺 SysTick_Handler
-> HAL_IncTick() 没被调用
-> HAL tick 不递增
-> HAL_Delay() 卡住
-> duty 不再更新
-> CCR1 一直是 0
-> LED 不呼吸
```

所以以后看 HAL 动态效果不动时，不要只盯着外设寄存器，也要看主循环是不是被 HAL 的时间基准卡住了。

