# 18_dma_basic - ADC + DMA 单值采样

## 1. 本课到底在学什么

本课表面现象很简单：PA1 上的模拟电压变化后，`g_adc_value` 会被自动刷新，主循环根据它控制 PC13 LED。

真正要学的是 STM32 里一条非常重要的数据通路：

```text
PA1 模拟电压
  -> ADC1_IN1
  -> ADC1 规则组连续转换
  -> ADC1->DR 出现新结果
  -> ADC 发出 DMA 请求
  -> DMA1_Channel1 自动读取 DR
  -> 写入 RAM 变量 g_adc_value
  -> CPU 只读内存变量控制 LED
```

前几节 ADC 课里，CPU 要么轮询 `EOC`，要么进 ADC 中断后读取 `DR`。本课开始，CPU 不再亲自搬 ADC 数据。ADC 完成转换后，由 DMA 控制器在后台把数据从外设寄存器搬到内存变量。你要学会分清：ADC 负责“产生数据”，DMA 负责“搬数据”，CPU 负责“消费已经在内存里的数据”。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 说清楚为什么 ADC1 在 STM32F103 上使用 `DMA1_Channel1`。
- 解释 `CPAR`、`CMAR`、`CNDTR`、`CCR` 分别决定 DMA 的哪一部分行为。
- 解释 `ADC_CR2_CONT` 和 `ADC_CR2_DMA` 为什么要同时出现。
- 说清楚为什么 DMA 要先使能，ADC 再 `SWSTART`。
- 看懂 HAL 版里 `DMA_HandleTypeDef` 每个字段对应 DMA 哪些寄存器位。
- 看懂 `__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)` 为什么不是装饰代码。
- 根据 LED 不变化、变量不更新、数值异常等现象反推可能错在哪一层。

## 3. 本课目录结构

```text
18_dma_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/src/main.c` 把 ADC 和 DMA 的寄存器配置摊开写；`hal/src/main.c` 使用 HAL 句柄和 API 完成同一件事。两个版本的最终目标一样：让 ADC1 连续采样 PA1，并让 DMA 自动把最新结果写入内存。

## 4. 实验硬件

本课使用 STM32F103C8T6 BluePill，工程板卡是 `genericSTM32F103C8`。时钟假设为外部 8MHz HSE，经 PLL 配到 72MHz。

引脚和观察点如下：

- `PA1`：模拟输入，对应 `ADC1_IN1`。可以接电位器中间端，电位器两端接 `3.3V` 和 `GND`。
- `PC13`：板载 LED 输出。BluePill 上通常是低电平点亮。
- `ST-Link`：下载和调试。

注意：PA1 输入电压不能超过芯片供电范围。ADC 只能测 0 到 VDDA 附近的电压，不能直接接 5V。

## 5. 先建立完整脑图

本课从现象到底层，可以拆成六层：

1. 现象层：转动电位器，`g_adc_value` 自动变化，超过 2048 时 LED 状态改变。
2. 物理层：PA1 上的模拟电压进入芯片 ADC 输入通道，PC13 输出电平驱动 LED。
3. 芯片模块层：GPIOA 提供模拟输入脚，ADC1 完成模数转换，DMA1_Channel1 搬运 ADC 数据，GPIOC 控制 LED。
4. 寄存器层：`ADC1->DR` 保存转换结果，`ADC1->CR2.DMA` 允许 ADC 发 DMA 请求，`DMA1_Channel1->CPAR/CMAR/CNDTR/CCR` 决定搬运路径。
5. C/CMSIS 层：`ADC1`、`DMA1_Channel1`、`RCC->AHBENR`、`volatile g_adc_value` 都是 C 代码访问硬件的入口。
6. HAL 工程层：`ADC_HandleTypeDef`、`DMA_HandleTypeDef`、`HAL_DMA_Init()`、`HAL_ADC_Start_DMA()` 把这些寄存器动作封装成结构体字段和函数调用。

代码顺序也来自这张脑图：先配系统时钟，再配 LED，再配 PA1 模拟输入，再配 DMA 通道，再配 ADC 连续转换和 DMA 请求，最后先启动 DMA、再启动 ADC。

## 6. 核心名词解释

### 6.1 `DMA` 是什么

