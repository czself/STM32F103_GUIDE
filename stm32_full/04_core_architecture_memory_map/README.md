# 第 4 课：内核架构与存储器映射

## 1. 本课到底在学什么

这节课表面上是在做：

- 把 `SCB->CPUID`、`FLASH_BASE`、`SRAM_BASE`、`PERIPH_BASE`、`GPIOC_BASE` 放进调试变量
- 运行程序后，在调试器 Watch 窗口里观察这些值
- 用 PC13 闪烁证明程序仍在正常运行

真正学习的是 Cortex-M3 的存储器映射思想：

```text
Cortex-M3 内核发出地址访问
  -> 总线根据地址范围判断访问对象
  -> 0x08000000 附近是 Flash
  -> 0x20000000 附近是 SRAM
  -> 0x40000000 附近是外设寄存器
  -> C 语言里的 RCC/GPIOC/SCB 本质是映射到这些地址的结构体指针
```

前面你已经写过 `GPIOC->CRH`。本课要补上背后的原因：**这不是普通变量，而是 CPU 对某个固定地址发起访问，硬件把这个地址解释成外设寄存器。**

## 2. 本课学习目标

学完本课，你应该能回答：

1. 为什么程序代码通常从 `0x08000000` 附近运行？
2. 为什么普通可写变量通常位于 `0x20000000` 附近？
3. 为什么 `GPIOC->CRH` 这种写法能改硬件寄存器？
4. `PERIPH_BASE` 和 `GPIOC_BASE` 分别代表什么？
5. `SCB->CPUID` 为什么属于内核寄存器，不属于普通 STM32 外设？
6. `volatile` 为什么常用于调试观察变量和寄存器访问？
7. 如果把外设地址当普通 RAM 随便写，会有什么风险？

## 3. 本课目录结构

```text
04_core_architecture_memory_map/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接读取 CMSIS 定义的内核寄存器和基地址宏。  
`hal/` 使用 HAL 工程结构运行同样的观察逻辑。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 观察方式：调试器 Watch 窗口
- 可见运行指示：PC13 LED 周期闪烁

本课没有额外外设接线。重点不是外部模块，而是看 CPU 如何通过地址访问 Flash、SRAM、外设和内核寄存器。

## 5. 先建立一个最基本的脑图

```text
程序下载到 Flash
  -> CPU 从 Flash 地址取指执行
  -> 全局变量分配在 SRAM
  -> 代码读取 CMSIS 提供的地址宏和 SCB 寄存器
  -> 把这些值保存到 g_debug_words[]
  -> 调试器从 SRAM 读取 g_debug_words[]
  -> 你在 Watch 窗口看到地址和 CPUID
