# 第 3 课：时钟树与 72MHz 系统时钟

## 1. 本课到底在学什么

这节课表面上是在做：

- 把 STM32F103 的系统时钟配置到 72MHz
- 用 PC13 LED 闪烁观察程序是否稳定运行

真正学习的是 STM32 的时钟链路：

```text
HSE 8MHz
  -> PLL 输入选择 HSE
  -> PLL x9 得到 72MHz
  -> SYSCLK 选择 PLL
  -> AHB = 72MHz
  -> APB1 = 36MHz
  -> APB2 = 72MHz
  -> GPIO/USART/TIM/SysTick 等外设获得正确频率基准
```

前两课里你已经能控制 GPIO，但那些代码如果不先理解时钟，就只是在“刚好能跑”。从本课开始，你要知道：**延时准不准、串口波特率对不对、定时器频率怎么算，根都在时钟树**。

## 2. 本课学习目标

学完本课，你应该能回答：

1. `HSE`、`HSI`、`PLL`、`SYSCLK` 分别是什么？
2. 为什么 BluePill 常用 8MHz HSE 乘 9 得到 72MHz？
3. 为什么切到 72MHz 前要先设置 `FLASH->ACR`？
4. 为什么 APB1 要分频到 36MHz，而 APB2 可以保持 72MHz？
5. `RCC->CR` 和 `RCC->CFGR` 分别负责时钟链路里的哪一段？
6. 为什么必须等待 `HSERDY`、`PLLRDY` 和 `SWS`？
7. HAL 版的 `RCC_OscInitTypeDef` 和 `RCC_ClkInitTypeDef` 分别映射到哪些寄存器配置？
8. 如果时钟配错，后续串口、定时器、延时会出现什么问题？

## 3. 本课目录结构

```text
03_clock_tree/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接操作 `FLASH` 和 `RCC` 寄存器。  
`hal/` 使用 HAL 的时钟配置结构体和 API 完成同样的时钟切换。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 外部高速时钟：板载 8MHz HSE 晶振
- 观察现象：PC13 LED 以固定节奏闪烁

本课默认 HSE 可用。如果你的板子没有 8MHz 外部晶振，代码会卡在等待 `HSERDY` 的循环里。

## 5. 先建立一个最基本的脑图

```text
配置 Flash 等待周期
  -> 打开 HSE
  -> 等待 HSE 稳定
  -> 配置 AHB/APB 分频
  -> 配置 PLL 来源为 HSE，倍频 x9
  -> 打开 PLL
  -> 等待 PLL 锁定
  -> SYSCLK 切换到 PLL
  -> 等待 SWS 确认切换完成
  -> 初始化 PC13 并闪烁 LED