`DMA` 是 Direct Memory Access，中文常叫直接存储器访问。它属于芯片内部的硬件控制器，不属于 GPIO、ADC 这种单一外设，也不是 C 语言函数。

它控制的是“数据搬运行为”：从哪个地址读、写到哪个地址、搬多少个数据、每次地址是否自增、搬完后停止还是循环。本课里，DMA 不改变 ADC 的采样精度，也不决定 PA1 的电压；它只负责把 `ADC1->DR` 里的结果搬到 `g_adc_value`。

它出现在本课，是因为 ADC 会不断产生数据。如果 CPU 每次都轮询或进中断读取，CPU 时间会被频繁打断。DMA 让“外设数据进入内存”这件事由硬件完成。配置错时，常见现象是 `g_adc_value` 一直为 0、保持旧值、LED 不随电压变化，或者内存变量被错误写坏。

### 6.2 `DMA1_Channel1` 是什么

`DMA1_Channel1` 是 DMA1 控制器的第 1 个通道，属于芯片 DMA 模块。STM32F103 的 DMA 请求和通道有固定映射，ADC1 的 DMA 请求固定接到 DMA1_Channel1。

它控制的是本课 ADC 数据的搬运通道。ADC1 完成一次转换后，只有对应通道使能并配置正确，DMA 才会响应这次请求。如果误用其他通道，代码可能仍能编译，但 ADC 请求到不了那个通道，`g_adc_value` 不会更新。

在寄存器版中它出现在 `dma1_channel1_init()`；HAL 版中对应 `hdma_adc1.Instance = DMA1_Channel1`。

### 6.3 `CPAR` 是什么

`CPAR` 是 DMA Channel Peripheral Address Register，外设地址寄存器，属于 DMA 通道寄存器。

它保存 DMA 外设端的地址。本课方向是外设到内存，所以 `CPAR = (uint32_t)&ADC1->DR`，意思是每次从 ADC 数据寄存器读取。注意这里取的是 `DR` 的地址，不是 `DR` 当前的数值。

如果 `CPAR` 写错，DMA 会从错误地址取数。轻则变量一直不对，重则访问了不该访问的外设地址，调试时表现为结果随机或完全不变化。

### 6.4 `CMAR` 是什么

`CMAR` 是 DMA Channel Memory Address Register，内存地址寄存器，属于 DMA 通道寄存器。

它保存 DMA 内存端的地址。本课写成 `CMAR = (uint32_t)&g_adc_value`，表示把 ADC 结果写入这个 RAM 变量。因为 `g_adc_value` 只保存最新一次结果，所以本课没有打开内存地址自增。

如果 `CMAR` 写错，DMA 会把 ADC 数据写到错误 RAM 位置，可能导致变量不更新、其他变量被改坏，甚至程序跑飞。

### 6.5 `CNDTR` 是什么

`CNDTR` 是 DMA Channel Number of Data Register，传输数量寄存器，属于 DMA 通道寄存器。

它决定一轮 DMA 要搬多少个数据单元。本课只需要一个“最新 ADC 值”，所以 `CNDTR = 1U`。每完成一次传输，硬件会让计数递减；因为打开了循环模式，减到 0 后会重新装载初始值继续工作。

如果 `CNDTR` 太小，数据不够；如果太大而内存没有对应空间，就可能越界写内存。本课长度为 1，正好匹配单个变量。

### 6.6 `CCR` 是什么

这里的 `CCR` 是 DMA Channel Configuration Register，通道配置寄存器，属于 DMA 通道。它不是定时器的捕获比较寄存器，名字相同但上下文不同。

它控制 DMA 的主要行为：是否使能、方向、循环模式、外设地址是否自增、内存地址是否自增、外设数据宽度、内存数据宽度、优先级等。本课设置 `CIRC=1`、`PSIZE=16bit`、`MSIZE=16bit`、`PL=高`，并保持 `DIR=0`、`PINC=0`、`MINC=0`。

如果 `CCR` 配错，现象非常直接：方向错会搬反，宽度错会数据错位，循环模式没开会只更新一次，通道没使能则完全不搬。

### 6.7 `DMA_CCR_CIRC` 是什么

`DMA_CCR_CIRC` 是 DMA 循环模式位，对应 `CCR.CIRC`。

本课 ADC 是连续转换，DMA 也必须连续接收结果。打开循环模式后，一轮传输完成不会停止，而是重新装载 `CNDTR` 继续响应下一次 ADC 请求。

