# 第 39 课：低功耗 Sleep

## 1. 本课到底在学什么

本课表面现象是：程序启动后 PC13 LED 快闪 2 次，然后 CPU 进入 Sleep 模式。每次按下 PA0，EXTI0 中断唤醒 CPU，LED 翻转并再闪一次，然后程序再次进入 Sleep。

真正要学的是 STM32 最浅层低功耗模式的进入和唤醒链路：

```text
配置一个可唤醒 CPU 的中断源
  -> 清 SCB->SCR.SLEEPDEEP，选择 Sleep 而不是 Stop/Standby
  -> 执行 WFI
  -> CPU 内核停止执行，等待中断
  -> PA0 下降沿触发 EXTI0
  -> NVIC 响应中断，CPU 自动唤醒
  -> ISR 清标志并设置唤醒标记
  -> 返回 WFI 后继续执行主循环
```

本课只讲 Sleep，不讲 Stop 和 Standby。Sleep 最安全：CPU 暂停，外设时钟仍保持，唤醒最快，时钟配置不会丢。

## 2. 本课学习目标

学完本课，你应该能回答：

1. Sleep、Stop、Standby 三种低功耗模式的差别是什么？
2. `WFI` 是什么，属于哪个层级？
3. `SCB->SCR.SLEEPDEEP` 为什么要清零？
4. 为什么必须先配置并使能 EXTI0 中断才能唤醒？
5. PA0 下降沿如何从 GPIO 事件变成 CPU 唤醒？
6. `g_wakeup` 在本课中起什么作用？
7. HAL 版 `HAL_PWR_EnterSLEEPMode()` 对应寄存器版哪些动作？
8. SysTick 为什么会影响低功耗效果？

## 3. 本课目录结构

```text
39_low_power_sleep/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 EXTI0、NVIC、`SCB->SCR` 并执行 `__WFI()`。  
`hal/` 使用 HAL GPIO EXTI 和 `HAL_PWR_EnterSLEEPMode()`。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- LED：PC13，常见 BluePill 为低电平点亮
- 按键：PA0 接按键，按下接 GND，内部上拉

本课最好配合电流表观察 Sleep 前后的电流变化。不过即使没有电流表，也能通过按键唤醒和 LED 现象验证链路。

## 5. 先建立一个最基本的脑图

```text
初始化
  -> PC13 输出
  -> PA0 上拉输入
  -> AFIO 映射 PA0 到 EXTI0
  -> EXTI0 下降沿触发
  -> NVIC 使能 EXTI0_IRQn
  -> LED 快闪 2 次表示准备完成

进入 Sleep
  -> g_wakeup=0
  -> SCB->SCR.SLEEPDEEP=0
  -> __WFI()
  -> CPU 停止执行，等待中断

唤醒
  -> PA0 按下，EXTI0 触发
  -> CPU 唤醒并执行 EXTI0_IRQHandler
  -> 清 EXTI->PR.PR0
  -> g_wakeup=1
  -> LED 翻转
  -> 回到 WFI 后面的代码
  -> 主循环看到 g_wakeup=1，再闪一次
  -> 再次进入 Sleep
