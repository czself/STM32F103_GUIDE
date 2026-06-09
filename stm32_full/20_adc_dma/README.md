# 20_adc_dma - ADC + DMA 循环缓冲区

## 1. 本课到底在学什么

本课表面现象是：PA1 的模拟电压被 ADC1 连续采样，DMA 自动把采样结果写入 16 个元素的数组，主循环对数组求平均后控制 PC13 LED。

真正要学的是这条数据采集链路：

```text
PA1 模拟电压
  -> ADC1_IN1 连续转换
  -> ADC1->DR 产生 12 位结果
  -> ADC1 发 DMA 请求
  -> DMA1_Channel1 读取 DR
  -> 依次写入 g_adc_buffer[0..15]
  -> CIRC 让 DMA 回到数组开头继续覆盖
  -> CPU 对数组求平均
  -> 平均值超过 2048 时点亮 PC13
```

上一课 `18_dma_basic` 只把 ADC 结果写进一个变量，适合保存“最新值”。本课把目标换成数组，适合保存“一小段历史采样”。关键变化是 `MINC=1` 和 `CNDTR=16`：DMA 每搬一次，内存地址自动移动到下一个数组元素；搬完 16 个后，由循环模式回到数组开头。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 解释为什么本课的 DMA 目标从单变量变成数组。
- 说清楚 `MINC=1`、`CNDTR=16`、`CIRC=1` 三者怎样组成循环缓冲区。
- 看懂 `g_adc_buffer` 为什么必须是 `volatile`。
- 解释为什么 ADC 结果仍然来自 `ADC1->DR`，只是写入位置发生变化。
- 理解 `adc_buffer_average_get()` 为什么用 `uint32_t` 求和。
- 把 HAL 版 `MemInc = DMA_MINC_ENABLE`、`HAL_ADC_Start_DMA(..., ADC_BUFFER_SIZE)` 对应回寄存器版。
- 根据“数组只有第 0 个变化”“LED 抖动”“数值不更新”等现象定位错误层级。

## 3. 本课目录结构

```text
20_adc_dma/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/src/main.c` 直接配置 DMA1_Channel1、ADC1 和数组缓冲区；`hal/src/main.c` 用 `ADC_HandleTypeDef`、`DMA_HandleTypeDef` 和 `HAL_ADC_Start_DMA()` 完成同一条采样链路。

## 4. 实验硬件

本课使用 STM32F103C8T6 BluePill，PlatformIO 板卡为 `genericSTM32F103C8`，外部 8MHz HSE 配到 72MHz。

- `PA1`：模拟输入，对应 `ADC1_IN1`，建议接电位器中间端。
- `PC13`：板载 LED，通常低电平点亮。
- `ST-Link`：下载和调试。

PA1 输入电压应在 0 到 3.3V 范围内，不能直接接 5V。观察时可以在调试器里看 `g_adc_buffer` 和平均值变化。

## 5. 先建立完整脑图

本课按六层理解：

1. 现象层：转动电位器，16 个采样值持续刷新，平均值超过阈值后 LED 点亮。
2. 物理层：PA1 输入模拟电压，PC13 输出电平驱动 LED。
3. 芯片模块层：GPIOA 提供 ADC 输入脚，ADC1 连续转换，DMA1_Channel1 写数组，GPIOC 控制 LED。
4. 寄存器层：`ADC1->DR` 是数据源，`DMA1_Channel1->CMAR` 指向数组首地址，`CNDTR=16` 决定长度，`CCR.MINC/CIRC` 决定循环写入。
5. C/CMSIS 层：`g_adc_buffer[16]` 是 DMA 写入的 RAM 缓冲区，`adc_buffer_average_get()` 是 CPU 的软件处理。
6. HAL 工程层：`DMA_MINC_ENABLE`、`DMA_CIRCULAR`、`HAL_ADC_Start_DMA()` 的长度参数把数组采样意图表达给 HAL。

顺序不能乱：先配系统时钟和 GPIO，再配 DMA 地址、长度、宽度、自增和循环，再配 ADC 连续转换和 DMA 请求，最后先开 DMA、再启动 ADC。

## 6. 核心名词解释

### 6.1 `ADC_BUFFER_SIZE` 是什么

`ADC_BUFFER_SIZE` 是 C 代码中的缓冲区长度宏，本课值为 16。

它决定 `g_adc_buffer` 有多少个元素，也决定 DMA 的 `CNDTR` 初值。它属于 C/CMSIS 层，但会直接影响 DMA 硬件搬运次数。长度写错时，轻则平均值样本数不对，重则 DMA 越界写内存。