```

这条链路里最关键的是：

1. **先把时钟源稳定下来，再切换系统时钟。**
2. **先配置 Flash 等待周期，再让 CPU 跑到 72MHz。**

如果顺序乱了，程序可能卡住、跑飞，或者后续外设频率整体不对。

## 6. 先认识本课里出现的核心名词

### 6.1 `HSE` 是什么

`HSE` 全称是：
- High-Speed External oscillator

中文通常叫：
- 外部高速时钟

它的作用是：
- 给 MCU 提供比内部 RC 更稳定的时钟源。
- 常见 BluePill 使用 8MHz 外部晶振。

你可以先把它理解成：
- 板子上那颗给芯片提供稳定节拍的外部时钟。

在本课里，HSE 是 PLL 的输入。没有 HSE，就不能按本课路线得到精确的 72MHz。

### 6.2 `PLL` 是什么

`PLL` 全称是：
- Phase-Locked Loop

中文通常叫：
- 锁相环

它的作用是：
- 把输入时钟倍频成更高频率。
- 让 8MHz HSE 通过 x9 得到 72MHz。

你可以先把它理解成：
- 时钟倍频器。

在本课里，PLL 位于 HSE 和 SYSCLK 之间。PLL 来源或倍频配错，整个系统频率都会错，后面的定时器、串口波特率都会跟着错。

### 6.3 `SYSCLK` 是什么

`SYSCLK` 全称是：
- System Clock

中文通常叫：
- 系统时钟

它的作用是：
- 作为 CPU 和总线时钟树的核心来源。
- 可以选择来自 HSI、HSE 或 PLL。

你可以先把它理解成：
- 整个芯片运行节奏的总源头。

在本课里，最终要让 `SYSCLK = PLLCLK = 72MHz`。如果只打开 PLL 但不切换 SYSCLK，CPU 仍不会使用 PLL。

### 6.4 `FLASH->ACR` 是什么

`FLASH->ACR` 全称是：
- Flash Access Control Register

中文通常叫：
- Flash 访问控制寄存器

它的作用是：
- 配置 Flash 预取和等待周期。
- 保证高频运行时 CPU 从 Flash 取指可靠。

你可以先把它理解成：
- CPU 高速读程序前，给 Flash 设置“跟得上”的节奏。

在本课里，72MHz 下设置 `FLASH_ACR_LATENCY_2`。如果等待周期太少，程序可能运行不稳定，甚至时钟刚切过去就跑飞。

### 6.5 `RCC->CR` 是什么

`RCC->CR` 全称是：
- Clock Control Register

中文通常叫：
- RCC 时钟控制寄存器

它的作用是：
- 打开或关闭 HSE、PLL 等时钟源。
- 通过 ready 标志告诉软件时钟是否稳定。

你可以先把它理解成：
- 时钟源的开关和状态面板。

在本课里，`HSEON`、`HSERDY`、`PLLON`、`PLLRDY` 都在 `RCC->CR`。只开不等 ready，后续配置就可能基于未稳定时钟。

### 6.6 `RCC->CFGR` 是什么

`RCC->CFGR` 全称是：
- Clock Configuration Register

中文通常叫：
- RCC 时钟配置寄存器

它的作用是：
- 配置 AHB/APB 分频。
- 选择 PLL 输入来源和倍频。
- 选择 SYSCLK 来源。

你可以先把它理解成：
- 时钟树的路线图设置寄存器。

在本课里，它决定 HSE 是否送进 PLL、PLL 是否 x9、APB1 是否 /2、SYSCLK 是否切到 PLL。

### 6.7 `HSERDY` 是什么

`HSERDY` 全称是：
- HSE Ready flag

中文通常叫：
- HSE 稳定标志

它的作用是：
- 表示外部高速时钟已经稳定。
- 软件必须等它置位后，才适合继续使用 HSE。

你可以先把它理解成：
- HSE 对 CPU 说“我准备好了”。

在本课里，若外部晶振损坏或没有焊接，程序会卡在等待 `HSERDY` 的循环里。

### 6.8 `PLLRDY` 是什么

`PLLRDY` 全称是：
- PLL Ready flag

中文通常叫：
- PLL 锁定标志

它的作用是：
- 表示 PLL 输出已经稳定。
- 软件必须等它置位后，才能把 SYSCLK 切到 PLL。

你可以先把它理解成：
- PLL 倍频器锁定到目标频率后的完成信号。

在本课里，不等 `PLLRDY` 就切换系统时钟，会让 CPU 使用不稳定时钟。

### 6.9 `APB1/APB2` 是什么

`APB` 全称是：
- Advanced Peripheral Bus

中文通常叫：
- 高级外设总线

它的作用是：
- 给不同外设提供时钟和访问通道。
- F103 中 APB1 最高 36MHz，APB2 最高 72MHz。

你可以先把它理解成：
- 外设所在的两条总线。

在本课里，`SYSCLK = 72MHz` 时，APB1 必须设置为 `/2`，得到 36MHz；APB2 可以不分频保持 72MHz。如果 APB1 不分频，会超过规格。

### 6.10 `HAL_RCC_OscConfig` 是什么

`HAL_RCC_OscConfig` 全称是：
- HAL RCC Oscillator Configuration

中文通常叫：
- HAL 振荡器配置函数

它的作用是：
- 根据 `RCC_OscInitTypeDef` 配置 HSE、HSI、PLL 等振荡源。
- 等待对应时钟源稳定。

你可以先把它理解成：
- HAL 版里配置“时钟源和 PLL”的函数。

在本课里，它对应寄存器版打开 HSE、选择 PLL 来源、设置 PLL x9、打开 PLL 并等待稳定。

### 6.11 `HAL_RCC_ClockConfig` 是什么

`HAL_RCC_ClockConfig` 全称是：
- HAL RCC Clock Configuration

中文通常叫：
- HAL 总线时钟配置函数

它的作用是：
- 选择 SYSCLK 来源。
- 设置 AHB、APB1、APB2 分频。
- 设置 Flash latency。

你可以先把它理解成：
- HAL 版里把稳定的时钟源分配给 CPU 和各条总线的函数。

在本课里，它对应寄存器版配置 `RCC->CFGR` 的 HPRE、PPRE、SW，并配置 Flash 等待周期。

## 7. 寄存器版代码逐步讲解

寄存器版在 [reg/src/main.c](reg/src/main.c)。

### 7.1 先看完整逻辑

```c
system_clock_72mhz_init();
led_pc13_init();