```

Sleep 不是复位，也不是重新启动。它只是让 CPU 暂停执行，唤醒后继续从 `WFI` 后面往下跑。

## 6. 先认识本课里出现的核心名词

### 6.1 `低功耗模式` 是什么

低功耗模式是 MCU 通过关闭或暂停部分电路来降低电流的工作状态。

STM32F1 常见层级：

- Sleep：CPU 停，外设继续
- Stop：大部分时钟停，SRAM 和寄存器保持
- Standby：最深，SRAM 通常不保持，唤醒类似复位

本课只用 Sleep，因为它最容易观察且风险最低。

### 6.2 `Sleep` 是什么

Sleep 是最浅的低功耗模式。

它属于 Cortex-M 内核低功耗机制和 STM32 电源管理之间的基础模式。进入 Sleep 后 CPU 不再执行普通指令，但外设、中断控制器和已运行时钟仍可工作。

如果没有可用中断，CPU 可能一直停在 Sleep，程序看起来“不动”。

### 6.3 `WFI` 是什么

`WFI` 是 Wait For Interrupt，等待中断指令。

它是 Cortex-M3 内核指令，不是 STM32 外设寄存器。执行后 CPU 进入等待状态，直到一个已使能中断到来。

本课寄存器版通过 `__WFI()` 调用这条指令。

### 6.4 `SCB->SCR` 是什么

`SCR` 是 System Control Register，系统控制寄存器，属于 Cortex-M3 内核的 SCB。

它控制 Sleep 行为中的一些核心选项。本课只关注 `SLEEPDEEP`。

### 6.5 `SLEEPDEEP` 是什么

`SLEEPDEEP` 决定 `WFI` 进入浅睡眠还是深睡眠。

- `SLEEPDEEP=0`：进入 Sleep
- `SLEEPDEEP=1`：进入 Stop 或 Standby，具体还要看 PWR 配置

本课明确清零它，避免误进更深模式。

### 6.6 `EXTI0` 是什么

EXTI0 是外部中断线 0。

本课用 PA0 的下降沿触发 EXTI0。EXTI0 既是按键中断源，也是唤醒 Sleep 的事件源。

### 6.7 `AFIO->EXTICR` 是什么

`EXTICR` 选择 EXTI 线来自哪个 GPIO 端口。

本课把 EXTI0 绑定到 PA0。如果映射错，按 PA0 不会触发 EXTI0，也就不能唤醒。

### 6.8 `NVIC` 是什么

NVIC 是 Cortex-M3 的中断控制器。

WFI 等待的是“已使能并能被响应的中断”。所以只配置 EXTI 边沿还不够，必须 `NVIC_EnableIRQ(EXTI0_IRQn)`。

### 6.9 `EXTI->PR` 是什么

`PR` 是 EXTI 挂起标志寄存器。

PA0 下降沿触发后 `PR0=1`。ISR 中写 1 清除它，否则中断可能反复进入。

### 6.10 `g_wakeup` 是什么

`g_wakeup` 是软件标志变量，ISR 里置 1，主循环用它判断本次从 Sleep 返回是否由 PA0 唤醒。

它属于 C 软件层。没有它，主循环仍会继续执行，但不容易区分是否发生了预期唤醒。

### 6.11 `HAL_PWR_EnterSLEEPMode()` 是什么

HAL 版用它进入 Sleep。

参数：

- `PWR_MAINREGULATOR_ON`：主调节器保持开启
- `PWR_SLEEPENTRY_WFI`：用 WFI 进入 Sleep

底层对应清 `SLEEPDEEP` 并执行 WFI。

### 6.12 `HAL_GPIO_EXTI_Callback()` 是什么

这是 HAL EXTI 中断处理后调用的用户回调。

PA0 唤醒 CPU 后，硬件进入 `EXTI0_IRQHandler()`，HAL 清除 EXTI 标志，再调用这个回调设置 `g_wakeup` 和翻转 LED。

### 6.13 `SysTick` 是什么

SysTick 是 Cortex-M 内核定时器，HAL 默认每 1ms 产生一次中断。

如果 SysTick 持续开启，CPU 可能每 1ms 被唤醒一次，导致 Sleep 省电效果变差。本课重点演示 EXTI 唤醒，没有专门暂停 SysTick。

## 7. 寄存器版代码逐步讲解

### 7.1 LED 初始化

PC13 配成推挽输出。启动后 `led_blink(2)` 表示系统初始化完成。

### 7.2 PA0 上拉输入

PA0 配成上拉输入，按下接 GND。未按下高电平，按下低电平，所以选择下降沿触发。

### 7.3 AFIO 映射

`AFIO->EXTICR[0]` 清 EXTI0 字段，选择 PA0 作为 EXTI0 来源。

### 7.4 EXTI 配置

```c
EXTI->IMR |= EXTI_IMR_MR0;
EXTI->FTSR |= EXTI_FTSR_TR0;
EXTI->PR = EXTI_PR_PR0;
```

允许 EXTI0 中断请求，选择下降沿，清除旧挂起标志。

### 7.5 NVIC 配置

`NVIC_SetPriority(EXTI0_IRQn, 1U)` 设置优先级，`NVIC_EnableIRQ(EXTI0_IRQn)` 允许 CPU 响应该中断。

这一步决定 EXTI0 能否唤醒 WFI。

### 7.6 `enter_sleep_mode()`

```c
SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
__WFI();
```

先确保 `SLEEPDEEP=0`，然后执行 WFI。CPU 从这里暂停，等待中断。

### 7.7 `EXTI0_IRQHandler()`

ISR 检查 `PR0`，写 1 清除，设置 `g_wakeup=1`，并翻转 LED。

这个中断既完成唤醒，也完成可见反馈。

### 7.8 主循环再次确认

WFI 返回后，主循环检查 `g_wakeup`。如果为 1，就再闪一次 LED，说明这次 Sleep 确实被按键唤醒。

### 7.9 为什么会再次进入 Sleep

主循环末尾回到开头，把 `g_wakeup` 清零，再执行 WFI。这样每次按键唤醒后都会重新睡眠。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()`

