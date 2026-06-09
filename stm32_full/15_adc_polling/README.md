# 15_adc_polling - ADC 轮询采样

## 1. 本课到底在学什么

本课表面现象是：PA1 接一个 0 到 3.3V 的模拟电压，例如电位器中间脚。程序不断读取 ADC1_IN1 的转换结果，结果大于 2048 时点亮 PC13，结果小于或等于 2048 时熄灭 PC13。

真正要学的是 ADC 轮询采样链路。模拟电压先从 PA1 进入 ADC1 的通道 1，ADC 采样保持电路取得电压，再经过 12 位模数转换得到 0 到 4095 的数字值。CPU 通过轮询 `EOC` 标志等待转换完成，然后读取 `DR` 数据寄存器。

这节课是从“数字输入输出”进入“模拟量采集”的第一课。前面 GPIO 读到的是 0 或 1；ADC 读到的是一个连续电压对应的数字量。下一课会把等待方式从轮询改成中断，本课先把最直观的启动、等待、读取三步讲透。

## 2. 本课学习目标

学完本课，你应该能做到：

- 解释 PA1 为什么要配置为模拟输入，而不是普通输入或复用输入。
- 说明 ADC1_IN1 和 PA1 的对应关系。
- 解释 12 位 ADC 结果为什么是 0 到 4095。
- 根据 `ADCPRE = PCLK2 / 6` 算出 ADC 时钟是 12MHz。
- 说明为什么 F103 ADC 时钟不能直接用 72MHz。
- 看懂规则组序列 `SQR1/SQR3` 如何选择只采 ADC 通道 1。
- 解释采样时间 `239.5 cycles` 为什么适合电位器这类源阻抗较高的输入。
- 说明 `ADON`、`RSTCAL`、`CAL`、`EXTTRIG`、`SWSTART`、`EOC`、`DR` 各自作用。
- 把 HAL 版 `HAL_ADC_Start()`、`HAL_ADC_PollForConversion()`、`HAL_ADC_GetValue()` 对应回寄存器版。

## 3. 本课目录结构