while (1) {
    GPIOC->BRR = GPIO_BRR_BR13;
    delay(1200000U);
    GPIOC->BSRR = GPIO_BSRR_BS13;
    delay(1200000U);
}
```

时钟配置先于 LED 初始化，是因为本课要先建立 72MHz 系统基准，再用 LED 观察程序稳定运行。

### 7.2 第一步为什么配置 `FLASH->ACR`

```c
FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
```

`FLASH_ACR_PRFTBE` 打开预取缓冲。  
`FLASH_ACR_LATENCY_2` 设置 2 个等待周期。

这一步必须在切到 72MHz 前完成。否则 CPU 从 Flash 取指可能跟不上时钟，程序会变得不稳定。

### 7.3 为什么先打开并等待 HSE

```c
RCC->CR |= RCC_CR_HSEON;
while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
}
```

`HSEON` 是启动外部高速时钟的开关。`HSERDY` 是外部时钟稳定标志。

HSE 启动需要时间，所以不能置位后立刻使用。若板子没有 HSE，程序会卡在这里，这是一个很典型的排错点。

### 7.4 `RCC->CFGR` 为什么先清零再设置

代码先清除相关字段：

```c
RCC->CFGR &= ~(RCC_CFGR_HPRE |
               RCC_CFGR_PPRE1 |
               RCC_CFGR_PPRE2 |
               RCC_CFGR_PLLSRC |
               RCC_CFGR_PLLXTPRE |
               RCC_CFGR_PLLMULL |
               RCC_CFGR_SW);