初始化 HAL Tick 和基础运行环境。注意 SysTick 会周期性中断，真实低功耗项目常需要处理它。

### 8.2 `gpio_init()` 打开时钟

HAL 版打开 GPIOC、GPIOA 和 PWR 时钟。PWR 时钟用于 HAL 低功耗 API。

### 8.3 `GPIO_MODE_IT_FALLING`

PA0 配成下降沿外部中断模式，HAL 内部会配置 GPIO、AFIO/EXTI 和触发边沿。

### 8.4 `HAL_NVIC_SetPriority()` 和 `EnableIRQ()`

设置并使能 EXTI0 的 NVIC 响应。没有 NVIC 使能，WFI 不会被 PA0 中断唤醒。

### 8.5 `HAL_PWR_EnterSLEEPMode()`

```c
HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
```

进入 Sleep，主调节器保持开启，入口方式为 WFI。

### 8.6 HAL EXTI 中断入口

`EXTI0_IRQHandler()` 调用 `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)`。HAL 检查并清除 EXTI 挂起标志。

### 8.7 HAL 回调

`HAL_GPIO_EXTI_Callback()` 判断 `GPIO_Pin==GPIO_PIN_0`，设置 `g_wakeup=1` 并翻转 LED。

### 8.8 `HAL_Delay()` 与 Sleep

本课 LED 闪烁使用 `HAL_Delay()`。它依赖 SysTick。低功耗项目中，如果想减少 SysTick 唤醒，需要在 Sleep 前后暂停/恢复 SysTick。

## 9. 两个版本真正应该怎么学

寄存器版重点看：

```text
EXTI/NVIC 唤醒源 -> SLEEPDEEP=0 -> WFI -> ISR -> WFI 后继续
```

HAL 版重点看：

```text
GPIO_MODE_IT_FALLING -> HAL_NVIC -> HAL_PWR_EnterSLEEPMode -> Callback
```

Sleep 的本质是 CPU 等中断，不是程序退出，也不是外设全部停止。

## 10. 检验问题清单

### 10.1 Sleep 模式会丢失 SRAM 吗？

**答**：不会。Sleep 只是停止 CPU 执行，SRAM 和外设状态保持。

### 10.2 `WFI` 等待的是什么？

**答**：等待一个已使能、能被 NVIC 响应的中断。中断到来后 CPU 自动唤醒。

### 10.3 为什么要清 `SLEEPDEEP`？

**答**：确保 WFI 进入 Sleep，而不是 Stop 或 Standby 这类更深低功耗模式。

### 10.4 PA0 如何唤醒 CPU？

**答**：PA0 下降沿触发 EXTI0，EXTI0 请求进入 NVIC，NVIC 响应中断，WFI 状态结束，CPU 执行 ISR。

### 10.5 `g_wakeup` 为什么要 volatile？

**答**：它在中断里修改、主循环里读取。`volatile` 防止编译器把主循环读取优化成固定值。

### 10.6 HAL 版低功耗入口对应寄存器版什么？

**答**：对应清 `SCB->SCR.SLEEPDEEP` 并执行 WFI 指令。

### 10.7 SysTick 为什么会影响 Sleep？