```text
15_adc_polling/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 PA1、ADC1、规则组、采样时间、校准和轮询读取。

`hal/` 使用 `ADC_HandleTypeDef`、`ADC_ChannelConfTypeDef` 和 HAL ADC 轮询接口实现同样功能。

两份工程都使用 `genericSTM32F103C8`、`stm32cube`、`stlink`，并定义 `HSE_VALUE=8000000U`。

## 4. 实验硬件

本课使用：

- STM32F103C8T6 BluePill
- ST-Link 下载器
- PA1 引脚
- 电位器或 0 到 3.3V 模拟电压源
- PC13 板载 LED

推荐电位器接法：

```text
电位器一端 -> 3.3V
电位器另一端 -> GND
电位器中间脚 -> PA1
```

PA1 输入电压必须在 0 到 3.3V 范围内。不要把 5V 模拟信号直接接到 PA1，否则可能损坏芯片。

PC13 是结果指示灯。BluePill 常见 PC13 LED 是低电平点亮，所以 ADC 值大于 2048 时，代码把 PC13 拉低让 LED 亮。

## 5. 先建立一个最基本的脑图

本课按六层拆开看。

现象层：旋转电位器时，PA1 电压变化。调试器里 `adc_value` 在 0 到 4095 之间变化；超过 2048 时 PC13 亮，低于或等于 2048 时 PC13 灭。

物理/硬件层：电位器把 3.3V 和 GND 之间的某个电压送到 PA1。PA1 必须配置为模拟输入，让模拟电压进入 ADC 采样电容。

芯片模块层：RCC 提供 GPIOA、GPIOC、ADC1 时钟；GPIOA 配置 PA1 模拟输入；ADC1 完成采样和转换；GPIOC 控制 PC13；HAL 版 SysTick 支撑 `HAL_Delay()`。

寄存器/bit 层：`RCC->CFGR.ADCPRE` 设置 ADC 时钟分频；`SQR1/SQR3` 设置规则组只转换通道 1；`SMPR2.SMP1` 设置采样时间；`CR2.ADON/RSTCAL/CAL/EXTTRIG/SWSTART` 控制 ADC 上电、校准和启动；`SR.EOC` 表示转换完成；`DR` 保存结果。

C/CMSIS 层：寄存器版 `adc1_read_channel1()` 通过写 `CR2` 启动转换，while 轮询 `EOC`，最后读 `ADC1->DR` 返回 `uint16_t` 结果。

HAL/工程层：HAL 版用 `HAL_ADC_Init()` 配整体 ADC 模式，用 `HAL_ADCEx_Calibration_Start()` 校准，用 `HAL_ADC_ConfigChannel()` 配通道，用 Start/Poll/GetValue 完成轮询读取。

完整链路是：

1. 系统时钟配置为 72MHz。
2. PC13 配成输出，初始熄灭。
3. GPIOA 时钟打开，PA1 配成模拟输入。
4. ADC1 时钟打开。
5. ADC 时钟设置为 PCLK2/6，也就是 12MHz。
6. 规则组长度设置为 1。
7. 规则组第 1 个转换通道设置为通道 1。
8. 通道 1 采样时间设置为 239.5 cycles。
9. ADC 上电，等待稳定。
10. 执行复位校准和校准。
11. 主循环软件触发一次规则组转换。
12. CPU 轮询等待 `EOC=1`。
13. 读取 `DR` 得到 12 位 ADC 值。
14. 和 2048 比较，控制 PC13。
15. 延时一小段时间后继续下一次采样。

## 6. 核心名词解释

### 6.1 `ADC` 是什么

`ADC` 是 Analog-to-Digital Converter，中文叫模数转换器。

它属于 STM32 片上模拟外设层。它把连续变化的模拟电压转换成离散数字值。

STM32F103 的 ADC 是 12 位，本课结果范围是 0 到 4095。0 接近 0V，4095 接近参考电压 VREF+，通常约 3.3V。

如果把 ADC 当成普通 GPIO 读，只能得到高低电平，无法得到中间电压值。

### 6.2 `ADC1` 是什么

`ADC1` 是 STM32F103 的第 1 个 ADC 外设。

它属于 ADC 外设实例层。本课使用 ADC1 读取 PA1 对应的 ADC1_IN1。

寄存器版所有 `ADC1->...` 都是在访问这个外设。HAL 版 `hadc1.Instance = ADC1` 也指定同一个硬件。

如果 ADC1 时钟没开，后面的 ADC 寄存器配置不会正常工作。

### 6.3 `PA1 / ADC1_IN1` 是什么

PA1 是 GPIOA 的 1 号引脚。ADC1_IN1 是 ADC1 的通道 1 输入。

它属于物理引脚层和 ADC 通道层。同一个 PA1 引脚在 ADC 功能下把模拟电压送到 ADC1 的通道 1。

本课代码配置 PA1，并在 ADC 规则组中选择通道 1。两边必须一致。

如果把电位器接到 PA0，但代码采 ADC1_IN1，就会读错通道。

### 6.4 模拟输入模式是什么

模拟输入模式是 GPIO 的一种模式。

它属于 GPIO 引脚电气层。在 STM32F1 中，模拟输入对应 `MODE=00`、`CNF=00`。这种模式会断开数字输入路径，减少对模拟电压的干扰。

寄存器版：

```c
GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
```

HAL 版：

```c
gpio.Mode = GPIO_MODE_ANALOG;
```

如果 PA1 配成普通输入，数字输入电路可能影响采样精度，也不符合 ADC 输入的正确配置。

### 6.5 `ADCPRE` 是什么

`ADCPRE` 是 ADC Prescaler，中文叫 ADC 预分频。

它属于 RCC 时钟树层，位于 `RCC->CFGR`。它决定 PCLK2 进入 ADC 前要除以多少。

本课设置：

```c
RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
```

PCLK2 是 72MHz，除以 6 得到 12MHz。F103 ADC 最大时钟约 14MHz，所以 12MHz 是安全选择。

如果 ADC 时钟过高，转换结果可能不稳定或不准确。

### 6.6 12 位 ADC 结果是什么

12 位结果表示 ADC 转换结果有 4096 个等级。

它属于 ADC 数据表示层。结果范围是 0 到 4095。

换算近似公式：

```text
电压 ≈ ADC值 / 4095 * VREF
```

本课用 2048 作为阈值，约等于 3.3V 的一半，也就是 1.65V。

### 6.7 规则组是什么

规则组是 ADC 的常规转换序列。

它属于 ADC 转换调度层。ADC 可以按一个列表依次转换多个通道，这个列表就是规则组序列。

本课只转换一个通道，所以规则组长度是 1，第一项是通道 1。

如果规则组通道和 PA1 不一致，读到的不是电位器电压。

### 6.8 `SQR1` 是什么

`SQR1` 是 ADC Regular Sequence Register 1，中文叫规则序列寄存器 1。

它属于 ADC 规则组配置层。本课使用其中的 `L` 字段表示规则组转换个数减 1。

代码：

```c
ADC1->SQR1 &= ~ADC_SQR1_L;
```

`L=0` 表示只转换 1 个通道。

### 6.9 `SQR3` 是什么

`SQR3` 是 ADC Regular Sequence Register 3，中文叫规则序列寄存器 3。

它属于 ADC 规则组通道选择层。`SQ1` 字段决定规则组第 1 个转换通道。

本课：

```c
ADC1->SQR3 &= ~ADC_SQR3_SQ1;
ADC1->SQR3 |= 1U;
```

表示第 1 个转换通道是 ADC 通道 1，也就是 PA1。

### 6.10 `SMPR2` 是什么

`SMPR2` 是 Sample Time Register 2，中文叫采样时间寄存器 2。

它属于 ADC 采样时间配置层。通道 0 到 9 的采样时间字段在 `SMPR2` 中。

本课通道 1 使用 `SMP1=111`，也就是 239.5 个 ADC 周期。

采样时间太短时，采样电容可能还没充到输入电压，结果偏低或抖动。

### 6.11 `ADON` 是什么

`ADON` 是 ADC ON，中文叫 ADC 开启位。

它属于 ADC 控制层，位于 `ADC1->CR2`。第一次置位 `ADON` 给 ADC 上电；后续配合触发位启动转换。

本课先上电，再等待一小段时间，让 ADC 内部稳定。

如果没上电，校准和转换都不能正常进行。

### 6.12 `RSTCAL` 和 `CAL` 是什么

`RSTCAL` 是 Reset Calibration，中文叫复位校准。`CAL` 是 Calibration，中文叫校准。

它们属于 ADC 校准控制层。F103 ADC 使用前通常要先复位校准，再执行校准。

寄存器版轮询等待硬件自动清除这些位。HAL 版用 `HAL_ADCEx_Calibration_Start()` 封装。

如果不校准，结果可能有偏移，尤其在精度要求较高时更明显。

### 6.13 `EXTTRIG` 和 `SWSTART` 是什么

`EXTTRIG` 是 External Trigger Enable，中文叫外部/软件触发使能。`SWSTART` 是 Software Start，中文叫软件启动。

它们属于 ADC 转换启动层。F103 中软件启动规则组转换时，需要允许触发，再写 `SWSTART` 发起转换。

本课：

```c
ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
```

如果 `SWSTART` 不生效，`EOC` 不会置位，轮询会卡住。

### 6.14 `EOC` 是什么

`EOC` 是 End Of Conversion，中文叫转换完成标志。

它属于 ADC 状态标志层，位于 `ADC1->SR`。转换完成后硬件置位 `EOC`，表示 `DR` 中有新数据。

寄存器版轮询：

```c
while ((ADC1->SR & ADC_SR_EOC) == 0U) {
}
```

如果 ADC 没启动或配置错误，这里可能一直等不到。

### 6.15 `DR` 是什么

`DR` 是 Data Register，中文叫数据寄存器。

它属于 ADC 结果寄存器层。转换完成后，12 位结果保存在 `DR` 的低位。

寄存器版：

```c
return (uint16_t)ADC1->DR;
```

读取 `DR` 后，F103 的 `EOC` 标志会被清除，准备下一次转换。

### 6.16 `ADC_HandleTypeDef` 是什么

`ADC_HandleTypeDef` 是 HAL 的 ADC 句柄结构体。

它属于 HAL 外设对象层。它保存 `Instance`、`Init`、状态、锁、DMA 关联等信息。

本课 `hadc1.Instance = ADC1`，说明 HAL 后续操作的是 ADC1。

### 6.17 `ADC_ChannelConfTypeDef` 是什么

`ADC_ChannelConfTypeDef` 是 HAL 的 ADC 通道配置结构体。

它属于 HAL 通道配置层。本课配置 `Channel = ADC_CHANNEL_1`、`Rank = ADC_REGULAR_RANK_1`、`SamplingTime = ADC_SAMPLETIME_239CYCLES_5`。

它对应寄存器版的 `SQR3` 和 `SMPR2` 配置。

### 6.18 轮询采样是什么

轮询采样是 CPU 主动等待转换完成的方式。

它属于软件控制流程层。CPU 启动 ADC 转换后，一直检查 `EOC`，直到转换完成。

本课使用轮询是为了让启动、等待、读取链路最直观。缺点是等待期间 CPU 不能做其他事。

下一课会用中断方式改进这个等待过程。

## 7. 寄存器版代码逐步讲解

### 7.1 `delay()` 的作用

`delay()` 用空循环和 `__NOP()` 做简单延时。

它不参与 ADC 转换本身，只用于 ADC 上电后等待稳定，以及主循环中降低采样/LED 更新速度。

### 7.2 系统时钟配置

`system_clock_72mhz_init()` 把 HSE 8MHz 通过 PLL x9 配成 72MHz。

ADC 挂在 APB2，PCLK2 是 72MHz。后续必须通过 `ADCPRE` 再分频到 12MHz，不能直接把 72MHz 给 ADC。

### 7.3 PC13 LED 初始化

代码打开 GPIOC 时钟，把 PC13 配成推挽输出，初始写高电平熄灭。

后面根据 ADC 阈值写 `BRR` 点亮或写 `BSRR` 熄灭。

### 7.4 PA1 配成模拟输入

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
GPIOA->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
```

