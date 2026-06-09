# 16_adc_interrupt - ADC 中断采样

## 1. 本课到底在学什么

本课表面现象是：PA1 接电位器模拟电压，ADC1 转换完成后通过中断读取结果；当 ADC 值大于 2048 时 PC13 点亮，否则 PC13 熄灭。

真正要学的是 ADC 转换完成中断链路。上一课轮询方式是 CPU 启动转换后一直等 `EOC`；本课改成 `EOCIE` 使能后，ADC 转换完成会主动向 NVIC 发中断请求，CPU 进入 `ADC1_2_IRQHandler()`，在 ISR 或 HAL 回调里读取 `DR`。

这节课和上一课使用同一个硬件输入 PA1/ADC1_IN1，ADC 时钟、采样时间、校准流程也基本相同。核心变化是等待方式：从“CPU 傻等 EOC”变成“ADC 完成后通知 CPU”。后面 DMA 课程会进一步把“CPU 读取 DR”也交给硬件搬运。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释 ADC 中断采样和 ADC 轮询采样的区别。
- 说明 PA1/ADC1_IN1、模拟输入、ADC 时钟分频、采样时间和校准为什么仍然需要。
- 看懂 `CR1.EOCIE` 如何让转换完成事件产生中断请求。
- 解释为什么 STM32F103 中 ADC1 和 ADC2 共用 `ADC1_2_IRQn`。
- 说明 NVIC 使能和 ADC 内部 `EOCIE` 为什么是两道门。
- 解释 `ADC1_2_IRQHandler()` 中为什么进入后不再轮询 `EOC`。
- 看懂读取 `DR` 后 `EOC` 被清除，以及为什么中断里要再次启动下一次转换。
- 把 HAL 版 `HAL_ADC_Start_IT()`、`HAL_ADC_IRQHandler()`、`HAL_ADC_ConvCpltCallback()` 对应回寄存器链路。
- 说明 `volatile g_adc_value` 为什么用于中断和主循环共享。

## 3. 本课目录结构