**答**：SysTick 每 1ms 中断一次，会频繁唤醒 CPU，使 Sleep 省电效果变差。

### 10.8 为什么本课不讲 Stop/Standby？

**答**：Stop/Standby 涉及时钟恢复、电压调节器和唤醒复位等更多内容。本课先掌握最安全的 Sleep。

## 11. 工程实现步骤

### 11.1 需求分析

本课要证明 CPU 能进入 Sleep，并能由外部按键中断唤醒。因此必须先配置一个可靠的 EXTI0 唤醒源。

### 11.2 硬件核查

确认 PA0 按键按下接 GND，内部上拉有效。确认 PC13 LED 正常。

### 11.3 寄存器路线

配置 PC13、PA0、AFIO、EXTI0、NVIC，然后在主循环清 `SLEEPDEEP` 并执行 `__WFI()`。

### 11.4 HAL 路线

用 HAL GPIO 配 PA0 为下降沿中断，用 HAL NVIC 使能 EXTI0，用 `HAL_PWR_EnterSLEEPMode()` 进入 Sleep。

### 11.5 工程思维

低功耗不是只调用一个 API。要先规划唤醒源、关闭无用外设、处理 SysTick，并明确唤醒后从哪里继续执行。

### 11.6 常见工程陷阱

没有使能 NVIC、EXTI 标志没清、误设 `SLEEPDEEP=1`、SysTick 频繁唤醒、忘记 volatile、按键抖动，都会让现象不符合预期。

## 12. 运行现象

上电后 LED 快闪 2 次，然后熄灭并进入 Sleep。每次按下 PA0，CPU 被唤醒，LED 在 ISR 中翻转，主循环再闪一次，然后重新进入 Sleep。

如果配电流表，Sleep 时电流应低于普通空循环，但由于本课没有关闭 SysTick 和其他外设，下降幅度有限。

## 13. 常见问题排查

### 13.1 按键不能唤醒

检查 PA0 输入、AFIO 映射、EXTI IMR/FTSR、NVIC 使能和 EXTI0_IRQHandler 是否存在。

### 13.2 程序像没睡一样

检查 SysTick 是否持续唤醒，或者是否有其他中断源一直触发。

### 13.3 进入后再也不醒

检查是否误进 Stop/Standby，`SLEEPDEEP` 是否被置 1，唤醒源是否可用。

### 13.4 唤醒后反复进入中断

检查 `EXTI->PR` 是否写 1 清除。HAL 版检查是否调用 `HAL_GPIO_EXTI_IRQHandler()`。

### 13.5 电流下降不明显

Sleep 只停 CPU，外设和 SysTick 仍可能运行。要更低功耗需要关闭外设或学习 Stop/Standby。

## 14. 本课最核心的结论

1. Sleep 是最浅低功耗模式，CPU 停止执行，外设通常继续工作。
2. `WFI` 是 Cortex-M 等待中断指令。
3. `SLEEPDEEP=0` 确保进入 Sleep。
4. 唤醒 Sleep 需要已配置并使能的中断源。
5. EXTI0 可以作为 PA0 按键唤醒源。
6. HAL 低功耗 API 封装了 `SLEEPDEEP` 和 WFI，但唤醒源仍要自己配置。
7. SysTick 和其他中断会影响 Sleep 的实际省电效果。

## 15. 建议你现在怎么读这节课

先把“WFI 前必须有唤醒中断源”记牢，再对照 `enter_sleep_mode()` 和 `EXTI0_IRQHandler()`。HAL 版重点看 `HAL_PWR_EnterSLEEPMode()` 和 EXTI Callback 的调用链。

## 16. 扩展练习

1. 在进入 Sleep 前暂停 SysTick，唤醒后恢复，观察电流变化。
2. 增加另一个 EXTI 唤醒源。
3. 记录每次唤醒次数到 BKP 寄存器。
4. 对比 Sleep 和普通 `while(1)` 空转的电流。

## 17. 下一课预告

- 上一课：[38_flash_internal](../38_flash_internal/README.md)
- 下一课：[40_low_power_stop_standby](../40_low_power_stop_standby/README.md)