```

这条链路里最关键的是：

1. MCU 不是用“变量名字”找硬件，而是用“地址”找硬件。
2. CMSIS 把这些地址包装成了容易读的 C 名字。

## 6. 先认识本课里出现的核心名词

### 6.1 `存储器映射` 是什么

`存储器映射` 英文通常叫：
- Memory map

中文通常叫：
- 存储器映射或地址映射

它的作用是：
- 规定不同地址范围对应 Flash、SRAM、外设或内核组件。
- 让 CPU 可以用统一的读写指令访问不同硬件对象。

你可以先把它理解成：
- MCU 的地址地图。

在本课里，`FLASH_BASE`、`SRAM_BASE`、`PERIPH_BASE` 都是这张地图上的关键地标。地址理解错，就会分不清代码、变量和外设寄存器。

### 6.2 `FLASH_BASE` 是什么

`FLASH_BASE` 全称可以理解为：
- Flash memory base address

中文通常叫：
- Flash 起始地址

它的作用是：
- 表示片内 Flash 在地址空间中的起点。
- STM32F103 用户程序通常从 `0x08000000` 附近开始存放。

你可以先把它理解成：
- 程序代码住的地方。

在本课里，代码把 `FLASH_BASE` 存入 `g_debug_words[1]`，用于在调试器里直观看到代码区的基地址。

### 6.3 `SRAM_BASE` 是什么

`SRAM_BASE` 全称可以理解为：
- SRAM base address

中文通常叫：
- SRAM 起始地址

它的作用是：
- 表示片内 SRAM 在地址空间中的起点。
- 全局变量、栈、堆等运行期可写数据通常位于 SRAM。

你可以先把它理解成：
- 程序运行时临时数据住的地方。

在本课里，`g_debug_words[]` 和 `g_sram_counter` 都是可写变量，调试器观察它们时，本质是在读 SRAM。

### 6.4 `PERIPH_BASE` 是什么

`PERIPH_BASE` 全称可以理解为：
- Peripheral base address

中文通常叫：
- 外设地址空间起始地址

它的作用是：
- 表示外设寄存器映射区域的起点。
- STM32F103 的很多外设寄存器都位于 `0x40000000` 之后。

你可以先把它理解成：
- 外设寄存器区的入口地址。

在本课里，理解 `PERIPH_BASE` 后，你就能明白 `GPIOC_BASE`、`RCC_BASE` 等外设基地址为什么都在外设地址区域内。

### 6.5 `GPIOC_BASE` 是什么

`GPIOC_BASE` 全称可以理解为：
- GPIOC peripheral base address

中文通常叫：
- GPIOC 外设基地址

它的作用是：
- 表示 GPIOC 寄存器组在地址空间中的起点。
- `GPIOC->CRH`、`GPIOC->BSRR` 等访问都以这个地址为基础加偏移。

你可以先把它理解成：
- GPIOC 这组硬件寄存器的门牌号。

在本课里，`GPIOC_BASE` 帮你把“外设地址”和前面学过的 `GPIOC->CRH` 连起来。

### 6.6 `SCB` 是什么

`SCB` 全称是：
- System Control Block

中文通常叫：
- 系统控制块

它的作用是：
- 提供 Cortex-M 内核级控制和状态寄存器。
- 包含 `CPUID`、低功耗控制、异常控制等信息。

你可以先把它理解成：
- Cortex-M 内核自己的控制寄存器组。

在本课里，`SCB` 不是 GPIO、USART 这类 STM32 外设，而是 ARM Cortex-M 内核的一部分。

### 6.7 `SCB->CPUID` 是什么

`CPUID` 全称是：
- CPU Identification Register

中文通常叫：
- CPU 身份识别寄存器

它的作用是：
- 保存内核实现、版本、架构等识别信息。
- 让软件或调试器确认当前 CPU 内核类型。

你可以先把它理解成：
- Cortex-M3 内核的身份证。

在本课里，代码读取 `SCB->CPUID` 并保存到 `g_debug_words[0]`。如果能在调试器里看到它，说明你已经在观察内核寄存器映射。

### 6.8 `volatile` 是什么

`volatile` 全称就是：
- volatile qualifier

中文通常叫：
- 易变限定符

它的作用是：
- 告诉编译器这个变量可能被硬件、中断或调试器观察影响。
- 避免编译器把看似“没用”的读写优化掉。

你可以先把它理解成：
- 告诉编译器“这个值别自作聪明优化掉”。

在本课里，`g_debug_words` 和 `g_sram_counter` 使用 `volatile`，方便你在调试器里持续观察它们。

### 6.9 `CoreDebug` 是什么

`CoreDebug` 全称是：
- Cortex-M Core Debug

中文通常叫：
- 内核调试控制块

它的作用是：
- 控制调试和追踪相关能力。
- 后续使用 DWT 周期计数器时通常要先打开 trace。

你可以先把它理解成：
- Cortex-M 内核调试功能的开关区。

本课主要认识地址映射，不强制展开 DWT，但要知道这类内核调试寄存器也在固定地址空间里。

### 6.10 `DWT` 是什么

`DWT` 全称是：
- Data Watchpoint and Trace

中文通常叫：
- 数据观察与跟踪单元

它的作用是：
- 提供观察点、跟踪和周期计数等调试能力。
- `CYCCNT` 可用于测量代码执行周期。

你可以先把它理解成：
- Cortex-M 内核里的高级调试计数器模块。

在本课里，DWT 是地址映射思想的延伸：内核调试模块也可以通过固定地址寄存器访问。

## 7. 寄存器版代码逐步讲解

寄存器版在 [reg/src/main.c](reg/src/main.c)。

### 7.1 先看完整逻辑

```c
system_clock_72mhz_init();
pc13_led_init();
memory_map_sample_init();
while (1) {
    g_sram_counter++;
    pc13_toggle();
    delay_cycles(3600000U);
}
```

`memory_map_sample_init()` 是本课核心，它把内核寄存器和基地址宏保存到 SRAM 变量里，方便调试器观察。

### 7.2 `g_debug_words[]` 为什么是全局 `volatile`

```c
static volatile uint32_t g_debug_words[5];
```

全局变量通常放在 SRAM，调试器容易观察。`volatile` 防止编译器认为这些变量“写了没人用”而优化掉。

如果不用 `volatile`，在高优化等级下，调试观察可能不符合你的预期。

### 7.3 `app_init()` 里每一行在观察什么

```c
g_debug_words[0] = SCB->CPUID;
```

读取 Cortex-M 内核身份寄存器。

```c
g_debug_words[1] = FLASH_BASE;
g_debug_words[2] = SRAM_BASE;
g_debug_words[3] = PERIPH_BASE;
g_debug_words[4] = GPIOC_BASE;
```

保存 Flash、SRAM、外设区、GPIOC 的基地址。它们不是运行时算出来的变量，而是 CMSIS 头文件里根据芯片手册定义好的地址。

### 7.4 `GPIOC->CRH` 为什么是地址访问

前面 LED 课里你看到：

```c
GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
```

本质是：

1. `GPIOC` 指向 `GPIOC_BASE`。
2. `CRH` 是 GPIO 寄存器结构体中的一个成员。
3. C 代码访问该成员时，CPU 实际访问 `GPIOC_BASE + CRH偏移`。
4. 硬件把这个地址解释为 GPIOC 的配置寄存器。

这就是“外设寄存器映射到地址空间”的具体体现。

### 7.5 为什么仍然保留 PC13 闪烁

本课真正观察点在调试器 Watch 窗口，但 PC13 闪烁能证明程序没有卡死。若调试变量不更新，同时 LED 也不闪，说明程序可能没有正常运行。

## 8. HAL 版代码逐步讲解

HAL 版在 [hal/src/main.c](hal/src/main.c)。

### 8.1 HAL 版和寄存器版的本质差异

HAL 版虽然使用 `HAL_Init()`、`HAL_GPIO_TogglePin()` 和 `HAL_Delay()`，但 `SCB->CPUID`、`FLASH_BASE` 这些地址概念没有变化。

HAL 不改变 Cortex-M 地址映射，只是封装外设初始化。

### 8.2 `HAL_Init()` 在本课的作用

`HAL_Init()` 建立 HAL 基础状态和 Tick。这样 `HAL_Delay()` 才能工作。

本课 HAL 版依然读取：

```c
g_debug_words[0] = SCB->CPUID;
```

这说明内核寄存器访问不依赖 HAL 封装。

### 8.3 HAL GPIO 与地址映射的关系

```c
HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
```

看起来是函数调用，但参数 `GPIOC` 仍然是指向 GPIOC 外设寄存器组的指针。HAL 内部最终还是通过这个地址访问 GPIOC 寄存器。

### 8.4 `SysTick_Handler()` 为什么出现

HAL 版使用 `HAL_Delay(500)`，它需要 HAL Tick 递增。因此：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
}
```