如果不打开它，`g_adc_value` 通常只更新第一次，之后转动电位器 LED 不再随之变化。这是 ADC+DMA 初学时很容易误判成 ADC 没工作的点。

### 6.8 `DMA_CCR_PSIZE` 和 `DMA_CCR_MSIZE` 是什么

`PSIZE` 是外设端数据宽度，`MSIZE` 是内存端数据宽度，都属于 DMA `CCR`。

STM32F103 ADC 结果是 12 位，但存放在 16 位数据寄存器有效位里，所以本课按半字 16 位搬运。寄存器版设置 `DMA_CCR_PSIZE_0` 和 `DMA_CCR_MSIZE_0`；HAL 版对应 `DMA_PDATAALIGN_HALFWORD` 和 `DMA_MDATAALIGN_HALFWORD`。

如果宽度设成 8 位，结果会被截断；如果内存变量和 DMA 宽度不匹配，可能出现数值异常或相邻内存被影响。

### 6.9 `DMA_CCR_MINC` 和 `DMA_CCR_PINC` 是什么

`MINC` 控制内存地址自增，`PINC` 控制外设地址自增，属于 DMA `CCR`。

本课源地址始终是 `ADC1->DR`，目标地址始终是 `g_adc_value`，所以两个自增都不打开。下一类“数组缓冲区”实验会打开 `MINC`，让 DMA 依次写入数组不同元素。

如果本课误开 `MINC`，DMA 第一次写 `g_adc_value`，后面会写到后续地址，变量看起来可能只变化一次，还可能破坏别的内存。

### 6.10 `ADC_CR2_CONT` 是什么

`ADC_CR2_CONT` 是 ADC 连续转换模式位，属于 ADC1 的 `CR2` 寄存器。

它控制 ADC 转换是否自动接续。`CONT=1` 后，只要第一次软件启动，ADC 转完一次会自动开始下一次。它解决的是“ADC 是否持续产生数据”的问题，不解决“数据怎么进内存”的问题。

如果不打开 `CONT`，ADC 只转换一次，DMA 也只能搬一次，`g_adc_value` 后续不会持续刷新。

### 6.11 `ADC_CR2_DMA` 是什么

`ADC_CR2_DMA` 是 ADC 的 DMA 请求使能位，属于 ADC1 的 `CR2` 寄存器。

它控制 ADC 完成转换后是否向 DMA 控制器发请求。本课必须打开它，否则 DMA 通道即使配置正确，也收不到 ADC 的搬运触发。

这个位是 ADC 和 DMA 之间的“握手开关”。如果没设，常见现象是 ADC 自己可能在转换，但 `g_adc_value` 不更新。

### 6.12 `ADC1->DR` 是什么

`DR` 是 ADC Data Register，数据寄存器，属于 ADC1。

每次规则组转换完成后，ADC 把转换结果放到 `DR`。在轮询和中断课里，CPU 读 `DR`；本课里，DMA 读 `DR`。这说明数据源没有变，只是读取者从 CPU 换成了 DMA。

如果 `CPAR` 没有指向 `&ADC1->DR`，DMA 就拿不到真正的 ADC 结果。

### 6.13 `volatile g_adc_value` 是什么

`g_adc_value` 是 RAM 中的全局变量，`volatile` 是 C 语言层面的限定符。

它不是寄存器，但它会被 DMA 硬件异步修改。`volatile` 告诉编译器：每次主循环读取这个变量时都要从内存重新取，不要假设它在 C 代码没有赋值时就不会变化。

如果去掉 `volatile`，优化级别较高时主循环可能读到缓存值，表现为 DMA 实际已经写内存，但 C 代码看起来像没更新。

### 6.14 `HAL_DMA_Init` 是什么

`HAL_DMA_Init()` 是 HAL 的 DMA 初始化函数，属于 HAL 工程层。

它读取 `DMA_HandleTypeDef.Init` 中的方向、自增、宽度、模式、优先级等字段，然后写 DMA 通道的 `CCR` 等寄存器。本课它不负责启动 ADC，也不负责设置 ADC 通道；它只把 DMA 通道本身准备好。

如果字段填错，HAL 仍然会认真把错误配置写到底层寄存器里。HAL 不是防止你理解硬件的屏障，它只是换了一种表达方式。