这个宏在 `reg/src/main.c` 和 `hal/src/main.c` 中同时出现，位置虽然是 C 预处理层，但意义会一路传到硬件层。寄存器版用它写 `DMA1_Channel1->CNDTR`，HAL 版把它作为 `HAL_ADC_Start_DMA()` 的第三个参数。也就是说，学生看到的“数组有 16 个点”，不是纯软件决定的，DMA 控制器也必须知道这一轮要搬 16 个数据单元。

这里尤其要注意单位。`ADC_BUFFER_SIZE=16` 表示 16 个 `uint16_t` 元素，不是 16 字节。因为本课 `PSIZE/MSIZE` 都配置成半字，所以 DMA 实际覆盖的内存范围是 32 字节。若以后把数组改成 32 个元素，却忘了同步修改 DMA 长度，后半个数组永远不会被硬件写入；若 DMA 长度大于数组长度，硬件会继续往数组后面的 RAM 写，破坏别的变量。

### 6.2 `g_adc_buffer` 是什么

`g_adc_buffer` 是 RAM 中的 ADC 采样数组，类型是 `volatile uint16_t[16]`。

DMA 在后台持续写它，CPU 在主循环读取它求平均。它是本课“硬件采集”和“软件处理”的交界点。如果不加 `volatile`，编译器可能优化 CPU 读取，导致调试现象和实际 DMA 写入不一致。

它属于 C 语言对象，同时又是 DMA 硬件的目标内存。CPU 没有在循环里执行“把 ADC 值放进数组”的语句，数组变化来自 DMA 总线主机直接写 SRAM。这个点非常重要：`g_adc_buffer` 是你第一次明显看到“不是 CPU 写变量，变量也会变”的课程。

`volatile` 不是让 DMA 工作的开关。DMA 是否工作由 `DMA1_Channel1->CCR.EN`、ADC DMA 请求和地址配置决定；`volatile` 只是告诉编译器：这个数组可能被当前 C 代码看不见的硬件修改，CPU 每次读取都应该真的去内存取。若去掉 `volatile`，低优化等级下可能看不出问题，高优化后平均值计算可能读到缓存过的旧值。

### 6.3 `循环缓冲区` 是什么

循环缓冲区是固定长度内存区域被反复覆盖使用的方式。

本课 DMA 依次写 `g_adc_buffer[0]` 到 `g_adc_buffer[15]`，写完后回到 `g_adc_buffer[0]`。它不无限增长，也不保存所有历史数据，只保存最近一轮附近的采样集合。若忘记循环模式，数组只填一轮后停止刷新。

它属于软件数据结构和 DMA 硬件行为的交界层。软件看见的是一个固定数组；硬件看见的是 `CMAR` 起始地址、`CNDTR` 传输数量、`MINC` 地址步进和 `CIRC` 自动重载。四者组合起来，才形成“循环”的效果。

这里的循环缓冲区还不是严格带读写指针的工程级环形队列。DMA 在循环覆盖，CPU 只是随时把 16 个元素求平均，并不知道 DMA 当前正写到第几个元素。入门实验中这足够观察平均滤波；若做严肃采集，通常要用半传输中断、传输完成中断或双缓冲，保证 CPU 处理的是一块完整样本。

### 6.4 `DMA_CCR_MINC` 是什么

`MINC` 是 DMA 内存地址自增位，属于 DMA 通道 `CCR`。

上一课目标是单变量，所以 `MINC=0`。本课目标是数组，所以必须 `MINC=1`。DMA 每搬一个 16 位 ADC 值，内存地址自动加 2，指向下一个 `uint16_t` 元素。

如果不开 `MINC`，16 次搬运都会覆盖 `g_adc_buffer[0]`，数组其他元素保持 0 或旧值，平均值会严重偏低。

`MINC` 控制的是内存端地址，不控制外设端地址。本课外设地址始终是 `ADC1->DR`，所以 `PINC=0`；内存端要从数组第 0 个元素走到第 15 个元素，所以 `MINC=1`。这正好和第 18 课单变量 DMA 形成对比：目标只有一个变量时，内存地址不应该移动；目标是数组时，内存地址必须移动。

代码位置在 `dma1_channel1_init()`：寄存器版执行 `DMA1_Channel1->CCR |= DMA_CCR_MINC`，HAL 版执行 `hdma_adc1.Init.MemInc = DMA_MINC_ENABLE`。排错时不要只看数组声明，要直接看这个位有没有被写进去。

### 6.5 `DMA_CCR_CIRC` 是什么

`CIRC` 是 DMA 循环模式位，属于 DMA `CCR`。