如果没有这段，HAL 版可能卡在延时里，调试变量虽然已经赋值，但 LED 不会继续闪。

## 9. 两个版本真正应该怎么学

### 9.1 先学寄存器版

寄存器版直接暴露 `SCB`、`GPIOC_BASE`、`PERIPH_BASE`，适合建立“地址就是硬件入口”的直觉。

### 9.2 再看 HAL 版

HAL 版告诉你：即使用 API，底层仍然绕不开这些地址。

### 9.3 正确心智模型

寄存器名、HAL 句柄、结构体字段都不是凭空来的，它们最终都要落到芯片手册定义的地址和 bit 上。

## 10. 检验问题清单

1. **为什么 Flash 和 SRAM 的地址范围不同？**
   - **答**：Cortex-M3 地址空间把不同硬件资源映射到不同区域，Flash 用于存代码，SRAM 用于运行期可写数据。

2. **`GPIOC_BASE` 和 `GPIOC->CRH` 有什么关系？**
   - **答**：`GPIOC_BASE` 是 GPIOC 寄存器组起始地址，`CRH` 是这个寄存器组中的一个偏移成员。

3. **为什么 `SCB` 不属于普通 STM32 外设？**
   - **答**：SCB 是 ARM Cortex-M 内核的系统控制块，属于内核外设区域。