### 6.15 `__HAL_LINKDMA` 是什么

`__HAL_LINKDMA()` 是 HAL 的句柄关联宏，属于 HAL 工程层。

本课写的是 `__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)`，含义是把 ADC 句柄里的 `DMA_Handle` 成员指向 `hdma_adc1`。之后 `HAL_ADC_Start_DMA()` 才知道要使用哪个 DMA 通道。

如果缺少这一步，ADC 句柄和 DMA 句柄各自存在但没有关系，HAL 启动 DMA 时找不到对应通道，可能返回错误或无法完成启动。

### 6.16 `HAL_ADC_Start_DMA` 是什么

`HAL_ADC_Start_DMA()` 是 HAL 启动 ADC+DMA 的函数，属于 HAL 工程层。

它接收 ADC 句柄、目标内存地址和长度。本课调用 `HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&g_adc_value, 1U)`，等价于告诉 HAL：用 ADC1，借助关联的 DMA1_Channel1，把 ADC 结果写入 `g_adc_value`，长度为 1。

它对应寄存器版的组合动作：配置 DMA 地址和计数、使能 DMA 通道、打开 ADC DMA 请求、启动 ADC 软件转换。参数地址或长度错时，底层搬运目标就会错。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟为什么先配

`system_clock_72mhz_init()` 先配置 Flash 等待周期，再打开 HSE，等待 `HSERDY`，然后配置 PLL 到 72MHz。

ADC 时钟来自 PCLK2 分频，软件延时也依赖 CPU 频率。先把系统时钟定下来，后面的 ADC 分频、串行时序和延时才有稳定基准。

### 7.2 PC13 LED 初始化

`led_pc13_init()` 先设置 `RCC->APB2ENR |= RCC_APB2ENR_IOPCEN`，让 GPIOC 获得时钟。

然后清 `GPIOC->CRH` 中的 `MODE13/CNF13`，再设置 `GPIO_CRH_MODE13_1`，使 PC13 成为 2MHz 通用推挽输出。最后写 `GPIOC->BSRR = GPIO_BSRR_BS13`，让 LED 初始熄灭。PC13 是本课现象层输出，用于观察 ADC+DMA 结果是否进入主循环判断。

### 7.3 PA1 配成模拟输入

`pa1_adc_input_init()` 打开 GPIOA 时钟，然后清 `GPIOA->CRL` 中的 `MODE1/CNF1`。

在 STM32F1 里，GPIO 模式为 `MODE=00, CNF=00` 表示模拟输入。PA1 对应 ADC1_IN1。只有先把引脚数字输入输出缓冲关到模拟模式，ADC 才能稳定采样这一路电压。

### 7.4 DMA1 时钟来自 AHB

`dma1_channel1_init()` 的第一句是：

```c
RCC->AHBENR |= RCC_AHBENR_DMA1EN;
```

DMA1 挂在 AHB 总线上，所以使能位在 `AHBENR`，不是 GPIO/ADC 常用的 `APB2ENR`。如果漏掉这一句，后面写 DMA 通道寄存器不会让 DMA 硬件真正工作。

### 7.5 配置 DMA 前先关闭通道

```c
DMA1_Channel1->CCR &= ~DMA_CCR_EN;
```

DMA 通道使能时，部分配置寄存器不应该被随意改。先清 `EN`，让通道停在可配置状态，再写 `CNDTR/CPAR/CMAR/CCR`。如果运行中改配置，可能出现写入无效或本轮传输状态混乱。

### 7.6 `CNDTR = 1`

```c
DMA1_Channel1->CNDTR = 1U;
```

本课目标不是保存一段波形数组，而是保存“最新一个 ADC 值”。所以一轮 DMA 只搬 1 个半字。配合循环模式，每次 ADC 请求来时，这个 1 会被反复装载使用。

### 7.7 `CPAR` 指向 ADC 数据寄存器

```c
DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
```

这句决定 DMA 从哪里读。`&ADC1->DR` 是 ADC 数据寄存器地址。ADC 每次转换完成后，新结果出现在这里，DMA 响应请求时从这里取走。

### 7.8 `CMAR` 指向内存变量

```c
DMA1_Channel1->CMAR = (uint32_t)&g_adc_value;
```