本课 ADC 连续转换，DMA 也要持续写数组。`CIRC=1` 后，`CNDTR` 递减到 0 时硬件自动重载，并把当前地址回到本轮起点。不开它时，DMA 只填满数组一次。

`CIRC` 控制的是“一轮结束后怎么办”。如果 `CNDTR=16`、`MINC=1` 但 `CIRC=0`，DMA 会完整写完 16 个元素，然后通道停止，后续 ADC 继续转换也不会再刷新数组。你在调试器里会看到数组开始变化一次，之后电位器怎么转都没反应。

HAL 版的对应字段是 `hdma_adc1.Init.Mode = DMA_CIRCULAR`。它和 `HAL_ADC_Start_DMA()` 的长度参数配套使用：长度告诉 DMA 一轮有多长，循环模式告诉 DMA 一轮结束后继续从头来。

### 6.6 `CNDTR = 16` 是什么

`CNDTR` 是 DMA 传输数量寄存器。本课写 `ADC_BUFFER_SIZE`，也就是 16。

它表示一轮 DMA 搬 16 个数据项。这里的数据项宽度由 `PSIZE/MSIZE` 决定，本课是 16 位。`CNDTR` 不是字节数，而是“数据单元数”。

`CNDTR` 会在 DMA 工作时由硬件递减。第一次搬运后从 16 变 15，搬到最后一个元素后变 0；因为 `CIRC=1`，硬件又把它重载回 16。调试时如果你观察 `DMA1_Channel1->CNDTR`，会看到它不断变化，这比只看数组更能说明 DMA 正在工作。

这个寄存器必须在 DMA 通道关闭时配置。代码里先清 `DMA_CCR_EN`，再写 `CNDTR/CPAR/CMAR/CCR`，最后才启动。若通道已经使能时乱改 `CNDTR`，行为不可预期，轻则配置不生效，重则搬运地址和数量错乱。

### 6.7 `CMAR = g_adc_buffer` 是什么

`CMAR` 是 DMA 内存地址寄存器。本课写入数组首地址 `g_adc_buffer`。

当 `MINC=1` 时，DMA 从这个首地址开始，依次写后续元素。若误写成 `&g_adc_buffer[0]` 也等价；若写错地址，DMA 会破坏其他 RAM。

`CMAR` 属于 DMA 寄存器层，但它保存的是 C 对象的地址。`g_adc_buffer` 在表达式里会退化成数组首元素地址，所以寄存器版写 `(uint32_t)g_adc_buffer`。HAL 版则把同一个地址作为 `HAL_ADC_Start_DMA()` 的第二个参数传进去。

本课要求你把“数组名”理解成一个真实 SRAM 地址，而不是抽象容器。DMA 不知道 C 数组边界，也不知道变量名，它只知道从 `CMAR` 这个地址开始按宽度写 `CNDTR` 次。数组越界类错误在 DMA 里尤其危险，因为硬件不会帮你检查。

### 6.8 `CPAR = &ADC1->DR` 是什么

`CPAR` 是 DMA 外设地址寄存器。本课仍然指向 `ADC1->DR`。

虽然目标从变量变成数组，但 ADC 数据源没有变。每次 ADC 完成转换，DMA 都从同一个数据寄存器读结果，所以 `PINC` 仍为 0。

`ADC1->DR` 是 ADC 转换结果寄存器。ADC 每完成一次规则转换，就把 12 位结果放到这里，并产生 DMA 请求。DMA1_Channel1 收到请求后，按 `CPAR` 指向的地址去读这个寄存器，再把读到的数据写到 `CMAR` 指向的内存。

这里容易混淆“方向”和“地址角色”。本课 `DIR=0`，表示外设到内存，所以 `CPAR` 是源地址，`CMAR` 是目标地址。到 UART DMA 发送课时，`DIR=1`，`CPAR=&USART1->DR` 就变成目标地址。地址寄存器名字没变，方向位决定数据流向。

### 6.9 `PSIZE/MSIZE = 16 位` 是什么

`PSIZE` 控制外设端数据宽度，`MSIZE` 控制内存端数据宽度。

ADC 结果是 12 位，通常按 16 位半字搬运；数组元素是 `uint16_t`，也正好是 16 位。宽度不匹配时，数组内容会错位或被截断。

ADC 的有效数据只有 0 到 4095，但寄存器读写通常以半字承载。寄存器版设置 `DMA_CCR_PSIZE_0` 和 `DMA_CCR_MSIZE_0`，含义都是 `01`，也就是 16 位。HAL 版使用 `DMA_PDATAALIGN_HALFWORD` 和 `DMA_MDATAALIGN_HALFWORD`。