```

这些字段分别控制 AHB 分频、APB1 分频、APB2 分频、PLL 来源、PLL 预分频、PLL 倍频、系统时钟选择。

先清零是为了避免旧值残留。例如 PLL 倍频字段如果旧值没清干净，就可能不是 x9。

然后设置目标值：

```c
RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
RCC->CFGR |= RCC_CFGR_PLLSRC;
RCC->CFGR |= RCC_CFGR_PLLMULL9;
```

含义是：

- AHB 不分频：HCLK = 72MHz。
- APB1 二分频：PCLK1 = 36MHz。
- APB2 不分频：PCLK2 = 72MHz。
- PLL 输入来自 HSE。
- PLL 倍频 x9。

### 7.5 为什么 APB1 要 `/2`

STM32F103 的 APB1 最高频率是 36MHz。系统时钟跑到 72MHz 后，如果 APB1 不分频，就会超过规格。

所以本课设置：

```text
PCLK1 = HCLK / 2 = 36MHz
```

这个值后面会影响 USART2/3、I2C、TIM2/3/4 等挂在 APB1 上的外设。

### 7.6 为什么打开 PLL 后还要等 `PLLRDY`

```c
RCC->CR |= RCC_CR_PLLON;
while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
}
```

PLL 需要时间锁定到稳定输出。`PLLRDY = 1` 才说明 72MHz 输出可用。

如果不等它稳定就切换系统时钟，CPU 可能运行在不稳定时钟上。

### 7.7 为什么切换后还要等 `SWS`

```c
RCC->CFGR &= ~RCC_CFGR_SW;
RCC->CFGR |= RCC_CFGR_SW_PLL;
while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
}
```

`SW` 是“请求切换到哪个时钟源”。  
`SWS` 是“当前实际正在使用哪个时钟源”。

写 `SW` 后硬件需要一点时间完成切换，所以要等待 `SWS` 变成 PLL。只写 `SW` 不看 `SWS`，不能保证系统已经真正切过去。

## 8. HAL 版代码逐步讲解

HAL 版在 [hal/src/main.c](hal/src/main.c)。

### 8.1 HAL 版和寄存器版的本质差异

HAL 版把时钟源配置拆成两个结构体：

- `RCC_OscInitTypeDef`：描述振荡器和 PLL。
- `RCC_ClkInitTypeDef`：描述 SYSCLK、HCLK、PCLK1、PCLK2。

它和寄存器版做的是同一条时钟链路。

### 8.2 `RCC_OscInitTypeDef` 每个字段是什么

```c
osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
osc.HSEState = RCC_HSE_ON;
osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
osc.PLL.PLLState = RCC_PLL_ON;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
osc.PLL.PLLMUL = RCC_PLL_MUL9;
```

这些字段对应：

- 使用 HSE。
- 打开 HSE。
- HSE 不预分频。
- 打开 PLL。
- PLL 输入来自 HSE。
- PLL 倍频 x9。

`HAL_RCC_OscConfig(&osc)` 对应寄存器版的 HSE/PLL 配置与等待 ready。

### 8.3 `RCC_ClkInitTypeDef` 每个字段是什么

```c
clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
clk.APB1CLKDivider = RCC_HCLK_DIV2;
clk.APB2CLKDivider = RCC_HCLK_DIV1;
```

这些字段对应：

- SYSCLK 选择 PLL。
- AHB 不分频。
- APB1 二分频。
- APB2 不分频。

`HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2)` 对应寄存器版设置 `SW/HPRE/PPRE`，并配置 Flash latency。

### 8.4 为什么 HAL 版要检查返回值

```c
if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    error_handler();
}
```

如果 HSE 不稳定、PLL 配置非法，HAL 会返回错误。继续运行只会让后续 GPIO、延时、串口全部建立在错误时钟上，所以进入 `error_handler()` 更容易定位问题。

### 8.5 `SysTick_Handler()` 为什么出现在 HAL 版

HAL 版使用 `HAL_Delay()` 闪灯，所以需要 HAL Tick 递增：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

如果没有这个中断处理，`HAL_Delay()` 可能卡住。本课虽然重点是时钟树，但 HAL 版运行现象仍依赖 SysTick。

## 9. 两个版本真正应该怎么学

### 9.1 先学寄存器版

寄存器版让你看见时钟切换的硬顺序：Flash、HSE、PLL、分频、SYSCLK。这个顺序非常重要。

### 9.2 再看 HAL 版

HAL 版让你看到工程中如何用结构体表达同样配置。

### 9.3 正确心智模型

`HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 不是两个“魔法函数”，它们分别对应“时钟源/PLL配置”和“系统/总线分频配置”。

## 10. 检验问题清单

1. **为什么 8MHz HSE 可以得到 72MHz？**
   - **答**：PLL 输入选择 HSE，倍频设置为 x9，所以 8MHz x 9 = 72MHz。

2. **为什么切到 72MHz 前要配置 Flash latency？**
   - **答**：CPU 高频取指时 Flash 响应速度跟不上，需要等待周期保证稳定。

3. **为什么 APB1 要分频到 36MHz？**
   - **答**：STM32F103 的 APB1 最大频率是 36MHz，HCLK 为 72MHz 时必须二分频。

4. **`HSERDY` 和 `PLLRDY` 有什么作用？**
   - **答**：它们分别表示 HSE 和 PLL 已稳定，未稳定前不能作为可靠时钟源。

5. **`SW` 和 `SWS` 有什么区别？**
   - **答**：`SW` 是软件请求切换到哪个时钟源，`SWS` 是硬件反馈当前实际使用哪个时钟源。

6. **时钟配错会影响哪些后续功能？**
   - **答**：会影响延时、SysTick、定时器频率、串口波特率、I2C/SPI 时序和 RTOS Tick。

7. **HAL 版两个 RCC 配置函数分别对应什么？**
   - **答**：`HAL_RCC_OscConfig()` 配置 HSE/PLL，`HAL_RCC_ClockConfig()` 配置 SYSCLK 和总线分频。

## 11. 工程实现步骤

### 11.1 需求分析

本课要把系统时钟配置到 72MHz，并用 LED 闪烁证明程序稳定运行。