PA1 属于 GPIOA，配置前先开时钟。清 `MODE1/CNF1` 后，PA1 进入模拟输入模式。

### 7.5 打开 ADC1 时钟并设置 ADC 分频

代码：

```c
RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
RCC->CFGR &= ~RCC_CFGR_ADCPRE;
RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
```

ADC1 挂在 APB2。`ADCPRE_DIV6` 让 ADC 时钟变成 12MHz，满足 F103 ADC 时钟限制。

### 7.6 配置规则组长度和通道

代码：

```c
ADC1->SQR1 &= ~ADC_SQR1_L;
ADC1->SQR3 &= ~ADC_SQR3_SQ1;
ADC1->SQR3 |= 1U;
```

`SQR1.L=0` 表示转换 1 个通道。`SQR3.SQ1=1` 表示第一个转换通道是 ADC 通道 1。

### 7.7 配置通道 1 采样时间

代码：

```c
ADC1->SMPR2 &= ~ADC_SMPR2_SMP1;
ADC1->SMPR2 |= ADC_SMPR2_SMP1;
```

`SMP1=111`，采样时间 239.5 cycles。加上固定转换 12.5 cycles，总转换约 252 cycles。12MHz 下约 21us。

### 7.8 ADC 上电、等待、校准

代码按顺序执行：