如果把内存宽度误配成 8 位，DMA 可能只写低 8 位，采样值最大看起来只有 255 左右；如果外设和内存宽度不一致，数组元素可能出现高低字节混乱。看到 ADC 值范围明显不对时，要把宽度配置列为优先检查项。

### 6.10 `ADC_CR2_CONT` 是什么

`CONT` 是 ADC 连续转换位。它让 ADC 启动一次后持续转换 PA1。

它负责“持续生产采样值”。如果不开它，DMA 最多搬一次或一轮很快停住，缓冲区不会持续刷新。

### 6.11 `ADC_CR2_DMA` 是什么

`DMA` 是 ADC 的 DMA 请求使能位。ADC 每次转换完成后，只有这个位打开，才会请求 DMA1_Channel1 搬运 `DR`。

如果不开它，DMA 通道配置再正确也收不到触发。

### 6.12 `平均滤波` 是什么

平均滤波是软件处理层的简单算法。本课把 16 个 ADC 值相加后除以 16，用平均值控制 LED。

它能降低单次采样噪声造成的抖动。代价是响应会稍慢，因为平均值反映的是一组样本，而不是最新单点。

### 6.13 `uint32_t sum` 是什么

`sum` 用 32 位整数保存累加和。

16 个 12 位最大值相加为 `16 * 4095 = 65520`，接近 `uint16_t` 上限。使用 `uint32_t` 更稳妥，也为后续增大缓冲区留下空间。

### 6.14 `DMA_MINC_ENABLE` 是什么

`DMA_MINC_ENABLE` 是 HAL 的内存地址自增配置，对应寄存器版 `CCR.MINC=1`。

它出现在 `hdma_adc1.Init.MemInc` 字段中。目标是数组时必须启用；目标是单变量时应禁用。

### 6.15 `HAL_ADC_Start_DMA` 的长度参数是什么

`HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buffer, ADC_BUFFER_SIZE)` 的第三个参数是 DMA 传输长度。

本课长度是 16，对应寄存器版 `CNDTR=16`。如果误写成 1，HAL 版会退化成上一课单值刷新效果。

### 6.16 `DMA_CIRCULAR` 是什么

`DMA_CIRCULAR` 是 HAL 的循环模式枚举，对应 `CCR.CIRC=1`。

它让 DMA 填满数组后继续从头覆盖。若改成 `DMA_NORMAL`，缓冲区只更新一轮。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟和 ADC 时钟基础

`system_clock_72mhz_init()` 把系统配置到 72MHz。`adc1_init()` 中再把 ADC 时钟设为 PCLK2/6，即 12MHz。ADC 时钟稳定后，采样时间和转换节奏才可靠。

### 7.2 PC13 和 PA1

PC13 配成推挽输出，用于显示平均值阈值结果。PA1 清 `MODE1/CNF1` 成模拟输入，对应 ADC1_IN1。若 PA1 不是模拟模式，ADC 读数会不稳定。

### 7.3 DMA1 时钟和通道关闭

`RCC->AHBENR |= RCC_AHBENR_DMA1EN` 打开 DMA1 时钟。随后清 `DMA_CCR_EN`，确保通道处于可配置状态。

DMA1 挂在 AHB 总线上，所以不是写 `APB2ENR`。这一点和 GPIOA、GPIOC、ADC1 不同。不开 `DMA1EN` 时，后面对 `DMA1_Channel1->CNDTR/CPAR/CMAR/CCR` 的写法在 C 语言上看似执行了，但硬件模块没有时钟，搬运不会发生。

先关闭通道是 DMA 配置的基本顺序。`CNDTR`、`CPAR`、`CMAR` 和大部分 `CCR` 行为都应在 `EN=0` 时准备好。你可以把它理解成：先把工单、源地址、目标地址和搬运规则写好，再按启动按钮。

### 7.4 `CNDTR = ADC_BUFFER_SIZE`

这句决定一轮传输 16 个数据项。配合循环模式，DMA 会不断以 16 个元素为一轮写入数组。

这里的硬件后果非常具体：DMA 内部计数器被装载为 16。每收到一次 ADC DMA 请求，它搬一个半字，内部计数减 1，当前内存地址按 `MINC` 规则前进。减到 0 后，如果 `CIRC=1`，硬件自动回到初始状态继续下一轮。

如果你在调试器中暂停程序，看到 `CNDTR` 不变，说明 ADC 没有触发 DMA 或 DMA 通道没启动；看到它递减但数组不对，重点查 `CMAR/MINC/MSIZE`。

