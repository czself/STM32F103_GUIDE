# 17_adc_multichannel_scan - ADC 多通道扫描

## 1. 本课到底在学什么

本课表面现象是：ADC1 按规则组顺序连续采样 PA0 和 PA1 两个模拟输入，程序把第一次读到的结果放进 `g_adc0`，把第二次读到的结果放进 `g_adc1`。寄存器版中如果 `g_adc0 > g_adc1`，PC13 会翻转；HAL 版每 300ms 翻转一次 PC13，作为程序运行指示。

真正要学的是 ADC 多通道扫描。前面两课只采一个通道，规则组里只有一个成员；本课打开 `SCAN`，把规则组长度设为 2，并在序列中安排第 1 次转换采通道 0、第 2 次转换采通道 1。软件读取 `DR` 的顺序必须和规则组顺序一致。

这节课是 ADC DMA 的前置课。多通道扫描时，CPU 如果每个结果都手动轮询和读取，容易忙不过来；下一课之后会逐步让 DMA 自动把 ADC 多通道结果搬到内存数组里。本课先把“扫描顺序”和“结果读取顺序”讲清楚。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释为什么 PA0、PA1 都要配置为模拟输入。
- 说明 `ADC_SCAN_ENABLE` 或 `CR1.SCAN` 打开后 ADC 行为有什么变化。
- 看懂 `SQR1.L = 1` 为什么表示规则组有 2 个转换。
- 说明 `SQR3` 中 `SQ1=0`、`SQ2=1` 如何决定 PA0/PA1 的采样顺序。
- 解释为什么第一次读取 `DR` 是通道 0 结果，第二次读取 `DR` 是通道 1 结果。
- 区分扫描模式、连续转换模式和轮询读取。
- 看懂 HAL 版 `ADC_REGULAR_RANK_1`、`ADC_REGULAR_RANK_2` 对应规则组序列位置。
- 说明 rank 配错后变量和真实引脚为什么会对不上。
- 解释为什么多通道扫描常常要配合 DMA 使用。

## 3. 本课目录结构