```c
ADC1->CR2 |= ADC_CR2_ADON;
delay(1000U);
ADC1->CR2 |= ADC_CR2_RSTCAL;
while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0U) {}
ADC1->CR2 |= ADC_CR2_CAL;
while ((ADC1->CR2 & ADC_CR2_CAL) != 0U) {}
```

先上电，再复位校准，再执行校准。每一步都要等硬件完成。

### 7.9 软件触发一次转换

代码：

```c
ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
```

`EXTTRIG` 允许触发，`SWSTART` 产生软件触发事件。ADC 开始规则组转换。

### 7.10 轮询等待 `EOC`

代码：

```c
while ((ADC1->SR & ADC_SR_EOC) == 0U) {
}
```

CPU 一直等待转换完成。这个等待时间内 CPU 不做其他工作。

### 7.11 读取 `DR`

代码：

```c
return (uint16_t)ADC1->DR;
```

读取数据寄存器得到 12 位 ADC 值。读取后 `EOC` 清除。

### 7.12 主循环阈值控制 LED

代码：

```c
if (adc_value > 2048U) {
    GPIOC->BRR = GPIO_BRR_BR13;
} else {
    GPIOC->BSRR = GPIO_BSRR_BS13;
}
```

`2048` 约等于半量程。高于阈值时 PC13 拉低，LED 亮；低于或等于阈值时 PC13 拉高，LED 灭。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和时钟配置