### 7.5 `CPAR` 和 `CMAR`

`CPAR = &ADC1->DR` 表示源是 ADC 数据寄存器。`CMAR = g_adc_buffer` 表示目标是数组首地址。一个固定源、一个自增目标，形成“单外设寄存器到数组”的采样路径。

这两句把片上外设地址空间和 SRAM 地址空间连接起来。`ADC1->DR` 在外设地址区域，`g_adc_buffer` 在 SRAM 区域；DMA 控制器在总线上直接读一个、写一个，不需要 CPU 中间倒手。

若 `CPAR` 写错，DMA 读到的不是 ADC 结果，数组值可能固定、随机或来自其他寄存器。若 `CMAR` 写错，ADC 数据会写到错误 RAM 位置，可能把全局变量、栈或 HAL 状态破坏掉。这类问题比普通 C 赋值更隐蔽，因为出错语句看起来不是 CPU 在写那个变量。

### 7.6 `CCR` 的关键配置

本课保持 `DIR=0` 外设到内存，`PINC=0` 外设地址固定；打开 `MINC=1` 让内存地址自增；设置 `PSIZE/MSIZE=16bit`；打开 `CIRC=1`；设置高优先级。

这些配置缺一项，数组采样语义都会变形。

可以把 `CCR` 拆成五类问题来读：数据往哪走、地址动不动、每次搬多宽、搬完是否继续、抢总线时优先级多高。本课答案分别是：外设到内存；外设地址不动、内存地址动；半字；循环；高优先级。

清 `CCR` 相关位再置位，是为了避免旧配置残留。例如上一实验若留下 `MINC=0` 或某个宽度配置，本课数组就会表现异常。寄存器版教学里反复强调“先清字段再写字段”，就是为了让当前配置值完全由本课代码决定。

### 7.7 ADC 规则组仍是单通道

`SQR1.L=0` 表示一个规则转换，`SQR3.SQ1=1` 表示采样 ADC1_IN1。本课不是多通道扫描，只是同一通道连续采样多次。

### 7.8 ADC 连续转换和 DMA 请求

`ADC_CR2_CONT` 让 ADC 连续转换，`ADC_CR2_DMA` 让转换完成触发 DMA 请求。ADC 负责产生数据，DMA 负责把数据排进数组。

### 7.9 ADC 校准

`ADON` 后执行 `RSTCAL` 和 `CAL`。校准属于 ADC 模拟模块准备动作，有助于减少偏差。

### 7.10 启动顺序

`adc1_dma_start()` 先使能 DMA 通道，再设置 `EXTTRIG | SWSTART` 启动 ADC。这样第一笔转换结果出来时，DMA 已经准备好接收请求。

这个顺序不能反过来。若先启动 ADC，第一笔甚至前几笔转换完成时 DMA 还没开，`DR` 里的结果可能被后续转换覆盖，缓冲区起始数据丢失。虽然本课连续采样最终会刷新数组，但工程上仍应养成“消费者先就绪，生产者再启动”的顺序。

`EXTTRIG | SWSTART` 在 F1 ADC 中用于软件触发规则转换。因为 `CONT=1` 已经开启，第一次软件启动后 ADC 会持续转换；每次转换完成又因为 `DMA=1` 产生 DMA 请求。这里一条语句同时把 ADC 生产线真正跑起来。

### 7.11 平均值计算

`adc_buffer_average_get()` 遍历 16 个元素，用 `uint32_t` 求和，最后除以 16。CPU 不参与每次搬运，只在需要时读取缓冲区并处理。

### 7.12 主循环控制 LED

主循环比较平均值和 2048。超过时拉低 PC13 点亮 LED，低于时拉高熄灭。阈值约等于 12 位 ADC 满量程的一半。

### 7.13 读数组时的同步局限

本课没有使用半传输中断或双缓冲，CPU 可能在 DMA 正写数组时读取。入门实验可以接受；工程里采集频率高时，要用半传输/传输完成中断或乒乓缓冲保证数据块完整。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 基础初始化

`HAL_Init()` 初始化 HAL Tick 和基础状态。`HAL_RCC_OscConfig()`、`HAL_RCC_ClockConfig()` 对应寄存器版 HSE、PLL、分频和 Flash 延迟配置。

### 8.2 GPIO HAL 配置

PC13 使用 `GPIO_MODE_OUTPUT_PP`。PA1 使用 `GPIO_MODE_ANALOG`。这分别对应寄存器版 PC13 推挽输出和 PA1 模拟输入。

### 8.3 DMA 通道选择