这句决定 DMA 写到哪里。目标是 RAM 中的 `g_adc_value`。主循环不读 `ADC1->DR`，只读这个变量，因为 DMA 会持续刷新它。

### 7.9 `CCR` 清位再设位

代码先清掉 `MEM2MEM/PL/MSIZE/PSIZE/MINC/PINC/CIRC/DIR` 等位，再重新设置本课需要的值。

本课方向保持 `DIR=0`，表示外设到内存；`PINC=0`，因为外设地址一直是 `ADC1->DR`；`MINC=0`，因为目标一直是同一个变量；`CIRC=1`，因为 ADC 连续转换；`PSIZE/MSIZE=16bit`，因为 ADC 结果按半字搬运；`PL=高`，让 ADC 数据搬运优先级更高。

### 7.10 ADC 时钟和 ADC 分频

`adc1_init()` 打开 `RCC_APB2ENR_ADC1EN`，再设置：

```c
RCC->CFGR &= ~RCC_CFGR_ADCPRE;
RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
```

系统 PCLK2 是 72MHz，ADC 时钟不能太高，除以 6 后是 12MHz，处在 STM32F103 ADC 可用范围内。ADC 时钟不合适会导致采样不稳定或转换结果不可靠。

### 7.11 ADC 规则组选择通道 1

`ADC1->SQR1` 清 `L`，表示规则序列长度为 1；`ADC1->SQR3` 设置第一个转换槽为通道 1，即 `ADC1_IN1`。

这一步把“PA1 这根脚”连接到“规则组第一次转换”。如果序列通道写错，ADC 会采样别的通道，表现为电位器怎么转都不对应。

### 7.12 采样时间设置

`ADC1->SMPR2` 中通道 1 的采样时间被设为较长采样周期。

采样时间属于 ADC 模拟前端行为：采样电容需要时间充到接近输入电压。电位器阻抗较高时，采样时间太短会导致读数偏差或跳动。

### 7.13 `CONT` 和 `DMA` 是 ADC+DMA 的核心组合

```c
ADC1->CR2 |= ADC_CR2_CONT;
ADC1->CR2 |= ADC_CR2_DMA;
```

`CONT` 让 ADC 持续产生转换结果；`DMA` 让每次结果产生后通知 DMA 搬运。一个负责“持续生产”，一个负责“请求搬运”。少任何一个，自动刷新链路都不完整。

### 7.14 ADC 上电和校准

代码设置 `ADON`，短暂延时，然后执行 `RSTCAL` 和 `CAL`，并等待硬件清位。

STM32F1 ADC 使用前需要校准。校准属于模拟模块自身的准备动作，目的是减小偏差。没有校准时程序可能能跑，但数值精度和稳定性会变差。

### 7.15 启动顺序：先 DMA 后 ADC

`adc1_dma_start()` 先执行：

```c
DMA1_Channel1->CCR |= DMA_CCR_EN;
```

再执行：

```c
ADC1->CR2 |= ADC_CR2_EXTTRIG | ADC_CR2_SWSTART;
```

先让 DMA 通道进入响应状态，再让 ADC 开始转换。这样第一笔 ADC 数据出来时，DMA 已经准备好接收请求。顺序反了，第一次转换可能已经完成但 DMA 还没使能，第一笔数据会丢失。

### 7.16 主循环为什么只读变量

主循环只判断：

```c
if (g_adc_value > 2048U)
```

