# 第 24 课：低功耗基础

## 1. 本课到底在学什么

本课表面上是“按键唤醒 LED”，真正要学的是 CPU 休眠和中断唤醒的链路。

STM32 的低功耗不是简单 delay，而是让 CPU 停止执行普通指令，等待中断事件到来。本课先学最基础、最安全的 `Sleep` 模式。

## 2. 学习目标

- 理解 Sleep 模式和普通死循环的区别
- 理解 `WFI` 是什么
- 理解为什么 EXTI 中断可以唤醒 CPU
- 知道 `SLEEPDEEP` 位决定 Sleep 还是更深的 Stop/Standby
- 能对应 HAL 的 `HAL_PWR_EnterSLEEPMode()` 和底层 `__WFI()`

## 3. 目录结构

```text
24_low_power_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- STM32F103C8T6 BluePill
- PC13 板载 LED
- PA0 外接按键到 GND，程序使用内部上拉

## 5. 先建立脑图

```text
初始化 LED 和 PA0 EXTI
  -> 主循环清唤醒标志
  -> 清 SLEEPDEEP，选择 Sleep 模式
  -> 执行 WFI，CPU 停止执行
  -> 按下 PA0
  -> EXTI0 中断到来
  -> CPU 自动唤醒并进入 EXTI0_IRQHandler
  -> 中断返回后主循环继续执行
```

## 6. 核心名词解释

### 6.1 Sleep 模式

Sleep 是最浅的低功耗模式。CPU 内核暂停执行，但大多数外设时钟仍然可以继续工作，所以唤醒快、风险低，适合作为低功耗入门。

### 6.2 `WFI`

`WFI` 是 Wait For Interrupt，等待中断指令。执行后 CPU 进入等待状态，直到某个已使能中断到来。

### 6.3 `SCB->SCR.SLEEPDEEP`

`SCR` 是 System Control Register。`SLEEPDEEP=0` 表示 `WFI` 进入 Sleep；`SLEEPDEEP=1` 时可能进入 Stop 或 Standby。因为本课只讲 Sleep，所以明确清零。

### 6.4 EXTI 唤醒

EXTI0 已经在 NVIC 中使能。PA0 下降沿触发时，EXTI0 会产生中断请求。这个中断既负责执行用户逻辑，也负责把 CPU 从 WFI 等待中唤醒。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/24_low_power_basic/reg/src/main.c)。

关键步骤：

1. 初始化 PC13 输出
2. 配置 PA0 为上拉输入
3. 用 AFIO 把 EXTI0 映射到 PA0
4. 配置 EXTI0 下降沿触发并在 NVIC 使能
5. `SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk` 选择 Sleep
6. 执行 `__WFI()` 进入等待中断状态
7. `EXTI0_IRQHandler` 清 `PR0` 并翻转 LED

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/24_low_power_basic/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `GPIO_MODE_IT_FALLING` | 配置 EXTI 下降沿 |
| `HAL_NVIC_EnableIRQ()` | 允许 NVIC 接收 EXTI0 中断 |
| `HAL_PWR_EnterSLEEPMode()` | 选择 Sleep 并执行 WFI |
| `HAL_GPIO_EXTI_IRQHandler()` | 清 EXTI 挂起标志并调用回调 |
| `HAL_GPIO_EXTI_Callback()` | 用户处理唤醒后的动作 |

## 9. 两种写法对比

寄存器版直接体现 `SLEEPDEEP + WFI + EXTI` 的关系；HAL 版把进入 Sleep 的细节收进 `HAL_PWR_EnterSLEEPMode()`。但唤醒条件没有变：必须有一个已使能中断到来。

## 10. 运行现象

- 上电后 LED 快闪 2 次，表示初始化完成
- 程序进入 Sleep
- 每按一次 PA0，CPU 被 EXTI0 唤醒，LED 状态变化，并再次进入 Sleep

## 11. 常见问题排查

- 按键不能唤醒：检查 EXTI0 是否配置、NVIC 是否使能
- 一直不睡眠：检查是否有频繁中断，例如 SysTick
- 唤醒后 LED 不变：检查 PC13 低电平点亮逻辑
- 想测电流但降不下来：BluePill 板载电源灯、调试器、外设时钟都会影响电流

## 12. 本课总结

低功耗的最小链路是：配置唤醒源，执行 WFI，等待中断唤醒。Sleep 是最适合入门的模式，因为它不会像 Stop/Standby 那样引入大量时钟恢复问题。

## 13. 扩展练习

- 把唤醒源从 PA0 改成 TIM2 中断
- 禁用 SysTick 后观察 Sleep 停留时间
- 查手册比较 Sleep、Stop、Standby 的区别

## 14. 下一课预告

下一阶段进入 FreeRTOS。你会看到“主循环”被多个任务替代，延时、通信和同步都会换一种组织方式。