`hdma_adc1.Instance = DMA1_Channel1` 选择 ADC1 固定映射的 DMA 通道。HAL 不能改变芯片内部请求映射。

### 8.4 DMA 方向和地址自增

`Direction = DMA_PERIPH_TO_MEMORY` 对应 `DIR=0`。`PeriphInc = DISABLE` 对应 `PINC=0`。`MemInc = DMA_MINC_ENABLE` 对应 `MINC=1`，这是本课 HAL 版相对上一课的关键变化。

HAL 字段名比寄存器位更像人话，但表达的是同一件硬件配置。`PeriphInc` 不是“外设数据会不会变化”，而是“外设地址会不会变化”；ADC 的结果每次都会变，但地址永远是 `ADC1->DR`，所以外设地址不自增。

`MemInc` 是本课最该盯住的字段。目标是数组时它必须启用。若复制上一课单变量 DMA 的 HAL 初始化，最容易漏掉的就是这里，现象就是 `g_adc_buffer[0]` 被反复覆盖，后续元素没有连续样本。

### 8.5 DMA 数据宽度和模式

`PeriphDataAlignment = HALFWORD`、`MemDataAlignment = HALFWORD` 对应 16 位搬运。`Mode = DMA_CIRCULAR` 对应循环模式。`Priority = HIGH` 对应高优先级。

### 8.6 `HAL_DMA_Init`

`HAL_DMA_Init(&hdma_adc1)` 把上述字段写入 DMA 通道寄存器。它只配置 DMA，不启动 ADC。

### 8.7 `__HAL_LINKDMA`

`__HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1)` 把 ADC 句柄和 DMA 句柄关联。没有这步，`HAL_ADC_Start_DMA()` 找不到 DMA1_Channel1。

### 8.8 ADC 初始化字段

`ContinuousConvMode = ENABLE` 对应 `CR2.CONT=1`。`NbrOfConversion = 1` 对应单通道规则组。`ExternalTrigConv = ADC_SOFTWARE_START` 对应软件启动。

### 8.9 ADC 校准和通道配置

`HAL_ADCEx_Calibration_Start()` 对应寄存器版校准。`HAL_ADC_ConfigChannel()` 选择 `ADC_CHANNEL_1`、`ADC_REGULAR_RANK_1` 和 `ADC_SAMPLETIME_239CYCLES_5`。

### 8.10 `HAL_ADC_Start_DMA`

调用目标地址是 `g_adc_buffer`，长度是 `ADC_BUFFER_SIZE`。这对应 `CMAR=g_adc_buffer` 和 `CNDTR=16`。HAL 内部还会使能 DMA 和 ADC DMA 请求。

这个 API 不是“只启动 ADC”。它会沿着 ADC 句柄找到 `DMA_Handle`，配置 DMA 的内存地址和传输长度，然后启动 DMA，再启动 ADC 的 DMA 转换流程。没有前面的 `__HAL_LINKDMA()`，它就缺少从 ADC 到 DMA 通道的句柄关系。

第二个参数被强转成 `(uint32_t *)g_adc_buffer`，是 HAL F1 API 的参数类型要求。实际数据宽度仍由 DMA 初始化里的 `MemDataAlignment = HALFWORD` 决定。不要因为参数类型是 `uint32_t *` 就误以为 DMA 按 32 位写数组。

### 8.11 HAL 主循环

HAL 版同样调用 `adc_buffer_average_get()` 求平均，再用 `HAL_GPIO_WritePin()` 控制 PC13。底层链路和寄存器版一致。

这里不要因为用了 HAL 就忽略 `g_adc_buffer` 的异步来源。数组仍然由 DMA 硬件改写，主循环仍然只是读取和计算。HAL 只改变初始化和启动写法，不改变“ADC 生产、DMA 搬运、CPU 处理”的分工。

`HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)` 对应低电平点亮 PC13，`GPIO_PIN_SET` 对应熄灭。这个 LED 极性和 ADC/DMA 无关，但它是现象层判断阈值是否生效的出口。

### 8.12 HAL 返回值和错误处理

本课 HAL 版对 `HAL_DMA_Init()`、`HAL_ADC_Init()`、`HAL_ADCEx_Calibration_Start()`、`HAL_ADC_ConfigChannel()`、`HAL_ADC_Start_DMA()` 都检查返回值，失败时进入 `error_handler()`。

这属于工程层保护。寄存器版通常需要你自己观察标志和寄存器；HAL 版把一部分失败情况折叠成 `HAL_OK/HAL_ERROR/HAL_BUSY/HAL_TIMEOUT`。如果程序停在 `error_handler()`，说明不是主循环逻辑错，而是某个初始化或启动 API 没有成功完成。