### 11.2 硬件核查

确认板子有 8MHz HSE。若 HSE 不存在或不起振，本课寄存器版会卡在 `HSERDY`，HAL 版会进入错误处理。

### 11.3 寄存器实现路线

1. 设置 Flash 等待周期，保证高频取指稳定。
2. 打开 HSE，并等待 `HSERDY`。
3. 配置 AHB/APB 分频，特别是 APB1 /2。
4. 配置 PLL 来源和倍频。
5. 打开 PLL，并等待 `PLLRDY`。
6. SYSCLK 切换到 PLL，并等待 `SWS` 确认。
7. 用 LED 闪烁观察程序运行。

### 11.4 HAL 实现路线

1. 调用 `HAL_Init()`。
2. 填写 `RCC_OscInitTypeDef`，描述 HSE 和 PLL。
3. 调用 `HAL_RCC_OscConfig()`。
4. 填写 `RCC_ClkInitTypeDef`，描述 SYSCLK/AHB/APB。
5. 调用 `HAL_RCC_ClockConfig()` 并传入 `FLASH_LATENCY_2`。
6. 初始化 LED 并用 `HAL_Delay()` 观察节奏。

### 11.5 工程思维

时钟配置通常是工程最早执行的代码之一。后续外设初始化之前，必须先知道系统和总线频率是多少。

### 11.6 常见工程陷阱

- HSE 不存在却等待 HSE：程序卡死在 `HSERDY`。
- Flash latency 配小：高频运行不稳定。
- APB1 没分频：超过芯片规格。
- 以为写了 `SW` 就已经切换：没有检查 `SWS`，可能误判频率。

## 12. 运行现象

PC13 LED 稳定闪烁。

本课 LED 不是为了学 GPIO，而是作为“程序切到 72MHz 后仍稳定运行”的可见现象。

## 13. 常见问题排查

### 13.1 程序卡住不闪

优先判断卡在哪里：如果卡在 `HSERDY`，查 HSE 晶振；如果卡在 `PLLRDY`，查 PLL 来源和倍频配置。

### 13.2 HAL 版进入 `error_handler()`

检查 `HAL_RCC_OscConfig()` 或 `HAL_RCC_ClockConfig()` 的返回值。重点查 HSE 是否可用、PLL 参数是否合法、Flash latency 是否正确。

### 13.3 后续串口乱码或定时器频率不对

先回头查本课时钟配置。系统频率或 APB 分频错，会让所有基于时钟计算的外设参数整体偏移。

### 13.4 LED 节奏和预期不同

寄存器版空循环延时依赖 CPU 频率和编译优化；HAL 版 `HAL_Delay()` 依赖 SysTick。先区分是哪种延时方式。

## 14. 本课最核心的结论

1. HSE 8MHz 通过 PLL x9 可以得到 72MHz 系统时钟。
2. 切到高频前必须先配置 Flash 等待周期。
3. APB1 最高 36MHz，72MHz HCLK 下必须二分频。
4. `ready` 标志不是装饰，必须等时钟稳定后再继续。
5. 后续所有定时、波特率和 RTOS Tick 都依赖本课时钟基础。

## 15. 建议你现在怎么读这节课

1. 先画出 HSE、PLL、SYSCLK、AHB、APB1、APB2 的关系图。
2. 再读寄存器版，按顺序标出每一步改了 `FLASH->ACR` 还是 `RCC`。
3. 然后读 HAL 版，把结构体字段映射回寄存器版。
4. 最后尝试把 PLL 倍频改小，观察 LED 节奏或调试变量变化。

## 16. 扩展练习

1. 把 PLL 倍频从 x9 改成 x8，思考系统频率变成多少。
2. 故意不等待 `HSERDY`，理解为什么这不是可靠写法。
3. 在调试器里观察 `RCC->CFGR` 的 `SWS` 位。
4. 对比 HAL 版和寄存器版的 APB1 分频配置。

## 17. 下一课预告

下一课进入 [04_core_architecture_memory_map](../04_core_architecture_memory_map/README.md)。

你已经知道“时钟从哪里来”。下一课会看“寄存器为什么能用 C 指针访问”，也就是 Cortex-M3 的地址空间和存储器映射。