4. **为什么调试变量要用 `volatile`？**
   - **答**：避免编译器优化掉读写，让调试器观察到真实内存变量变化。

5. **如果随便往外设地址写数据，会怎样？**
   - **答**：可能改变真实硬件寄存器状态，导致外设异常、系统跑飞或难以排查的问题。

6. **HAL 版是否改变了存储器映射？**
   - **答**：没有。HAL 只是封装访问方式，底层仍然访问同一套地址空间。

## 11. 工程实现步骤

### 11.1 需求分析

本课要通过调试器观察几个关键地址和内核寄存器，建立“寄存器访问就是地址访问”的基础理解。

### 11.2 硬件核查

确认 ST-Link 能调试连接。因为本课很多现象要在 Watch 窗口观察，不只是看 LED。

### 11.3 寄存器实现路线

1. 配置系统时钟和 PC13，保证程序可运行、可观察。
2. 定义 `volatile` 全局数组，作为调试观察窗口。
3. 读取 `SCB->CPUID`。
4. 保存 `FLASH_BASE/SRAM_BASE/PERIPH_BASE/GPIOC_BASE`。
5. 主循环递增 SRAM 变量并闪烁 LED。

### 11.4 HAL 实现路线

1. 调用 `HAL_Init()`。
2. 配置系统时钟和 PC13。
3. 同样保存内核寄存器和地址宏。
4. 用 `HAL_GPIO_TogglePin()` 和 `HAL_Delay()` 维持运行现象。

### 11.5 工程思维

读任何 STM32 寄存器代码，都要能追问：这个名字对应哪个基地址、哪个偏移、哪个 bit。

### 11.6 常见工程陷阱

- 把外设地址当普通 RAM：会误改硬件寄存器。
- 忘记 `volatile`：调试观察结果可能被优化影响。
- 只看变量名不看地址：无法理解寄存器访问本质。
- HAL 用久了忘记底层地址：排错时会卡住。

## 12. 运行现象

正常现象：

- PC13 周期闪烁。
- 调试器 Watch 窗口能看到 `g_debug_words[]` 中保存的 CPUID 和基地址。
- 寄存器版里 `g_sram_counter` 持续递增；HAL 版主要观察 `g_debug_words[]` 和 PC13 闪烁。

## 13. 常见问题排查

### 13.1 Watch 窗口看不到变量

确认变量是全局或静态变量，并且使用了 `volatile`。同时检查编译优化等级。

### 13.2 LED 闪但变量不变

检查 Watch 表达式是否写对，是否观察的是当前工程的变量，程序是否已经运行到 `app_init()` 之后。

### 13.3 变量能看但 LED 不闪

寄存器版查 GPIOC 时钟和 PC13 配置；HAL 版查 `SysTick_Handler()` 和 `HAL_IncTick()`。

### 13.4 不理解地址值

对照芯片参考手册的 Memory map 章节，确认 `0x08000000`、`0x20000000`、`0x40000000` 分别属于什么区域。

## 14. 本课最核心的结论

1. Cortex-M3 用统一地址空间访问 Flash、SRAM、外设和内核寄存器。
2. `GPIOC->CRH` 本质是对固定外设地址的结构体成员访问。
3. CMSIS 把芯片手册里的地址和寄存器定义成 C 语言名字。
4. `volatile` 对寄存器和调试观察变量非常重要。
5. HAL 不改变地址映射，只是封装访问过程。

## 15. 建议你现在怎么读这节课

1. 先记住三个地址区域：Flash、SRAM、Peripheral。
2. 再在调试器里观察 `g_debug_words[]`。
3. 然后回头看 `GPIOC->CRH`，把它拆成“基地址 + 偏移”。
4. 最后打开芯片手册 Memory map，对照这些地址。

## 16. 扩展练习

1. 在 Watch 窗口观察 `&g_debug_words[0]`，确认它位于 SRAM 区域。
2. 增加 `RCC_BASE`、`GPIOA_BASE` 到调试数组中。
3. 观察 `GPIOC->ODR` 随 LED 翻转变化。
4. 查手册确认 `SCB` 位于哪个内核地址区域。

## 17. 下一课预告

下一课进入 [05_systick](../05_systick/README.md)。

你已经知道地址如何对应硬件。下一课会使用 Cortex-M3 内核自带的 SysTick，让 CPU 按 1ms 节拍产生中断。