排查 HAL 错误时，要把返回值和底层链路对应起来。例如 `HAL_ADC_Start_DMA()` 失败，可能是 ADC 句柄、DMA 句柄关联、DMA 状态、ADC 状态或参数长度有问题；不是简单地说“HAL 坏了”。

## 9. 两个版本真正应该怎么学

先看寄存器版，确认数组采样的四个硬件条件：`CMAR` 指向数组、`CNDTR=16`、`MINC=1`、`CIRC=1`。再看 HAL 版，把 `MemInc`、`Mode`、`Start_DMA` 长度参数逐个对应回这些寄存器动作。

本课不要只记住“DMA 可以进 buffer”。要说清楚：为什么上一课不能形成数组，为什么本课能形成数组。

## 10. 检验问题清单

### 10.1 本课相对单变量 DMA 的关键变化是什么

**答**：目标内存从单个变量变成数组，`MINC` 从 0 变成 1，`CNDTR` 从 1 变成 16。

### 10.2 如果不开 `MINC` 会怎样

**答**：DMA 会反复写数组第一个元素，其他元素不更新，平均值失真。

### 10.3 如果不开 `CIRC` 会怎样

**答**：DMA 填满 16 个元素后停止，后续电压变化不会继续反映到数组。

### 10.4 `CNDTR=16` 表示 16 字节吗

**答**：不是。它表示 16 个数据单元。本课数据宽度是 16 位，所以一轮实际写 32 字节。

### 10.5 为什么 `CPAR` 仍然是 `&ADC1->DR`

**答**：因为 ADC 数据源没有变，只是 DMA 写入目标从变量变成数组。

### 10.6 为什么平均值用 `uint32_t` 求和

**答**：为了避免多个 12 位采样值相加时接近或超过 16 位范围，也方便后续扩大缓冲区。

### 10.7 HAL 版哪个字段对应 `MINC=1`

**答**：`hdma_adc1.Init.MemInc = DMA_MINC_ENABLE`。

### 10.8 HAL 版长度参数写成 1 会怎样

**答**：DMA 只按 1 个数据项循环，相当于退化成单值采样，数组其他元素不会形成有效历史数据。

## 11. 工程实现步骤

### 11.1 需求分析

需求是让 ADC1 连续采样 PA1，把结果自动写入 16 元素循环缓冲区，CPU 对缓冲区求平均后控制 LED。

拆成工程需求就是三件事：采样不能靠 CPU 等待，搬运不能靠 CPU 逐个复制，判断 LED 时不要只看单次噪声值。ADC 连续转换解决“持续采样”，DMA 循环模式解决“自动搬运”，平均滤波解决“让判断更稳”。

### 11.2 硬件核查

确认 PA1 接 0 到 3.3V 模拟电压，PC13 LED 可用，板卡为 STM32F103C8T6，HSE 为 8MHz。

PA1 最好接电位器中间端，电位器两端接 3.3V 和 GND。若 PA1 悬空，ADC 值会飘，LED 抖动不能说明 DMA 或平均滤波错了。若误接 5V，可能损坏引脚。PC13 是低电平点亮，判断 LED 现象时要先确认这个电气事实。

### 11.3 寄存器路线

进入 `20_adc_dma/reg`，重点读 `dma1_channel1_init()`、`adc1_init()`、`adc_buffer_average_get()`。确认 `MINC/CIRC/CNDTR/CMAR` 是否符合数组采样。

```sh
pio run
pio run -t upload
```

### 11.4 HAL 路线

进入 `20_adc_dma/hal`，重点读 `dma1_channel1_init()` 和 `HAL_ADC_Start_DMA()`。确认 `MemInc = ENABLE`、`Mode = DMA_CIRCULAR`、长度为 `ADC_BUFFER_SIZE`。

### 11.5 工程思维

DMA 负责采集，CPU 负责处理，这是数据采集系统常见分工。工程上还要考虑同步：CPU 读取数组时，DMA 可能正在写同一个数组。

本课为了让链路清晰，没有引入半传输中断和双缓冲。真实项目中如果采样频率高，CPU 求平均时可能跨过 DMA 正在更新的元素，得到一组“半新半旧”的样本。解决方案不是让 CPU 关 DMA 硬等，而是用半缓冲通知、双缓冲或更明确的生产者/消费者协议。

### 11.6 常见工程陷阱

缓冲区长度和 `CNDTR` 不一致、忘记 `MINC`、忘记 `CIRC`、宽度不匹配、数组没加 `volatile`、平均值求和类型太小，都会导致数据看起来不稳定或不可信。