HAL 版先调用 `HAL_Init()`，再用 `HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 配置 72MHz 系统时钟。

这对应寄存器版 HSE、PLL、总线分频和 Flash latency 配置。

### 8.2 HAL 配置 PC13

`GPIO_InitTypeDef` 把 PC13 配成 `GPIO_MODE_OUTPUT_PP`，初始写 `GPIO_PIN_SET` 熄灭 LED。

`HAL_GPIO_WritePin()` 底层会写 GPIO 的置位/复位寄存器。

### 8.3 HAL 配置 PA1 模拟输入

代码：

```c
gpio.Pin = GPIO_PIN_1;
gpio.Mode = GPIO_MODE_ANALOG;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_MODE_ANALOG` 对应寄存器版 `MODE1=00`、`CNF1=00`。

### 8.4 `__HAL_RCC_ADC_CONFIG()` 设置 ADC 时钟

代码：

```c
__HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);
```

它对应 `RCC->CFGR.ADCPRE = PCLK2/6`。HAL 只是换了宏名，硬件仍然是 ADC 时钟分频。

### 8.5 `ADC_HandleTypeDef.Init` 配 ADC 整体模式

代码设置：

```c
hadc1.Instance = ADC1;
hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
hadc1.Init.ContinuousConvMode = DISABLE;
hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
hadc1.Init.NbrOfConversion = 1;
```

这些字段表示单通道、单次转换、软件触发、右对齐、规则组 1 个转换。

### 8.6 `HAL_ADC_Init()`

`HAL_ADC_Init(&hadc1)` 把 ADC 整体配置写入 ADC1 的控制寄存器和序列配置。

注意 STM32F1 的 ADC 校准不是自动完成的，后面还要显式调用校准函数。

### 8.7 `HAL_ADCEx_Calibration_Start()`

这个函数对应寄存器版的 `RSTCAL` 和 `CAL` 流程。

它会让 ADC 执行校准，校准完成后再返回。校准失败时 HAL 版进入 `error_handler()`。

### 8.8 `ADC_ChannelConfTypeDef` 配通道

代码：

```c
sConfig.Channel = ADC_CHANNEL_1;
sConfig.Rank = ADC_REGULAR_RANK_1;
sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
```

这对应寄存器版 `SQR3.SQ1=1` 和 `SMPR2.SMP1=111`。

### 8.9 `HAL_ADC_Start()`

它启动一次 ADC 转换。

对应寄存器版设置 `EXTTRIG/SWSTART`。如果返回不是 `HAL_OK`，说明 ADC 状态或配置异常。

### 8.10 `HAL_ADC_PollForConversion()`

它轮询等待转换完成，并带超时参数。

本课超时设置为 100ms。ADC 转换本身只有几十微秒，100ms 是故障保护边界。

### 8.11 `HAL_ADC_GetValue()`

它读取 ADC 转换结果。

底层等价于读取 `ADC1->DR`。返回 `uint32_t`，但本课有效值是 12 位。

### 8.12 `SysTick_Handler()` 和 `HAL_Delay()`

HAL 版主循环用 `HAL_Delay(20)` 控制采样间隔。`HAL_Delay()` 依赖 `SysTick_Handler()` 里的 `HAL_IncTick()`。

如果 SysTick 没工作，程序可能卡在 delay。

## 9. 两个版本真正应该怎么学

寄存器版抓住 ADC 的底层顺序：

```text
PA1 模拟输入
ADC 时钟分频
规则组通道
采样时间
上电校准
软件触发
等待 EOC
读取 DR
```

HAL 版抓住三类对象：

```text
ADC_HandleTypeDef：ADC 整体模式
ADC_ChannelConfTypeDef：通道和采样时间
Start/Poll/GetValue：轮询采样流程
```

只要你能把 HAL 的字段翻译回 `SQR/SMPR/CR2/SR/DR`，HAL 版就不再是黑箱。

## 10. 检验问题清单

### 10.1 为什么 PA1 要配置为模拟输入？

答：模拟输入模式会断开数字输入路径，让电压直接进入 ADC 采样电路。普通输入模式会经过数字输入结构，影响采样准确性。

### 10.2 ADC 值 2048 大约对应多少伏？

答：如果 VREF 是 3.3V，电压约为 `2048/4095*3.3V ≈ 1.65V`。

### 10.3 为什么 ADC 时钟要设为 12MHz？

答：PCLK2 是 72MHz，F103 ADC 不能直接用这么高的时钟。本课用 PCLK2/6 得到 12MHz，低于约 14MHz 的限制。

### 10.4 采样时间太短会怎样？

答：采样电容可能还没充到输入电压，结果会偏低或抖动。电位器这类源阻抗较高的输入更需要较长采样时间。

### 10.5 轮询方式的缺点是什么？

答：CPU 启动转换后一直等 `EOC`，等待期间不能做其他工作。转换时间越长，CPU 浪费越明显。

### 10.6 读 `DR` 后会发生什么？

答：得到 ADC 转换结果；在 F103 上读取 `DR` 后 `EOC` 会被清除，ADC 准备下一次转换。

### 10.7 HAL 版哪个 API 对应等待 `EOC`？

答：`HAL_ADC_PollForConversion()`。它底层轮询 `EOC`，并提供超时机制。

### 10.8 如果电位器接到 PA0，代码不改会怎样？

答：代码采的是 ADC1_IN1，也就是 PA1。电位器接 PA0 时，读到的不是这个电位器电压。

## 11. 工程实现步骤

### 11.1 需求分析

需求是读取 PA1 上的模拟电压，并用半量程阈值控制 PC13。

这要求 PA1 模拟输入正确、ADC1 时钟和分频正确、规则组选择通道 1、采样时间足够、ADC 校准完成、轮询读取成功。

### 11.2 硬件核查

确认电位器两端接 3.3V 和 GND，中间脚接 PA1。

确认输入电压不超过 3.3V。外部模拟源必须和 STM32 共地。

### 11.3 寄存器路线

寄存器版按这个顺序实现：

1. 配置系统时钟。
2. 配置 PC13 输出。
3. 配置 PA1 模拟输入。
4. 打开 ADC1 时钟。
5. 设置 `ADCPRE=PCLK2/6`。
6. 设置规则组长度为 1。
7. 设置 `SQ1=1`。
8. 设置通道 1 采样时间 239.5 cycles。
9. `ADON` 上电并等待稳定。
10. `RSTCAL` 复位校准。
11. `CAL` 执行校准。
12. 主循环设置 `EXTTRIG/SWSTART`。
13. 等待 `EOC`。
14. 读取 `DR` 并控制 PC13。

### 11.4 HAL 路线

HAL 版按这个顺序实现：

1. `HAL_Init()`。
2. 配置系统时钟。
3. 配置 PC13 输出。
4. 配置 PA1 为 `GPIO_MODE_ANALOG`。
5. `__HAL_RCC_ADC1_CLK_ENABLE()`。
6. `__HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6)`。
7. 填写 `hadc1.Init`。
8. 调用 `HAL_ADC_Init()`。
9. 调用 `HAL_ADCEx_Calibration_Start()`。
10. 填写 `ADC_ChannelConfTypeDef`。
11. 调用 `HAL_ADC_ConfigChannel()`。
12. 主循环 `HAL_ADC_Start()`。
13. `HAL_ADC_PollForConversion()`。
14. `HAL_ADC_GetValue()`。

### 11.5 工程思维

ADC 采样不是只“读一个引脚”。它涉及模拟输入模式、ADC 时钟、采样时间、通道序列、校准、触发和完成标志。

轮询方式适合入门和低速采样。采样频率高、CPU 还有其他任务时，应考虑中断或 DMA。

### 11.6 常见工程陷阱

第一个陷阱是 PA1 没配模拟输入。

第二个陷阱是 ADC 时钟超过规格。

第三个陷阱是电位器接错引脚或没有共地。

第四个陷阱是采样时间太短导致结果不稳。

第五个陷阱是忘记校准，导致结果有偏移。

## 12. 运行现象

旋转电位器时，`adc_value` 会在 0 到 4095 之间变化。

当 `adc_value > 2048` 时，PC13 被拉低，LED 点亮。当 `adc_value <= 2048` 时，PC13 被拉高，LED 熄灭。

如果用调试器观察，PA1 接近 0V 时值接近 0，接近 3.3V 时值接近 4095。

## 13. 常见问题排查

### 13.1 ADC 值一直是 0

检查 PA1 是否有输入电压，电位器中间脚是否接到 PA1。

再查 GPIOA 时钟、PA1 模拟输入、ADC1 时钟、规则组通道是否是 1。

### 13.2 ADC 值一直接近 4095

检查 PA1 是否被接到 3.3V，或者输入悬空被外部电路拉高。

也要确认没有把电位器两端接反或中间脚接错。

### 13.3 ADC 值抖动很大

检查模拟输入线是否太长、是否共地、供电是否稳定。

可以适当增加硬件滤波电容，或在软件里做多次平均。本课已经使用最长采样时间，适合电位器。

### 13.4 程序卡在等待转换完成

寄存器版检查 `EXTTRIG/SWSTART` 是否设置，ADC 是否上电校准完成。

HAL 版检查 `HAL_ADC_Start()` 是否成功，`HAL_ADC_PollForConversion()` 是否超时。

### 13.5 LED 阈值现象反了

PC13 板载 LED 通常低电平点亮。代码中大于 2048 时写低电平，LED 亮。

如果你外接 LED，接法不同可能导致亮灭直觉相反。

## 14. 本课最核心的结论

- ADC 把 PA1 上的模拟电压转换成 0 到 4095 的数字值。
- PA1 必须配置为模拟输入，才能正确送入 ADC1_IN1。
- ADC 时钟需要从 PCLK2 分频到规格范围内，本课是 12MHz。
- 规则组决定采哪个通道，采样时间决定采样电容有多久充电。
- 校准能减少 ADC 偏移误差。
- 轮询采样的流程是启动转换、等待 `EOC`、读取 `DR`。
- HAL 的 Start/Poll/GetValue 对应寄存器版的 SWSTART/EOC/DR。

## 15. 建议你现在怎么读这节课

先从硬件接线读起：电位器中间脚到 PA1。

然后看寄存器版 `adc1_init()`，按“时钟、序列、采样时间、上电、校准”的顺序理解。

最后看 `adc1_read_channel1()`，把启动转换、等待 EOC、读取 DR 三步背后的硬件状态想清楚。

## 16. 扩展练习

1. 把阈值 2048 改成 1024 或 3072，观察 LED 触发位置变化。
2. 把采样时间改短，观察电位器读数是否更容易抖动。
3. 在调试器中观察 `ADC1->SR`、`ADC1->DR` 和 `adc_value`。
4. 连续读取 16 次求平均，比较显示稳定性。
5. 思考下一课：如果不想 CPU 一直等待 `EOC`，应该怎样处理？

## 17. 下一课预告

上一课：[14_timer_advanced_tim1](../14_timer_advanced_tim1/README.md)

下一课：[16_adc_interrupt](../16_adc_interrupt/README.md)

下一课会把 ADC 转换完成后的等待方式从轮询改为中断。ADC 完成转换后主动通知 CPU，CPU 不必一直卡在 `while(EOC==0)` 里等待。