```text
17_adc_multichannel_scan/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 ADC1 扫描模式、规则组序列和连续转换，然后轮询两次 `EOC` 读取两个结果。

`hal/` 使用 `ADC_HandleTypeDef` 和两次 `HAL_ADC_ConfigChannel()` 配置 rank 1/rank 2，再用 `HAL_ADC_PollForConversion()` 连续读取两个转换结果。

两份工程都使用 `genericSTM32F103C8`、`stm32cube`、`stlink`，并定义 `HSE_VALUE=8000000U`。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA0 模拟输入
- PA1 模拟输入
- 两个电位器或两个 0 到 3.3V 模拟电压源
- PC13 板载 LED

推荐接法：

```text
电位器 1 中间脚 -> PA0
电位器 2 中间脚 -> PA1
两个电位器两端 -> 3.3V 和 GND
```

PA0/PA1 输入电压都不能超过 3.3V。外部模拟源必须和 STM32 共地。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：调试器里 `g_adc0` 对应规则组第 1 次转换结果，正常接线下是 PA0 电压；`g_adc1` 对应第 2 次转换结果，正常接线下是 PA1 电压。

物理/硬件层：PA0 和 PA1 分别接两个模拟电压源，都要配置成模拟输入。ADC1 一次只能输出一个转换结果到 `DR`，所以两个通道结果要按顺序读取。

芯片模块层：GPIOA 负责 PA0/PA1 模拟输入，ADC1 负责扫描转换，RCC 提供 ADC 时钟，GPIOC 控制 PC13，HAL 版 SysTick 支撑 `HAL_Delay()`。

寄存器/bit 层：`CR1.SCAN` 打开扫描模式，`CR2.CONT` 打开连续转换，`SQR1.L` 设置规则组长度，`SQR3.SQ1/SQ2` 设置通道顺序，`SMPR2.SMP0/SMP1` 设置采样时间，`SR.EOC` 表示每个转换完成，`DR` 依次输出每个转换结果。

C/CMSIS 层：寄存器版连续两次等待 `EOC`，第一次读 `DR` 放入 `g_adc0`，第二次读 `DR` 放入 `g_adc1`。

HAL/工程层：HAL 版用 `NbrOfConversion = 2` 和两次 `HAL_ADC_ConfigChannel()` 配 rank；再用两次 Poll/GetValue 读取两个 rank 的结果。

完整链路是：

1. 系统时钟配置到 72MHz。
2. PC13 配成输出。
3. PA0、PA1 配成模拟输入。
4. ADC1 时钟打开。
5. 寄存器版设置 ADC 时钟为 PCLK2/6。
6. 打开 ADC 扫描模式。
7. 打开连续转换模式。
8. 设置规则组长度为 2。
9. 规则组第 1 项设置为通道 0，也就是 PA0。
10. 规则组第 2 项设置为通道 1，也就是 PA1。
11. 设置通道 0/1 采样时间。
12. ADC 校准后启动转换。
13. 第一次 `EOC` 后读取 `DR`，得到通道 0 结果。
14. 第二次 `EOC` 后读取 `DR`，得到通道 1 结果。
15. ADC 因连续转换模式继续下一轮扫描。

## 6. 核心名词解释

### 6.1 多通道扫描是什么

多通道扫描是 ADC 按规则组序列依次转换多个通道。

它属于 ADC 转换调度层。单通道时每次只采一个输入；扫描模式下，ADC 会按 `SQR` 里配置的顺序依次采多个输入。

本课扫描 PA0 和 PA1 两个通道。写错序列时，变量和实际引脚会对不上。

### 6.2 `PA0 / ADC1_IN0` 是什么

PA0 是 GPIOA 的 0 号引脚，ADC1_IN0 是 ADC1 的通道 0。

它属于物理引脚层和 ADC 通道层。本课把 PA0 作为第 1 个扫描通道，结果保存到 `g_adc0`。

如果电位器接在 PA0，但 rank 配到通道 1，读数就会跑到另一个变量。

### 6.3 `PA1 / ADC1_IN1` 是什么

PA1 是 GPIOA 的 1 号引脚，ADC1_IN1 是 ADC1 的通道 1。

它属于物理引脚层和 ADC 通道层。本课把 PA1 作为第 2 个扫描通道，结果保存到 `g_adc1`。

PA1 和 PA0 都必须是模拟输入。

### 6.4 `SCAN` 是什么

`SCAN` 是 ADC Scan Conversion Mode，中文叫扫描转换模式。

它属于 ADC 控制模式层。寄存器版设置 `ADC1->CR1 = ADC_CR1_SCAN`，HAL 版设置 `hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE`。

如果不打开扫描模式，ADC 不会按多个 rank 连续转换，第二个通道读数就不符合预期。

### 6.5 连续转换模式是什么

连续转换模式表示 ADC 完成一轮转换后自动开始下一轮。

它属于 ADC 运行模式层。寄存器版 `ADC_CR2_CONT`，HAL 版 `ContinuousConvMode = ENABLE`。

本课使用连续转换，所以启动一次后 ADC 会不断扫描 PA0/PA1。

如果关闭连续转换，就需要软件每轮重新启动。

### 6.6 `SQR1.L` 是什么

`SQR1.L` 是规则组序列长度字段。

它属于 ADC 规则组长度配置层。`L` 写的是转换数量减 1。

本课寄存器版：

```c
ADC1->SQR1 = ADC_SQR1_L_0;
```

`L=1` 表示规则组有 2 个转换。不是 1 个转换。

### 6.7 `SQR3.SQ1/SQ2` 是什么

`SQ1` 和 `SQ2` 是规则组第 1、第 2 个转换位置。

它们属于 ADC 规则组通道顺序层。本课：

```c
ADC1->SQR3 = (0U << 0) | (1U << 5);
```

`SQ1=0` 表示第 1 次转换通道 0，`SQ2=1` 表示第 2 次转换通道 1。

### 6.8 `Rank` 是什么

Rank 是 HAL 对规则组序列位置的称呼。

它属于 HAL 规则组配置层。`ADC_REGULAR_RANK_1` 对应第 1 个转换位置，`ADC_REGULAR_RANK_2` 对应第 2 个转换位置。

HAL 版用两次 `HAL_ADC_ConfigChannel()` 配 rank 1 和 rank 2。

Rank 配错时，变量读到的通道顺序会错。

### 6.9 `SMPR2` 是什么

`SMPR2` 是 ADC 采样时间寄存器 2。

它属于 ADC 采样时间层。通道 0 和 1 都在 `SMPR2` 中配置。

寄存器版把 `SMP0` 和 `SMP1` 都置成最长采样时间。HAL 版使用 `ADC_SAMPLETIME_55CYCLES_5`。

这也是两份代码的一个差异：采样时间不完全相同。

### 6.10 `EOC` 是什么

`EOC` 是 End Of Conversion，中文叫转换完成标志。

它属于 ADC 状态层。多通道扫描中，每完成一个通道转换，软件就可以读取一次 `DR`。

本课寄存器版和 HAL 版都按“等待一次 EOC，读取一次结果”的方式读取两个通道。

### 6.11 `DR` 是什么

`DR` 是 Data Register，中文叫数据寄存器。

它属于 ADC 结果输出层。ADC 每次转换完成后，当前通道结果放入 `DR`。

多通道扫描时，`DR` 不是同时保存两个结果，而是按转换完成顺序依次给出结果。

如果读取不及时，后续转换可能覆盖旧结果。

### 6.12 `g_adc0/g_adc1` 是什么

`g_adc0` 和 `g_adc1` 是保存两个通道结果的全局变量。

它们属于 C 代码数据层。寄存器版和 HAL 版都把第一次读取结果放入 `g_adc0`，第二次读取结果放入 `g_adc1`。

它们声明为 `volatile`，方便调试观察，并避免编译器过度优化。

### 6.13 `HAL_ADC_ConfigChannel()` 是什么

这是 HAL 配置 ADC 规则组通道的接口。

它属于 HAL 通道配置层。每次调用根据 `Channel`、`Rank`、`SamplingTime` 写入底层 `SQR` 和 `SMPR`。

本课 HAL 版调用两次：第一次配置通道 0 为 rank 1，第二次配置通道 1 为 rank 2。

### 6.14 `NbrOfConversion` 是什么

`NbrOfConversion` 是 HAL ADC 规则组转换数量字段。

它属于 HAL 规则组长度层。本课设置为 2，对应寄存器版 `SQR1.L=1`。

如果它仍是 1，HAL 版不会按两个 rank 完整扫描。

### 6.15 ADC 时钟分频是什么

ADC 时钟分频控制 PCLK2 到 ADC 的频率。

它属于 RCC/ADC 时钟层。寄存器版显式设置 `RCC_CFGR_ADCPRE_DIV6`。HAL 版当前代码没有显式调用 `__HAL_RCC_ADC_CONFIG()`，这点要如实注意。

实际工程中应确认 ADC 时钟不超过 F103 规格限制。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟和 PC13

系统时钟配置到 72MHz，PC13 配成输出。

PC13 在本课只是指示程序运行或比较结果，不参与 ADC 扫描本身。

### 7.2 全局变量

代码：

```c
static volatile uint16_t g_adc0, g_adc1;
```

`g_adc0` 保存规则组第 1 个结果，`g_adc1` 保存第 2 个结果。

### 7.3 打开 GPIOA 和 ADC1 时钟

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
```