还有一个隐蔽陷阱是“以为数组顺序就是绝对时间顺序”。循环模式下，DMA 当前写指针会不断绕圈，`g_adc_buffer[0]` 不一定永远比 `g_adc_buffer[15]` 更早或更晚。对平均值来说顺序不重要；对波形分析、峰值检测、协议采样来说，必须知道当前 DMA 写到哪里。

另一个工程陷阱是把 DMA 缓冲区放到不适合 DMA 访问的区域。F103 的 SRAM 全局数组没问题，但一些芯片有 DCache、不同 RAM 域或 MPU 限制时，DMA 缓冲区还要考虑缓存一致性和内存区域。本课先不引入这些复杂性，但要知道 DMA 不是普通函数调用，它直接访问总线地址。

## 12. 运行现象

正常情况下，调试器里 `g_adc_buffer[0..15]` 会持续变化。转动电位器时，平均值随之变化。平均值超过约 2048 时 PC13 点亮，低于时熄灭。相比单值 DMA，LED 对瞬时噪声不那么敏感。

## 13. 常见问题排查

### 13.1 数组只有第 0 个元素变化

检查 `DMA_CCR_MINC` 或 HAL 的 `DMA_MINC_ENABLE`。不开内存自增时，DMA 只覆盖首元素。

### 13.2 数组只更新一轮

检查 `DMA_CCR_CIRC` 或 HAL 的 `DMA_CIRCULAR`。普通模式填满后会停止。

### 13.3 所有元素都不更新

检查 PA1 模拟输入、ADC1 时钟、`ADC_CR2_DMA`、DMA1_Channel1 时钟、`CPAR/CMAR/CNDTR` 和启动顺序。

排查顺序建议从硬件链路向软件走：先看 PA1 是否有 0 到 3.3V 电压，再看 `ADC1->DR` 是否变化；若 `DR` 变化但数组不变，问题在 ADC DMA 请求或 DMA 通道；若 `DR` 也不变，问题在 ADC 通道、采样时间、校准或启动。

### 13.4 平均值异常或 LED 抖动

检查输入是否悬空、采样时间是否足够、数组元素宽度是否为 16 位、求和是否使用 `uint32_t`。也要理解本课没有做严格 DMA 同步。

如果 16 个数组值本身就大幅跳动，多半是输入硬件不稳定或采样源阻抗过高；如果数组值稳定但平均值异常，重点查循环长度、求和类型和是否有越界；如果平均值在阈值附近来回跳，这是阈值比较本身没有滞回，不一定是 DMA 错。

### 13.5 HAL 版行为像上一课单变量

检查 `HAL_ADC_Start_DMA()` 第三个参数是否为 `ADC_BUFFER_SIZE`，以及 `MemInc` 是否启用。

## 14. 本课最核心的结论

1. 本课把 ADC+DMA 从“最新值变量”升级成“循环采样数组”。
2. `MINC=1` 让 DMA 依次写数组元素，是数组采样的核心。
3. `CNDTR=16` 表示一轮传输 16 个数据单元，不是 16 字节。
4. `CIRC=1` 让 DMA 填满数组后继续从头覆盖，形成循环缓冲区。
5. ADC 数据源仍然是 `ADC1->DR`，所以外设地址不自增。
6. 平均滤波是 CPU 对 DMA 缓冲区的最简单软件处理。
7. HAL 版必须把 `MemInc`、`DMA_CIRCULAR`、长度参数同时配对，才等价于寄存器版。

## 15. 建议你现在怎么读这节课

先读 `dma1_channel1_init()`，只盯 `CMAR/CNDTR/MINC/CIRC`。再读 `adc_buffer_average_get()`，理解 CPU 不搬运数据，只处理数据。最后读 HAL 版，把 `DMA_MINC_ENABLE` 和 `HAL_ADC_Start_DMA()` 的长度参数对应回寄存器版。

## 16. 扩展练习

- 把 `ADC_BUFFER_SIZE` 改成 4 或 32，观察平均值响应速度。
- 去掉 `MINC`，观察数组变化。
- 把 `DMA_CIRCULAR` 改成普通模式，观察只更新一轮的现象。
- 在调试器里观察 `DMA1_Channel1->CNDTR` 如何递减和重载。
- 尝试用半传输/传输完成中断处理半个缓冲区。

## 17. 下一课预告

上一课：[19_dma_memory_uart_cases](../19_dma_memory_uart_cases/README.md)

下一课：[21_uart_polling](../21_uart_polling/README.md)