这里没有 `EOC` 轮询，也没有读 `ADC1->DR`。这正是本课重点：CPU 的视角从“等待外设结果”变成“读取已经被 DMA 刷新的内存”。阈值 2048 对应 12 位 ADC 的中间值，约等于 VDDA 的一半。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()`

`HAL_Init()` 准备 HAL 基础状态和 SysTick。它不配置 ADC，也不配置 DMA，但 HAL 延时、超时和部分状态机依赖它。

### 8.2 HAL 时钟配置

`HAL_RCC_OscConfig()` 通过 `RCC_OscInitTypeDef` 选择 HSE、PLL 源和倍频；`HAL_RCC_ClockConfig()` 通过 `RCC_ClkInitTypeDef` 配置 SYSCLK、HCLK、PCLK1、PCLK2 和 Flash 延迟。

这对应寄存器版的 `FLASH->ACR`、`RCC->CR`、`RCC->CFGR` 配置。

### 8.3 HAL GPIO 配置

PC13 使用 `GPIO_MODE_OUTPUT_PP`，对应 GPIO 推挽输出；PA1 使用 `GPIO_MODE_ANALOG`，对应 F1 中 `MODE1=00, CNF1=00`。

HAL 结构体字段只是把寄存器位组合变成更可读的枚举。

### 8.4 `hdma_adc1.Instance`

```c
hdma_adc1.Instance = DMA1_Channel1;
```

这句选择具体硬件通道。ADC1 的 DMA 请求映射到 DMA1_Channel1，所以 HAL 版也必须选它。选错通道时，HAL 初始化可能成功，但 ADC 请求不会触发正确通道搬运。

### 8.5 `Direction`

```c
hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
```

它对应寄存器版 `CCR.DIR = 0`。本课数据从 `ADC1->DR` 进入 RAM 变量，所以方向是外设到内存。

### 8.6 `PeriphInc` 和 `MemInc`

`DMA_PINC_DISABLE` 对应 `PINC=0`，因为外设地址固定为 `ADC1->DR`。

`DMA_MINC_DISABLE` 对应 `MINC=0`，因为内存目标固定为一个变量。本课不是数组采样，所以内存地址不自增。

### 8.7 数据宽度字段

```c
hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
```

这对应 `PSIZE=01`、`MSIZE=01`。ADC 结果是 12 位，按 16 位半字搬运，和 `uint16_t`/低 16 位存储匹配。

### 8.8 `Mode = DMA_CIRCULAR`

`DMA_CIRCULAR` 对应 `CCR.CIRC=1`。ADC 连续转换时，DMA 必须循环接收结果。否则只会搬一次，变量不会持续刷新。

### 8.9 `HAL_DMA_Init()`

`HAL_DMA_Init(&hdma_adc1)` 根据上面字段写 DMA 通道配置寄存器。它做的是“准备 DMA 通道”，还不是启动 ADC+DMA。

如果它返回错误，说明句柄或参数状态不对，本课进入 `error_handler()`。

### 8.10 `__HAL_LINKDMA`

```c
__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
```

这句把 `hadc1.DMA_Handle` 指向 `hdma_adc1`。没有这个关联，`HAL_ADC_Start_DMA()` 只知道 ADC 句柄，不知道该操作哪个 DMA 通道。

### 8.11 ADC 初始化字段

`hadc1.Instance = ADC1` 选择 ADC1。`ScanConvMode = ADC_SCAN_DISABLE` 表示单通道，不扫描多通道。`ContinuousConvMode = ENABLE` 对应 `CR2.CONT=1`。`ExternalTrigConv = ADC_SOFTWARE_START` 对应软件触发。`DataAlign = ADC_DATAALIGN_RIGHT` 表示右对齐。`NbrOfConversion = 1` 对应规则序列长度 1。

这些字段合起来描述“ADC1、单通道、软件启动、连续转换、一个规则转换”。

### 8.12 ADC 校准和通道配置

`HAL_ADCEx_Calibration_Start(&hadc1)` 对应寄存器版 `RSTCAL/CAL`。

`HAL_ADC_ConfigChannel()` 中 `Channel = ADC_CHANNEL_1` 对应 PA1/ADC1_IN1，`Rank = ADC_REGULAR_RANK_1` 对应规则序列第一个位置，`SamplingTime = ADC_SAMPLETIME_239CYCLES_5` 对应较长采样时间。

### 8.13 `HAL_ADC_Start_DMA`

```c
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&g_adc_value, 1U)
```

第一个参数选择 ADC 句柄，第二个参数给 DMA 目标内存地址，第三个参数给传输长度。它内部会借助 `hadc1.DMA_Handle` 找到 DMA1_Channel1，启动 DMA，再启动 ADC。

本课长度是 1，所以效果和寄存器版 `CNDTR=1, CMAR=&g_adc_value` 对齐。

## 9. 两个版本真正应该怎么学

寄存器版让你看到每个硬件开关：DMA 时钟、通道关闭、地址、计数、方向、宽度、循环、ADC DMA 请求、ADC 软件启动。

HAL 版让你看到工程表达：句柄选择实例，结构体字段描述硬件行为，`HAL_DMA_Init()` 写配置，`__HAL_LINKDMA()` 建立外设和 DMA 的关系，`HAL_ADC_Start_DMA()` 启动整条链路。

学这课时不要背 API。先画出“ADC1->DR 到 g_adc_value”的搬运路径，再把每个寄存器位和 HAL 字段放回路径上。

## 10. 检验问题清单

### 10.1 为什么 ADC1 使用 `DMA1_Channel1`

因为 STM32F103 内部的 DMA 请求映射是固定的，ADC1 的 DMA 请求连接到 DMA1_Channel1。软件不能随便把 ADC1 请求改接到别的普通通道。

### 10.2 `CPAR` 和 `CMAR` 分别保存什么

`CPAR` 保存外设端地址，本课是 `&ADC1->DR`。`CMAR` 保存内存端地址，本课是 `&g_adc_value`。

### 10.3 为什么本课 `CNDTR` 是 1

因为本课只保存最新一个 ADC 采样结果，不保存数组。循环模式会让这个长度为 1 的搬运反复发生。

### 10.4 为什么要打开 `ADC_CR2_DMA`

因为 DMA 搬运需要 ADC 发请求触发。不开这个位，ADC 即使转换完成，也不会通知 DMA 读取 `DR`。

### 10.5 为什么要打开 `ADC_CR2_CONT`

因为本课希望 ADC 持续采样。不开连续转换时，ADC 只响应一次软件启动，DMA 也只能搬一次。

### 10.6 为什么 `g_adc_value` 要加 `volatile`

因为它会被 DMA 硬件在 C 代码执行流之外修改。`volatile` 防止编译器把主循环中的读取优化成旧值。

### 10.7 HAL 版里 `__HAL_LINKDMA` 少了会怎样

ADC 句柄找不到对应 DMA 句柄，`HAL_ADC_Start_DMA()` 无法正确操作 DMA 通道，常见结果是启动失败或变量不更新。

### 10.8 如果 LED 不随电位器变化，先查哪几层

先查 PA1 是否接入 0 到 3.3V 模拟电压，再查 GPIOA 模拟模式、ADC 通道是否为 1、`CR2.DMA/CONT` 是否打开、DMA1_Channel1 的 `CPAR/CMAR/CNDTR/CCR` 是否正确，最后查主循环阈值和 PC13 低电平点亮逻辑。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求是让 ADC1 连续采样 PA1，并用 DMA 把每次转换结果自动写进一个变量。CPU 不轮询 `EOC`，不进入 ADC 中断，不手动读取 `ADC1->DR`。

### 11.2 硬件核查

确认板卡是 STM32F103C8T6 BluePill，PA1 接模拟电压，电压范围不超过 3.3V，PC13 LED 可用。若用电位器，电位器两端接 3.3V/GND，中间端接 PA1。

### 11.3 寄存器路线

进入 `18_dma_basic/reg`，先读 `system_clock_72mhz_init()`、`led_pc13_init()`、`pa1_adc_input_init()`，再读 `dma1_channel1_init()` 和 `adc1_init()`。重点确认 `CPAR=&ADC1->DR`、`CMAR=&g_adc_value`、`CNDTR=1`、`CIRC=1`、`ADC_CR2_DMA`、`ADC_CR2_CONT`。

编译命令：

```sh
pio run
```

下载命令：

```sh
pio run -t upload
```

### 11.4 HAL 路线

进入 `18_dma_basic/hal`，重点读 `dma1_channel1_init()`、`adc1_init()` 和 `HAL_ADC_Start_DMA()`。把 `DMA_HandleTypeDef.Init` 字段逐个对应到 DMA `CCR`，把 ADC 初始化字段对应到 ADC `CR2/SQR/SMPR`。

### 11.5 工程思维

DMA 调试时不要只盯 CPU 代码。要同时看三件事：外设有没有产生请求，DMA 通道有没有响应请求，内存目标是否正确。ADC+DMA 的本质是两个硬件模块之间协作，CPU 只是配置者和结果消费者。

### 11.6 常见工程陷阱

DMA 通道选错、忘记开 `ADC_CR2_DMA`、忘记循环模式、目标变量没加 `volatile`、宽度设置不匹配、启动顺序反了，都会让程序看起来“能跑但数据不动”。这类问题要按数据路径逐层排查。

## 12. 运行现象

正常情况下，PA1 电压低于约 VDDA/2 时，`g_adc_value` 小于 2048，PC13 保持熄灭；PA1 电压高于约 VDDA/2 时，`g_adc_value` 大于 2048，PC13 点亮。

用调试器观察 `g_adc_value`，它应该在不暂停 CPU 的情况下持续被 DMA 更新。主循环里看不到读 `ADC1->DR` 的语句，这是本课正确现象的一部分。

## 13. 常见问题排查

### 13.1 `g_adc_value` 一直是 0

先查 PA1 是否真的有电压，再查 GPIOA 时钟和 PA1 模拟模式。然后查 ADC1 时钟、通道号是否为 `ADC_CHANNEL_1` 或 `SQR3=1`。最后查 DMA 是否使能、`ADC_CR2_DMA` 是否打开。

### 13.2 `g_adc_value` 只变化一次

重点查 `DMA_CCR_CIRC` 和 `ADC_CR2_CONT`。少了循环模式，DMA 一轮结束后停止；少了连续转换，ADC 只产生一次结果。

### 13.3 数值变化但 LED 逻辑反了

BluePill PC13 LED 通常低电平点亮。寄存器版写 `BRR` 是拉低点亮，写 `BSRR` 是拉高熄灭。先确认板子 LED 极性，再看阈值判断分支。

### 13.4 数值异常跳动

检查 PA1 输入是否悬空，电位器 GND 是否和开发板共地，VDDA 是否稳定。再检查采样时间是否足够，ADC 时钟是否为 PCLK2/6 附近。

### 13.5 HAL 版启动后不更新或程序异常

检查 `__HAL_LINKDMA()` 是否在 `HAL_ADC_Start_DMA()` 前执行，`hdma_adc1.Instance` 是否为 `DMA1_Channel1`，`HAL_ADC_Start_DMA()` 的目标地址和长度是否正确。

如果还出现跑飞或其他变量异常，继续查 `CMAR` 目标地址、`CNDTR` 长度、`MSIZE` 和内存变量类型是否匹配。DMA 写错地址会直接破坏 RAM，不一定表现为清晰错误。

## 14. 本课最核心的结论

1. ADC+DMA 的核心不是“多一个库函数”，而是把 ADC 数据读取者从 CPU 换成 DMA 硬件。
2. `ADC_CR2_CONT` 让 ADC 持续产生数据，`ADC_CR2_DMA` 让 ADC 完成转换后请求 DMA 搬运。
3. `CPAR` 决定从哪里读，`CMAR` 决定写到哪里，`CNDTR` 决定搬多少，`CCR` 决定怎么搬。
4. STM32F103 的 ADC1 DMA 请求固定映射到 `DMA1_Channel1`，通道选择不是随便写的。
5. 单变量接收时不打开内存自增；数组接收时才需要考虑 `MINC`。
6. HAL 版的 `HAL_ADC_Start_DMA()` 依赖 `__HAL_LINKDMA()` 建立 ADC 句柄和 DMA 句柄的关系。
7. 排查 ADC+DMA 时要沿着“模拟输入 -> ADC 转换 -> DMA 请求 -> DMA 通道 -> RAM 变量 -> LED 判断”逐层看。

## 15. 建议你现在怎么读这节课

第一遍先看第 5 章脑图，确认 ADC 和 DMA 分工。第二遍读寄存器版 `dma1_channel1_init()`，把 `CPAR/CMAR/CNDTR/CCR` 写在纸上。第三遍读 HAL 版，把每个 `hdma_adc1.Init` 字段对应回 DMA 寄存器。第四遍上板观察 `g_adc_value`，故意关掉 `CIRC` 或 `CONT` 看现象如何变化。

## 16. 扩展练习

- 把阈值 `2048U` 改成 `1024U` 或 `3072U`，观察 LED 翻转点。
- 去掉 `DMA_CCR_CIRC`，观察变量是否只更新一次。
- 把 `MINC` 打开，观察单变量接收为什么会出问题。
- 把 HAL 版 `DMA_CIRCULAR` 改成 `DMA_NORMAL`，对照寄存器版现象。
- 用调试器同时观察 `ADC1->DR` 和 `g_adc_value`。

## 17. 下一课预告

上一课：[17_adc_multichannel_scan](../17_adc_multichannel_scan/README.md)

下一课：[19_dma_memory_uart_cases](../19_dma_memory_uart_cases/README.md)