PA0/PA1 属于 GPIOA，ADC1 是 APB2 外设。两个时钟都必须打开。

### 7.4 PA0/PA1 配成模拟输入

代码：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 | GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
```

清掉 PA0/PA1 的 MODE/CNF 后，它们成为模拟输入。

### 7.5 配置 ADC 时钟和扫描/连续模式

代码：

```c
RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
ADC1->CR1 = ADC_CR1_SCAN;
ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_CONT;
```

`ADCPRE_DIV6` 设置 ADC 时钟分频。`SCAN` 打开扫描模式。`CONT` 打开连续转换。`ADON` 给 ADC 上电。

### 7.6 配置采样时间

代码：

```c
ADC1->SMPR2 = ADC_SMPR2_SMP0 | ADC_SMPR2_SMP1;
```

通道 0 和 1 都设置为最长采样时间，适合电位器这类输入。

### 7.7 配置规则组长度和顺序

代码：

```c
ADC1->SQR1 = ADC_SQR1_L_0;
ADC1->SQR3 = (0U << 0) | (1U << 5);
```

`L=1` 表示两个转换。`SQ1=0` 表示先采 PA0，`SQ2=1` 表示再采 PA1。

### 7.8 校准并启动扫描

代码执行 `RSTCAL`、`CAL`，等待硬件完成后：

```c
ADC1->CR2 |= ADC_CR2_SWSTART;
```

这启动扫描。由于 `CONT=1`，ADC 会持续循环扫描两个通道。

### 7.9 第一次 EOC 读取 `g_adc0`

主循环：

```c
while((ADC1->SR & ADC_SR_EOC)==0U){}
g_adc0=ADC1->DR;
```

第一次等待到的是规则组第 1 个转换结果，也就是通道 0。

### 7.10 第二次 EOC 读取 `g_adc1`

接着：

```c
while((ADC1->SR & ADC_SR_EOC)==0U){}
g_adc1=ADC1->DR;
```

第二次等待到的是规则组第 2 个转换结果，也就是通道 1。

### 7.11 比较两个通道并翻转 PC13

代码：

```c
if(g_adc0>g_adc1) pc13_toggle();
delay_cycles(720000U);
```

当 PA0 电压对应值大于 PA1 时，PC13 翻转。延时让现象不至于太快。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化和系统时钟

HAL 版先 `HAL_Init()`，再配置系统时钟到 72MHz。

`SysTick_Handler()` 调用 `HAL_IncTick()`，为 `HAL_Delay()` 提供 tick。

### 8.2 HAL 配置 PA0/PA1 模拟输入

代码：

```c
gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
gpio.Mode = GPIO_MODE_ANALOG;
HAL_GPIO_Init(GPIOA, &gpio);
```

PA0 和 PA1 都进入模拟输入模式。

### 8.3 `hadc1.Init` 配扫描和连续转换

代码设置：

```c
hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
hadc1.Init.ContinuousConvMode = ENABLE;
hadc1.Init.NbrOfConversion = 2;
```

这对应打开扫描、连续转换和规则组长度为 2。

### 8.4 rank 1 配通道 0

代码：

```c
ch.Channel = ADC_CHANNEL_0;
ch.Rank = ADC_REGULAR_RANK_1;
ch.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
HAL_ADC_ConfigChannel(&hadc1, &ch);
```

这表示第 1 个转换位置采 PA0。

### 8.5 rank 2 配通道 1

代码：

```c
ch.Channel = ADC_CHANNEL_1;
ch.Rank = ADC_REGULAR_RANK_2;
HAL_ADC_ConfigChannel(&hadc1, &ch);
```

这表示第 2 个转换位置采 PA1。采样时间沿用上一次结构体中的 55.5 cycles。

### 8.6 校准并启动 ADC

HAL 版：

```c
HAL_ADCEx_Calibration_Start(&hadc1);
HAL_ADC_Start(&hadc1);
```

校准后启动连续扫描。注意当前 HAL 版没有显式设置 ADC 分频，工程中应确认默认分频满足 ADC 时钟限制。

### 8.7 主循环读取 rank 1/rank 2

代码：

```c
HAL_ADC_PollForConversion(&hadc1,10);
g_adc0=HAL_ADC_GetValue(&hadc1);
HAL_ADC_PollForConversion(&hadc1,10);
g_adc1=HAL_ADC_GetValue(&hadc1);
```

第一次 Poll/GetValue 读 rank 1 的结果，第二次读 rank 2 的结果。

### 8.8 PC13 心跳

HAL 版主循环每轮读取两个结果后：

```c
HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);
HAL_Delay(300);
```

这里 PC13 是运行指示，不根据 `g_adc0 > g_adc1` 判断。

## 9. 两个版本真正应该怎么学

寄存器版重点抓：

```text
SCAN
CONT
SQR1.L = 2 个转换
SQR3: SQ1=0, SQ2=1
EOC/DR 读两次
```

HAL 版重点抓：

```text
NbrOfConversion = 2
Rank 1 = ADC_CHANNEL_0
Rank 2 = ADC_CHANNEL_1
Poll/GetValue 两次
```

多通道扫描的关键不是“多写几个变量”，而是变量读取顺序必须严格对应规则组转换顺序。

## 10. 检验问题清单

### 10.1 为什么 `SQR1.L=1` 表示两个转换？

答：`L` 字段写的是转换数量减 1。两个转换时，`L=2-1=1`。

### 10.2 第一次读 `DR` 为什么是 PA0？

答：`SQR3.SQ1=0`，规则组第 1 个转换通道是 ADC 通道 0，也就是 PA0。

### 10.3 第二次读 `DR` 为什么是 PA1？

答：`SQR3.SQ2=1`，规则组第 2 个转换通道是 ADC 通道 1，也就是 PA1。

### 10.4 如果 rank 配反会怎样？

答：`g_adc0` 和 `g_adc1` 的物理含义会交换。你以为读 PA0，实际可能是 PA1。

### 10.5 不打开 `SCAN` 会怎样？

答：ADC 不会按多个 rank 扫描，第二个通道结果不可靠或不会按预期更新。

### 10.6 连续转换模式有什么作用？

答：一轮 PA0/PA1 扫描完成后，ADC 自动开始下一轮，不需要每轮手动启动。

### 10.7 为什么多通道扫描适合 DMA？

答：多个结果连续产生，CPU 手动按 EOC 读取容易忙且容易错过。DMA 可以按顺序搬到数组。

### 10.8 HAL 版当前要注意哪个时钟细节？

答：当前 HAL 版代码没有显式设置 ADC 分频。工程中应确认 ADC 时钟不超过 F103 规格限制。

## 11. 工程实现步骤

### 11.1 需求分析

需求是连续采样 PA0 和 PA1 两个模拟输入，并把结果分别放入 `g_adc0/g_adc1`。

这要求两个引脚模拟输入正确，扫描模式打开，规则组长度和 rank 顺序正确，软件读取顺序正确。

### 11.2 硬件核查

确认 PA0 和 PA1 都接入 0 到 3.3V 模拟信号。

两个模拟源必须和 STM32 共地。不要让任一输入超过 3.3V。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出。
3. 配置 PA0/PA1 模拟输入。
4. 打开 ADC1 时钟。
5. 设置 ADC 时钟分频。
6. 设置 `SCAN` 和 `CONT`。
7. 设置通道 0/1 采样时间。
8. 设置 `SQR1.L=1`。
9. 设置 `SQ1=0`、`SQ2=1`。
10. 校准 ADC。
11. 启动转换。
12. 等 EOC 读 `g_adc0`。
13. 再等 EOC 读 `g_adc1`。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 输出。
4. 配置 PA0/PA1 为 `GPIO_MODE_ANALOG`。
5. 打开 ADC1 时钟。
6. 配置 `ScanConvMode = ENABLE`。
7. 配置 `ContinuousConvMode = ENABLE`。
8. 配置 `NbrOfConversion = 2`。
9. rank 1 配 `ADC_CHANNEL_0`。
10. rank 2 配 `ADC_CHANNEL_1`。
11. 校准并启动 ADC。
12. 两次 Poll/GetValue 读取两个结果。

### 11.5 工程思维

多通道扫描最重要的是顺序意识。ADC 结果从同一个 `DR` 依次出来，软件必须知道当前读到的是第几个 rank。

当通道数量变多、采样速度变快时，应使用 DMA 把序列结果搬到数组，避免 CPU 手动读取错位。

### 11.6 常见工程陷阱

第一个陷阱是 rank 配错，变量和引脚对不上。

第二个陷阱是没开扫描模式，只能正确读一个通道。

第三个陷阱是读取 `DR` 次数不够或顺序错。

第四个陷阱是 ADC 时钟配置不明确。

第五个陷阱是模拟输入悬空导致读数乱跳。

## 12. 运行现象

寄存器版中，`g_adc0` 应随 PA0 电压变化，`g_adc1` 应随 PA1 电压变化。当 `g_adc0 > g_adc1` 时 PC13 翻转。

HAL 版中，`g_adc0/g_adc1` 同样分别保存两次转换结果；PC13 每 300ms 翻转一次，表示主循环持续运行。

用调试器观察变量，比只看 LED 更可靠。

## 13. 常见问题排查

### 13.1 两个值都不变化

检查 PA0/PA1 是否有模拟输入，是否配置为模拟输入，ADC 是否启动。

HAL 版检查 `HAL_ADC_Start()` 是否执行。

### 13.2 两个变量和电位器对应反了

检查 `SQR3` 或 HAL rank 配置。

如果 rank 1 是通道 1、rank 2 是通道 0，变量含义就会交换。

### 13.3 第二个通道读数异常

检查是否打开扫描模式，规则组长度是否为 2，是否读取了两次 `DR`。

只读一次只会拿到一个 rank 的结果。

### 13.4 读数抖动大

检查模拟输入是否悬空，电位器是否共地，采样时间是否足够。

寄存器版使用最长采样时间；HAL 版使用 55.5 cycles，必要时可改长。

### 13.5 ADC 时钟可能超规格

寄存器版显式设置 PCLK2/6。HAL 版当前代码未显式设置 ADC 分频，建议补充或确认默认配置，避免 ADC 时钟超过规格。

## 14. 本课最核心的结论

- 多通道扫描让 ADC 按规则组序列依次转换多个通道。
- PA0/PA1 必须都配置为模拟输入。
- `SCAN` 打开后，ADC 才会按多个 rank 扫描。
- `SQR1.L` 设置转换数量，`SQR3` 设置每个位置采哪个通道。
- `DR` 每次只给出当前转换结果，软件必须按顺序读取。
- HAL 的 rank 概念对应底层规则组序列位置。
- 多通道扫描和 DMA 是天然搭档。

## 15. 建议你现在怎么读这节课

先画一个两格序列：rank 1 是 PA0，rank 2 是 PA1。

然后看寄存器版 `SQR1/SQR3`，确认这个序列是怎么写进 ADC 的。

最后看主循环，两次 EOC、两次 DR 读取必须和序列顺序对齐。

## 16. 扩展练习

1. 交换 PA0/PA1 的 rank，观察 `g_adc0/g_adc1` 含义变化。
2. 把 HAL 版采样时间改成 239.5 cycles，比较读数稳定性。
3. 增加第三个通道，思考 `SQR1.L` 和读取次数怎么改。
4. 故意只读一次 `DR`，观察第二个变量是否更新。
5. 思考用 DMA 时，数组下标和 rank 如何对应。

## 17. 下一课预告

上一课：[16_adc_interrupt](../16_adc_interrupt/README.md)

下一课：[18_dma_basic](../18_dma_basic/README.md)

下一课会学习 DMA 基础。DMA 可以在外设和内存之间自动搬运数据，为后面的 ADC DMA 多通道采样打基础。