```text
16_adc_interrupt/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 ADC1、`EOCIE`、`ADC1_2_IRQn`，在 `ADC1_2_IRQHandler()` 中读取 ADC 结果并再次启动转换。

`hal/` 使用 `HAL_ADC_Start_IT()` 启动中断采样，在 `HAL_ADC_ConvCpltCallback()` 中读取结果并再次启动。

两份工程都使用 `genericSTM32F103C8`、`stm32cube`、`stlink`，并定义 `HSE_VALUE=8000000U`。

## 4. 实验硬件

本课硬件和上一课相同：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA1 引脚
- 电位器或 0 到 3.3V 模拟电压源
- PC13 板载 LED

推荐接法：

```text
电位器一端 -> 3.3V
电位器另一端 -> GND
电位器中间脚 -> PA1
```

PA1 输入不能超过 3.3V。外部模拟源必须和 STM32 共地。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：旋转电位器时，`g_adc_value` 更新；大于 2048 时 PC13 亮，小于或等于 2048 时 PC13 灭。主循环不再等待 ADC。

物理/硬件层：PA1 接模拟电压，必须配置为模拟输入；PC13 是低电平点亮的板载 LED。

芯片模块层：GPIOA 负责 PA1 模拟输入，ADC1 负责采样转换，NVIC 负责把 ADC1/ADC2 共用中断送入 CPU，GPIOC 负责 LED。

寄存器/bit 层：`SQR/SMPR/CR2` 和上一课一样配置通道、采样和校准；新增 `CR1.EOCIE` 允许转换完成中断；新增 NVIC 使能 `ADC1_2_IRQn`；中断中读取 `DR` 并再次写 `SWSTART`。

C/CMSIS 层：寄存器版用 `ADC1_2_IRQHandler()` 作为中断入口，用 `volatile uint16_t g_adc_value` 保存中断更新结果。

HAL/工程层：HAL 版用 `HAL_ADC_Start_IT()` 启动转换和中断，用 `HAL_ADC_IRQHandler()` 分发中断，用 `HAL_ADC_ConvCpltCallback()` 写用户业务。

完整链路是：

1. 系统时钟配置到 72MHz。
2. PC13 配成输出，初始熄灭。
3. PA1 配成模拟输入。
4. ADC1 时钟打开，ADC 时钟分频为 PCLK2/6。
5. 规则组只采通道 1，采样时间 239.5 cycles。
6. ADC 上电并校准。
7. `CR1.EOCIE = 1`，允许转换完成中断。
8. NVIC 设置并使能 `ADC1_2_IRQn`。
9. main 中启动第一次 ADC 转换。
10. ADC 转换完成后 `EOC=1`。
11. 因为 `EOCIE=1` 且 NVIC 放行，CPU 进入 `ADC1_2_IRQHandler()`。
12. ISR 读取 `DR` 到 `g_adc_value`。
13. 根据阈值控制 PC13。
14. ISR 再次启动下一次转换。
15. 后续形成“转换 -> 中断 -> 再启动 -> 转换”的持续采样链。

## 6. 核心名词解释

### 6.1 ADC 中断采样是什么

ADC 中断采样是 ADC 转换完成后由硬件触发中断，CPU 在中断里读取结果的方式。

它属于 ADC 软件控制流程层。和轮询不同，CPU 不需要一直检查 `EOC`，转换期间可以执行别的代码。

本课使用单次转换加中断再启动，形成持续采样。

如果没有再次启动下一次转换，ADC 只会转换一次。

### 6.2 `PA1 / ADC1_IN1` 是什么

PA1 是 GPIOA 的 1 号引脚，ADC1_IN1 是 ADC1 的通道 1。

它属于物理引脚层和 ADC 通道层。本课仍然读取 PA1 上的模拟电压。

寄存器版配置 PA1 模拟输入，并让 `SQR3.SQ1=1`。HAL 版使用 `GPIO_PIN_1` 和 `ADC_CHANNEL_1`。

### 6.3 `EOC` 是什么

`EOC` 是 End Of Conversion，中文叫转换完成标志。

它属于 ADC 状态标志层。ADC 转换完成后硬件置位 `SR.EOC`，表示 `DR` 里有新结果。

轮询课中 CPU 一直等它；本课中它触发中断链路。

### 6.4 `EOCIE` 是什么

`EOCIE` 是 End Of Conversion Interrupt Enable，中文叫转换完成中断使能。

它属于 ADC 中断使能层，位于 `ADC1->CR1`。

寄存器版：

```c
ADC1->CR1 |= ADC_CR1_EOCIE;
```

如果 `EOCIE=0`，`EOC` 仍会置位，但不会向 NVIC 发中断请求。

### 6.5 `ADC1_2_IRQn` 是什么

`ADC1_2_IRQn` 是 ADC1 和 ADC2 共用的 NVIC 中断号。

它属于 Cortex-M NVIC 中断编号层。STM32F103 中 ADC1 和 ADC2 共用同一个中断入口，所以名字里有 `1_2`。

本课只使用 ADC1，但仍然要使能 `ADC1_2_IRQn`。

如果 NVIC 没使能，即使 `EOCIE=1`，CPU 也不会进入 ISR。

### 6.6 `ADC1_2_IRQHandler()` 是什么

`ADC1_2_IRQHandler()` 是 ADC1/ADC2 共用中断服务函数。

它属于 C 代码中断入口层。函数名必须和启动文件中的向量表匹配。

寄存器版在这里判断 `ADC1->SR & ADC_SR_EOC`，读取 `DR`，控制 LED，再次启动转换。

如果函数名写错，中断会进默认处理函数或没有业务执行。

### 6.7 `DR` 是什么

`DR` 是 Data Register，中文叫数据寄存器。

它属于 ADC 转换结果层。转换完成后，12 位结果保存在 `DR` 中。

读取 `DR` 后，F103 的 `EOC` 会被清除。中断方式中，读取 `DR` 既取走数据，也完成标志清理。

### 6.8 `volatile g_adc_value` 是什么

`g_adc_value` 是保存最新 ADC 值的全局变量，`volatile` 表示它可能被中断异步修改。

它属于 C 语言中断共享数据层。寄存器版在 ISR 中写它，主循环理论上也可以读它。

如果去掉 `volatile`，编译器可能优化主循环读取，导致看不到最新值。

### 6.9 `adc1_start_conversion()` 是什么

这是寄存器版的软件启动转换函数。

它属于软件触发层。本课中 main 调用它启动第一次转换；ISR 里再次调用它启动下一次转换。

代码：

```c
ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
```

如果 ISR 里不调用它，持续采样链会断掉。

### 6.10 `NVIC` 是什么

`NVIC` 是 Nested Vectored Interrupt Controller，中文叫嵌套向量中断控制器。

它属于 Cortex-M 内核中断控制层。ADC 外设内部中断使能只是分闸，NVIC 是 CPU 接收中断的总闸。

本课使用 `NVIC_SetPriority()` 和 `NVIC_EnableIRQ()`。

### 6.11 `HAL_ADC_Start_IT()` 是什么

`HAL_ADC_Start_IT()` 是 HAL 的 ADC 中断启动接口。

它属于 HAL 运行控制层。它启动 ADC 转换，并使能转换完成中断。

它对应寄存器版的 `EOCIE` 和 `SWSTART` 相关动作，但 NVIC 仍在初始化里手动配置。

### 6.12 `HAL_ADC_IRQHandler()` 是什么

`HAL_ADC_IRQHandler()` 是 HAL 的 ADC 中断分发函数。

它属于 HAL 中断处理层。用户的 `ADC1_2_IRQHandler()` 调用它后，它检查 ADC 状态标志，清理状态，并调用对应回调。

如果 HAL 版 ISR 不调用它，`HAL_ADC_ConvCpltCallback()` 不会执行。

### 6.13 `HAL_ADC_ConvCpltCallback()` 是什么

这是 HAL 的 ADC 转换完成回调。

它属于 HAL 用户业务层。ADC 转换完成后，HAL 在中断处理过程中调用它。

本课在回调里读取 ADC 值、控制 PC13，并再次调用 `HAL_ADC_Start_IT()`。

### 6.14 `HAL_ADC_GetValue()` 是什么

`HAL_ADC_GetValue()` 是 HAL 读取 ADC 结果的接口。

它属于 HAL 寄存器读取封装层。本质上读取 `ADC1->DR`。

本课在回调里调用它，因为进入回调时转换已经完成。

### 6.15 单次转换是什么

单次转换表示 ADC 每启动一次只转换一次规则组。

它属于 ADC 转换模式层。本课 `ContinuousConvMode = DISABLE`，寄存器版也没有打开连续转换。

因此必须在 ISR 或回调里再次启动下一轮转换。

### 6.16 中断里直接控制 LED 是什么取舍

这是教学 Demo 的简化写法。

它属于工程实现策略层。简单场景中 ISR 直接控制 GPIO 很直观；复杂工程里更常见的是 ISR 只保存数据或置标志，主循环/任务处理业务。

本课这样写是为了把 ADC 完成中断和现象直接连起来。

## 7. 寄存器版代码逐步讲解

### 7.1 全局变量

代码：

```c
static volatile uint16_t g_adc_value = 0U;
```

它保存最新 ADC 结果。中断里写，其他代码可读，所以加 `volatile`。

### 7.2 系统时钟、PC13、PA1

系统时钟、PC13 输出、PA1 模拟输入与上一课一致。

PA1 仍必须是模拟输入，否则 ADC 采样路径不正确。

### 7.3 ADC 时钟、规则组、采样时间

`adc1_init()` 中先开 ADC1 时钟，设置 `ADCPRE_DIV6`，规则组只采通道 1，采样时间为 239.5 cycles。

这些是 ADC 正确采样的基础，中断模式不会替代它们。

### 7.4 开启 `EOCIE`

代码：

```c
ADC1->CR1 |= ADC_CR1_EOCIE;
```

这是本课相对轮询课新增的关键位。它允许 `EOC` 触发中断请求。

### 7.5 ADC 上电和校准

代码先置 `ADON`，等待稳定，再执行 `RSTCAL` 和 `CAL`。

校准完成后，ADC 才进入适合采样的状态。

### 7.6 配置 NVIC

代码：

```c
NVIC_SetPriority(ADC1_2_IRQn, 1U);
NVIC_EnableIRQ(ADC1_2_IRQn);
```

ADC 内部中断和 NVIC 都打开后，转换完成才能让 CPU 进入 ISR。

### 7.7 启动第一次转换

main 中调用：

```c
adc1_start_conversion();
```

没有第一次启动，就没有第一次 `EOC`，也就不会进入中断。

### 7.8 `ADC1_2_IRQHandler()` 判断 `EOC`

代码：

```c
if ((ADC1->SR & ADC_SR_EOC) != 0U) {
```

因为 ADC1/ADC2 共用入口，先判断 ADC1 是否确实完成转换。

进入中断后不需要再 while 等待 `EOC`，能进来就说明事件已经发生。

### 7.9 读取 `DR` 和控制 LED

代码读取：

```c
g_adc_value = (uint16_t)ADC1->DR;
```

随后根据是否大于 2048 控制 PC13。读取 `DR` 后 `EOC` 清除。

### 7.10 ISR 中再次启动转换

代码：

```c
adc1_start_conversion();
```

本课是单次转换模式。每次转换完成后必须再次启动，才能持续采样。

### 7.11 主循环空闲

主循环不等待 ADC。ADC 完成后中断会自动处理。

真实项目里，这里可以放其他任务；这就是中断方式比轮询方式更灵活的地方。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化和基础配置

HAL 版先 `HAL_Init()`，再配置时钟、PC13、PA1 模拟输入。

这些和 ADC 轮询 HAL 版一致。

### 8.2 `hadc1.Init` 配 ADC 模式

HAL 版仍配置单通道、单次转换、软件触发、右对齐、规则组 1 个通道。

`ContinuousConvMode = DISABLE` 是后面需要在回调里再次 Start 的原因。

### 8.3 HAL ADC 校准和通道配置

`HAL_ADCEx_Calibration_Start()` 对应寄存器版 `RSTCAL/CAL`。

`HAL_ADC_ConfigChannel()` 配置 `ADC_CHANNEL_1`、`ADC_REGULAR_RANK_1`、`ADC_SAMPLETIME_239CYCLES_5`。

### 8.4 HAL NVIC 配置

代码：

```c
HAL_NVIC_SetPriority(ADC1_2_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
```

`HAL_ADC_Start_IT()` 会使能 ADC 内部中断，但不会自动替你配置 NVIC。本课手动配置。

### 8.5 `HAL_ADC_Start_IT()`

main 中第一次调用：

```c
HAL_ADC_Start_IT(&hadc1)
```

它启动第一次中断转换。和 `HAL_ADC_Start()` 不同，它会让转换完成后走中断回调。

### 8.6 `ADC1_2_IRQHandler()`

HAL 版 ISR：

```c
HAL_ADC_IRQHandler(&hadc1);
```

用户业务不直接写在这里，而是交给 HAL 检查标志并调用回调。

### 8.7 `HAL_ADC_ConvCpltCallback()`

回调先判断：

```c
if (hadc->Instance == ADC1) {
```

确认是 ADC1 完成转换。然后读取结果、控制 LED、再次调用 `HAL_ADC_Start_IT()`。

### 8.8 `HAL_ADC_GetValue()`

在回调中读取：

```c
g_adc_value = HAL_ADC_GetValue(hadc);
```

这对应寄存器版读 `ADC1->DR`。进入回调时转换已经完成，不需要再 Poll。

### 8.9 `error_handler()`

HAL 函数返回非 `HAL_OK` 时进入错误处理，关闭中断并死循环。

调试时可以检查是不是 Start_IT、Init、Calibration 或 ConfigChannel 失败。

## 9. 两个版本真正应该怎么学

寄存器版抓住新增链路：

```text
EOC -> EOCIE -> ADC1_2_IRQn -> ADC1_2_IRQHandler -> 读 DR -> 再 SWSTART
```

HAL 版抓住回调链：

```text
HAL_ADC_Start_IT -> ADC1_2_IRQHandler -> HAL_ADC_IRQHandler -> HAL_ADC_ConvCpltCallback -> HAL_ADC_GetValue -> 再 Start_IT
```

不要把中断理解成“自动读取 ADC”。中断只是把 CPU 带到 ISR；读取 `DR` 和再次启动仍然要由代码完成。

## 10. 检验问题清单

### 10.1 中断采样比轮询采样改变了哪一步？

答：启动、通道、采样、校准基本不变；等待转换完成这一步从 CPU 轮询 `EOC` 改成 ADC 完成后触发中断。

### 10.2 只开 `EOCIE`，不开 NVIC 会怎样？

答：ADC 外设内部允许发中断请求，但 NVIC 没放行，CPU 不会进入 `ADC1_2_IRQHandler()`。

### 10.3 只开 NVIC，不开 `EOCIE` 会怎样？

答：CPU 中断通道放行了，但 ADC 完成转换不会发中断请求，也不会进入 ISR。

### 10.4 为什么 ISR 里不需要再等待 `EOC`？

答：能进入 ADC 完成中断，说明 `EOC` 已经置位。ISR 只需确认标志、读取数据。

### 10.5 为什么要在 ISR/回调里再次启动转换？

答：本课是单次转换模式。一次转换完成后 ADC 停止，不再次启动就不会继续采样。

### 10.6 `ADC1_2_IRQn` 为什么名字里有 1_2？

答：STM32F103 中 ADC1 和 ADC2 共用一个中断号和入口函数。

### 10.7 HAL 版哪个函数会调用用户回调？

答：`HAL_ADC_IRQHandler()` 检查到转换完成后，会调用 `HAL_ADC_ConvCpltCallback()`。

### 10.8 如果回调里不判断 `hadc->Instance` 会怎样？

答：项目中如果 ADC2 也使用中断，ADC2 完成转换也可能进入同一个回调，导致误处理。

## 11. 工程实现步骤

### 11.1 需求分析

需求是用中断方式持续采样 PA1 模拟电压，并根据结果控制 PC13。

这要求 ADC 基础采样配置正确，ADC 完成中断使能，NVIC 放行，中断入口正确，ISR/回调能读取数据并再次启动。

### 11.2 硬件核查

硬件接线和上一课相同：电位器中间脚接 PA1，两端接 3.3V 和 GND。

确认输入电压不超过 3.3V，外部模拟源共地。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出。
3. 配置 PA1 模拟输入。
4. 配置 ADC1 时钟、分频、规则组、采样时间。
5. 设置 `CR1.EOCIE`。
6. ADC 上电并校准。
7. `NVIC_EnableIRQ(ADC1_2_IRQn)`。
8. main 中启动第一次转换。
9. ISR 中判断 `EOC`。
10. 读取 `DR`。
11. 控制 PC13。
12. ISR 中再次启动下一次转换。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 输出。
4. 配置 PA1 模拟输入。
5. 配置 `hadc1.Init`。
6. `HAL_ADC_Init()`。
7. `HAL_ADCEx_Calibration_Start()`。
8. `HAL_ADC_ConfigChannel()`。
9. 配置 `ADC1_2_IRQn`。
10. `HAL_ADC_Start_IT()`。
11. `ADC1_2_IRQHandler()` 调用 `HAL_ADC_IRQHandler()`。
12. `HAL_ADC_ConvCpltCallback()` 读取结果并再次 Start_IT。

### 11.5 工程思维

中断方式释放了 CPU 等待时间，但也带来 ISR 设计问题。中断里应该短小，避免长时间阻塞其他中断。

本课为了直观，在 ISR/回调里直接控制 LED。真实工程中通常只保存 ADC 值或置标志，主循环或任务里处理复杂逻辑。

### 11.6 常见工程陷阱

第一个陷阱是忘记 NVIC，只开了 ADC 内部中断。

第二个陷阱是忘记 `EOCIE` 或没有用 `HAL_ADC_Start_IT()`。

第三个陷阱是中断里不读 `DR`，导致标志和数据处理不正确。

第四个陷阱是单次转换后不再次启动。

第五个陷阱是 ISR 里做太多耗时工作。

## 12. 运行现象

旋转电位器时，`g_adc_value` 会更新。

当 `g_adc_value > 2048` 时，PC13 低电平点亮；否则 PC13 高电平熄灭。

主循环为空也没关系，因为 ADC 采样和 LED 控制由中断驱动。

## 13. 常见问题排查

### 13.1 只采样一次就不变了

检查 ISR 或 HAL 回调里是否再次启动下一次转换。

寄存器版看 `adc1_start_conversion()`，HAL 版看 `HAL_ADC_Start_IT(hadc)`。

### 13.2 完全不进中断

检查 `EOCIE`、`ADC1_2_IRQn`、函数名 `ADC1_2_IRQHandler()`。

HAL 版还要确认调用的是 `HAL_ADC_Start_IT()`，不是 `HAL_ADC_Start()`。

### 13.3 HAL 版进 IRQ 但不进回调

检查 `ADC1_2_IRQHandler()` 是否调用 `HAL_ADC_IRQHandler(&hadc1)`。

如果直接空着，HAL 回调不会执行。

### 13.4 ADC 值不随电位器变化

按上一课 ADC 基础链路排查：PA1 接线、模拟输入模式、规则组通道、采样时间、ADC 时钟和校准。

中断方式不会修复通道配置错误。

### 13.5 程序频繁进中断影响其他逻辑

本课每次转换完成立刻启动下一次，采样频率较高。

可以在中断里不立刻重启，改由定时器或主循环控制采样间隔。

## 14. 本课最核心的结论

- ADC 中断采样保留了 ADC 基础配置，只改变等待结果的方式。
- `EOC` 表示转换完成，`EOCIE` 让它产生中断请求。
- ADC 内部中断使能和 NVIC 使能缺一不可。
- F103 中 ADC1/ADC2 共用 `ADC1_2_IRQn`。
- 中断里读取 `DR` 得到结果，也清理转换完成状态。
- 单次转换模式下，想持续采样必须再次启动下一次转换。
- HAL 的 Start_IT/IRQHandler/Callback 对应寄存器版的 EOCIE/ISR/读 DR。

## 15. 建议你现在怎么读这节课

先拿上一课轮询流程做对比：启动、采样、读取都还在，只有等待方式变了。

然后读寄存器版 `adc1_init()` 里的 `EOCIE` 和 NVIC，再读 `ADC1_2_IRQHandler()`。

最后看 HAL 版中断链：`HAL_ADC_Start_IT()` 到 `HAL_ADC_ConvCpltCallback()`。

## 16. 扩展练习

1. 在主循环里读取 `g_adc_value`，根据不同区间做不同 LED 节奏。
2. 去掉 ISR 里的再次启动，观察是否只采样一次。
3. 把阈值 2048 改成 1024 或 3072。
4. 在回调里只置标志，主循环里控制 LED。
5. 思考如果采样频率很高，为什么 DMA 比中断更合适。

## 17. 下一课预告

上一课：[15_adc_polling](../15_adc_polling/README.md)

下一课：[17_adc_multichannel_scan](../17_adc_multichannel_scan/README.md)

下一课会学习 ADC 多通道扫描。ADC 不再只采 PA1 一个通道，而是按规则组序列依次采多个模拟输入。
