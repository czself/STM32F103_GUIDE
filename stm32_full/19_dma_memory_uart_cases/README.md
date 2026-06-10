# 19_dma_memory_uart_cases - DMA 内存拷贝与 USART1 发送

## 1. 本课到底在学什么

本课表面现象是：程序每隔一段时间通过 USART1 发送字符串 `DMA UART demo\n`，同时 PC13 LED 翻转一次。

真正要学的是 DMA 的两个典型用法：

```text
用法 1：内存到内存
g_src[] -> DMA1_Channel1 -> g_dst[]

用法 2：内存到外设
g_dst[] -> DMA1_Channel4 -> USART1->DR -> PA9 TX 引脚
```

上一课 ADC+DMA 是“外设到内存”：ADC 产生数据，DMA 写入变量。本课换两个方向看 DMA：先让 DMA 在两块 RAM 之间拷贝，再让 DMA 把 RAM 中的字符串喂给 USART1 发送。你要建立的核心直觉是：DMA 不是 ADC 专属工具，它是芯片内部的数据搬运控制器；不同外设请求、不同方向、不同通道，会形成不同数据路径。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 区分 DMA 的内存到内存传输和 USART 发送 DMA。
- 解释为什么内存拷贝使用 `DMA1_Channel1`，而 USART1_TX 使用 `DMA1_Channel4`。
- 说清楚 `DMA_CCR_MEM2MEM`、`DMA_CCR_DIR`、`DMA_CCR_MINC`、`DMA_CCR_PINC` 在两个案例中分别怎么用。
- 解释 `USART_CR3_DMAT` 为什么是 USART1 发送 DMA 的关键开关。
- 看懂 `TCIF1` 和 `TCIF4` 分别表示哪一路 DMA 完成。
- 看懂 HAL 版中 `HAL_UART_Transmit_DMA()` 如何借助 `hdmatx` 句柄启动 DMA1_Channel4。
- 根据串口无输出、只输出一次、输出乱码、LED 不闪等现象排查对应层级。

## 3. 本课目录结构

```text
19_dma_memory_uart_cases/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

寄存器版直接配置 DMA1_Channel1、DMA1_Channel4 和 USART1。HAL 版用 `memcpy()` 完成内存拷贝，用 `HAL_UART_Transmit_DMA()` 完成 USART1 DMA 发送。两个版本不是逐句完全相同，但学习目标相同：理解 DMA 搬运路径如何随使用场景变化。

## 4. 实验硬件

本课使用 STM32F103C8T6 BluePill，工程板卡是 `genericSTM32F103C8`，外部 8MHz HSE 配到 72MHz。

连接和观察点：

- `PA9`：USART1_TX，接 USB-TTL 的 RX。
- `GND`：开发板 GND 和 USB-TTL GND 必须共地。
- `PC13`：板载 LED，每轮发送后翻转。
- 串口参数：115200 波特率，8 数据位，无校验，1 停止位。

本课只发送，不接收，所以不需要连接 PA10。若串口助手无输出，先确认 USB-TTL 方向：开发板 PA9 要接到转换器 RX。

## 5. 先建立完整脑图

本课可以拆成六层理解：

1. 现象层：串口助手周期性看到 `DMA UART demo`，PC13 周期性翻转。
2. 物理层：PA9 输出串口电平，经 USB-TTL 转成电脑串口数据。
3. 芯片模块层：DMA1 负责搬运，USART1 负责串行发送，GPIOA 提供 PA9 复用输出，GPIOC 控制 LED。
4. 寄存器层：`DMA1_Channel1` 配 MEM2MEM，`DMA1_Channel4` 配内存到 USART1，`USART1->CR3.DMAT` 打开发送 DMA 请求。
5. C/CMSIS 层：`g_src`、`g_dst` 是 RAM 缓冲区，`DMA1->ISR` 的 `TCIF` 标志用于判断传输完成。
6. HAL 工程层：`UART_HandleTypeDef` 描述 USART1，`DMA_HandleTypeDef hdma_tx` 描述 TX DMA，`__HAL_LINKDMA()` 把两者关联。

先看内存拷贝路径，再看串口发送路径。它们都叫 DMA，但触发方式不同：MEM2MEM 打开后 DMA 可以直接搬；USART1_TX DMA 需要 USART 发送器通过 DMA 请求逐字节取数。

## 6. 核心名词解释

### 6.1 `DMA MEM2MEM` 是什么

`MEM2MEM` 是 DMA 的内存到内存模式，属于 DMA 硬件层。

普通外设 DMA 通常由外设请求触发，例如 USART 发送空了才请求 DMA 写下一个字节。MEM2MEM 不依赖外设请求，打开后 DMA 在两块内存之间搬运。本课寄存器版用它把 `g_src` 拷贝到 `g_dst`。

如果没开 `MEM2MEM`，DMA1_Channel1 没有外设请求源驱动这次内存拷贝，`g_dst` 可能不会得到期望字符串。

### 6.2 `g_src` 是什么

`g_src` 是 RAM 中的源数组，属于 C/CMSIS 层的数据对象。

寄存器版中它保存 `"DMA UART demo\n"`。内存拷贝阶段 DMA 从它读取；UART 发送阶段不直接发它，而是发送已经拷贝到 `g_dst` 的内容。

如果源数组长度和字符串理解不一致，可能会发送多余的 `\0` 或旧数据。本课数组长度是 16 字节，字符串包含换行和结尾空字符，DMA 会按 `sizeof(g_src)` 搬完整个数组。

### 6.3 `g_dst` 是什么

`g_dst` 是 RAM 中的目标数组，属于 C/CMSIS 层的数据对象。

第一段 DMA 把 `g_src` 写入 `g_dst`；第二段 DMA 把 `g_dst` 作为 USART1 发送缓冲区。它是两个案例之间的连接点。

如果内存拷贝失败，USART DMA 仍然可能发送，但发送的是未初始化或旧的 `g_dst` 内容。

### 6.4 `DMA1_Channel1` 在本课是什么

`DMA1_Channel1` 是 DMA1 的第 1 通道，属于 DMA 硬件层。本课寄存器版把它用于内存到内存拷贝。

注意它在上一课用于 ADC1 DMA，是因为 ADC1 请求固定映射到 Channel1；本课 MEM2MEM 不靠 ADC 请求，而是利用 DMA 通道直接做 RAM 拷贝。也就是说，同一个通道在不同课程里可以承担不同用途，但前提是当时没有被别的外设请求占用。

如果同时把 Channel1 给 ADC 和 MEM2MEM 用，就会产生资源冲突。本课没有 ADC，所以可用它演示内存拷贝。

### 6.5 `DMA1_Channel4` 是什么

`DMA1_Channel4` 是 DMA1 的第 4 通道，属于 DMA 硬件层。STM32F103 中 USART1_TX 的 DMA 请求映射到 DMA1_Channel4。

它控制 USART1 发送缓冲区到 `USART1->DR` 的搬运。USART1 发送器每需要下一个字节，就通过 DMA 请求让 Channel4 把内存中的下一个字节写入数据寄存器。

如果用错通道，USART1 的 TX DMA 请求不会驱动该通道，串口助手通常看不到输出。

### 6.6 `USART1_TX` 是什么

`USART1_TX` 是 USART1 的发送信号，物理引脚在本课映射到 PA9。

它属于芯片外设和物理引脚之间的复用输出路径。USART1 负责把并行字节转换成串行位流，PA9 负责把这个 TX 信号输出到板外。

如果 PA9 没有配置成复用推挽输出，USART1 内部可能在发送，但信号不会正确到引脚。

### 6.7 `USART1->DR` 是什么

`DR` 是 USART1 Data Register，数据寄存器，属于 USART1 外设。

发送时，软件或 DMA 把一个字节写入 `DR`，USART 发送器再把它移入发送移位寄存器并从 TX 引脚输出。本课 DMA1_Channel4 的外设地址就是 `&USART1->DR`。

如果 `CPAR` 没有指向 `USART1->DR`，DMA 就不会把字节送进 USART 发送器。

### 6.8 `USART_CR3_DMAT` 是什么

`USART_CR3_DMAT` 是 USART1 的 DMA Transmitter Enable 位，属于 USART `CR3` 寄存器。

它控制 USART1 发送侧是否向 DMA 发请求。本课寄存器版设置 `USART1->CR3 = USART_CR3_DMAT`。只有这个位打开，USART1_TX 才会在需要数据时请求 DMA1_Channel4 写 `DR`。

如果忘记设置它，DMA 通道可能配置好了，USART 也使能了，但发送 DMA 不会被触发，串口无输出。

### 6.9 `DMA_CCR_DIR` 是什么

`DIR` 是 DMA 通道方向位，属于 DMA `CCR`。

在 STM32F1 DMA 中，`DIR=0` 表示从外设读到内存，`DIR=1` 表示从内存读到外设。USART1_TX DMA 要把内存里的 `g_dst` 写到 `USART1->DR`，所以 Channel4 设置 `DIR=1`。

在 MEM2MEM 案例里，代码也设置 `DIR=1`，配合 `MEM2MEM=1`，让 `CPAR` 作为源地址、`CMAR` 作为目标地址使用。方向理解错时，最常见现象是数据没复制到目标或串口不发送。

### 6.10 `DMA_CCR_MEM2MEM` 是什么

`DMA_CCR_MEM2MEM` 是 DMA 内存到内存模式位，属于 DMA `CCR`。

它只用于本课第一段内存拷贝。打开后，DMA 可以不等外设请求，直接从源内存搬到目标内存。USART1_TX 发送不能打开它，因为 USART 发送节奏应该由 USART 外设请求控制。

如果在 USART 发送通道乱开 MEM2MEM，DMA 行为会偏离 USART 请求节奏，发送链路就不是正确的外设 DMA。

### 6.11 `DMA_CCR_MINC` 是什么

`MINC` 是内存地址自增位，属于 DMA `CCR`。

本课两段 DMA 都需要打开 `MINC`。内存拷贝时，源和目标都要逐字节移动；USART 发送时，内存源 `g_dst` 要逐字节前进，否则会一直发送同一个字节。

如果 USART DMA 不开 `MINC`，串口可能重复输出第一个字符，而不是完整字符串。

### 6.12 `DMA_CCR_PINC` 是什么

`PINC` 是外设地址自增位，属于 DMA `CCR`。

在 MEM2MEM 拷贝中，代码把 `CPAR` 当作源内存地址使用，所以打开 `PINC` 让源地址逐字节前进。USART1_TX 中，外设地址固定为 `USART1->DR`，所以不能打开 `PINC`。

如果 USART 发送时误开 `PINC`，DMA 会把后续字节写到 `USART1->DR` 后面的地址，串口输出会异常，甚至影响其他 USART 寄存器。

### 6.13 `TCIF1` 和 `TCIF4` 是什么

`TCIF` 是 Transfer Complete Interrupt Flag，传输完成标志，属于 DMA1 的 `ISR` 状态寄存器。

`TCIF1` 对应 Channel1 完成，`TCIF4` 对应 Channel4 完成。寄存器版轮询这些标志判断一轮 DMA 是否结束，再通过 `IFCR` 写 1 清标志。

如果不等完成就复用缓冲区，可能发送旧数据或半截数据；如果不清标志，下一轮判断可能被旧完成状态误导。

### 6.14 `USART1->BRR` 是什么

`BRR` 是 USART Baud Rate Register，波特率寄存器，属于 USART1。

本课代码写 `USART1->BRR = 72000000U / 115200U`，用 72MHz PCLK2 近似配置 115200 波特率。它决定串口位时间，电脑串口助手必须使用相同波特率。

如果波特率配置错，常见现象是串口输出乱码，或者看起来完全没有可读字符。

### 6.15 `HAL_UART_Transmit_DMA` 是什么

`HAL_UART_Transmit_DMA()` 是 HAL 的 UART DMA 发送函数，属于 HAL 工程层。

它接收 UART 句柄、发送缓冲区地址和长度。本课传入 `&huart1`、`g_dst`、`sizeof(g_dst)`，表示让 USART1 通过已关联的 TX DMA 通道发送 `g_dst` 这段内存。

它本质上会配置 DMA1_Channel4 的地址和计数，打开 USART TX DMA 请求，并启动 DMA 发送。如果 UART 句柄没有关联 `hdmatx`，它就不知道该用哪个 DMA 通道。

在 CubeF1 HAL 里，它还依赖 DMA 和 USART 中断完成收尾：DMA 搬完后进入 `DMA1_Channel4_IRQHandler()`，HAL 再等待 USART 发送完成中断，最后把 UART 状态恢复为 READY。否则第一轮可能已经发出，但下一轮会因为 UART 仍是 BUSY 状态而启动失败。

### 6.16 `__HAL_LINKDMA(&huart1, hdmatx, hdma_tx)` 是什么

这是 HAL 的句柄关联宏，属于 HAL 工程层。

`hdmatx` 是 `UART_HandleTypeDef` 里专门表示发送 DMA 的成员。把它指向 `hdma_tx` 后，`HAL_UART_Transmit_DMA()` 才能找到 DMA1_Channel4。

如果缺少这句，UART 初始化和 DMA 初始化看起来都做了，但 HAL 的 UART 发送函数无法把它们连接起来，DMA 发送会失败。

### 6.17 `memcpy` 在 HAL 版中扮演什么角色

HAL 版用标准库 `memcpy(g_dst, g_src, sizeof(g_dst))` 完成内存拷贝，而不是用 DMA MEM2MEM。

这说明 HAL 版本课重点放在 USART DMA 发送上。它仍然保留 `g_src -> g_dst -> USART1` 的数据路径，但第一段由 CPU/库函数完成，第二段由 DMA 完成。读文档时要诚实地区分这点。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟和 Flash 等待周期

`system_clock_72mhz_init()` 设置 Flash 预取和 2 个等待周期，打开 HSE，配置 PLL x9，最后切换 SYSCLK 到 PLL。

USART1 在 APB2 上，波特率计算依赖 PCLK2。代码后面直接用 `72000000U / 115200U` 写 `BRR`，所以系统时钟必须先稳定在 72MHz。

### 7.2 PC13 LED 初始化

`pc13_led_init()` 打开 GPIOC 时钟，把 PC13 配成通用推挽输出，并默认输出高电平熄灭。

LED 在本课不是主要数据通路，但它能说明主循环在周期运行。如果串口无输出但 LED 翻转，问题更可能在 USART/DMA/接线；如果 LED 也不动，先查时钟、下载和程序是否跑起来。

### 7.3 `g_src` 和 `g_dst`

```c
static uint8_t g_src[16] = "DMA UART demo\n";
static uint8_t g_dst[16];
```

`g_src` 是源缓冲区，`g_dst` 是目标缓冲区。第一段 DMA 把前者复制到后者，第二段 DMA 发送后者。数组长度固定为 16，所以 DMA 每轮搬 16 个字节。

### 7.4 打开 GPIOA、USART1、DMA1 时钟

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
...
RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
...
RCC->AHBENR |= RCC_AHBENR_DMA1EN;
```

GPIOA 和 USART1 在 APB2，DMA1 在 AHB。AFIO 时钟用于复用功能相关配置。少开任一时钟，对应模块就不会工作：PA9 无法输出、USART1 无法发送或 DMA 无法搬运。

### 7.5 PA9 配成复用推挽输出

代码清 `GPIOA->CRH` 中 PA9 的配置位，再设置：

```c
GPIOA->CRH |= GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1;
```

PA9 属于 CRH。`MODE9=10` 表示 2MHz 输出，`CNF9=10` 表示复用推挽输出。这样 USART1_TX 才能从芯片内部 USART 模块走到 PA9 引脚。

### 7.6 USART1 波特率和发送使能

```c
USART1->BRR = 72000000U / 115200U;
USART1->CR3 = USART_CR3_DMAT;
USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
```

`BRR` 设置波特率。`CR3.DMAT` 打开 USART 发送 DMA 请求。`CR1.TE` 打开发送器，`CR1.UE` 使能 USART。三者缺一不可：有波特率才有正确位时间，有 TE 才能发送，有 DMAT 才能请求 DMA。

### 7.7 `dma_mem_copy()` 先关闭并配置 Channel1

```c
DMA1_Channel1->CCR = 0;
DMA1_Channel1->CPAR = (uint32_t)g_src;
DMA1_Channel1->CMAR = (uint32_t)g_dst;
DMA1_Channel1->CNDTR = sizeof(g_src);
```

`CCR=0` 让通道停在可配置状态。MEM2MEM 中，代码把 `CPAR` 放源地址，`CMAR` 放目标地址，长度为 16 字节。

### 7.8 Channel1 的 MEM2MEM 配置

```c
DMA1_Channel1->CCR = DMA_CCR_MINC |
                     DMA_CCR_PINC |
                     DMA_CCR_DIR |
                     DMA_CCR_MEM2MEM |
                     DMA_CCR_PL_0 |
                     DMA_CCR_EN;
```

`MEM2MEM=1` 让 DMA 做内存到内存。`DIR=1` 配合 MEM2MEM 表示从 `CPAR` 指向的源读到 `CMAR` 指向的目标。`PINC=1` 和 `MINC=1` 让源、目标地址都逐字节前进。代码没有设置 `PSIZE/MSIZE`，默认 8 位，正好匹配 `uint8_t` 数组。

### 7.9 等待并清除 `TCIF1`

```c
while ((DMA1->ISR & DMA_ISR_TCIF1) == 0U) {}
DMA1->IFCR = DMA_IFCR_CTCIF1;
```

第一句等 Channel1 传输完成，第二句清完成标志。只有确认 `g_dst` 已经拷贝完成，后面才能把它交给 USART DMA 发送。

### 7.10 `dma_uart_send()` 配置 Channel4

```c
DMA1_Channel4->CCR = 0;
DMA1_Channel4->CPAR = (uint32_t)&USART1->DR;
DMA1_Channel4->CMAR = (uint32_t)g_dst;
DMA1_Channel4->CNDTR = sizeof(g_dst);
```

Channel4 的外设端是 `USART1->DR`，内存端是 `g_dst`。长度为 16 字节。这里和 MEM2MEM 不同：外设地址固定，内存地址要自增。

### 7.11 Channel4 的 USART TX 配置

```c
DMA1_Channel4->CCR = DMA_CCR_MINC |
                     DMA_CCR_DIR |
                     DMA_CCR_PL_0 |
                     DMA_CCR_EN;
```

`DIR=1` 表示内存到外设，`MINC=1` 表示依次读取 `g_dst` 的每个字节。没有 `PINC`，因为每个字节都写同一个 `USART1->DR`。没有 `MEM2MEM`，因为发送节奏由 USART1_TX 请求控制。

### 7.12 等待并清除 `TCIF4`

```c
while ((DMA1->ISR & DMA_ISR_TCIF4) == 0U) {}
DMA1->IFCR = DMA_IFCR_CTCIF4;
```

这表示本轮 DMA 已把 16 个字节写给 USART 数据寄存器。注意 DMA 完成表示数据已经交给 USART，不一定等同于最后一个停止位已经完全从引脚发完；但对本课的周期发送和延时来说足够使用。

### 7.13 主循环的完整动作

```c
while (1) {
    dma_mem_copy();
    dma_uart_send();
    pc13_toggle();
    delay_cycles(7200000U);
}
```

每轮先复制，再发送，再翻转 LED，再延时。这个顺序确保串口发送的数据来自本轮刚拷贝好的 `g_dst`。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和 `SysTick_Handler`

HAL 版 main 先调用 `HAL_Init()`，并实现 `SysTick_Handler()` 调用 `HAL_IncTick()`。这样 `HAL_Delay()` 才能基于 HAL Tick 工作。

如果 SysTick 没有正确递增，主循环里的 `HAL_Delay(200)` 和 `HAL_Delay(800)` 会异常，表现为卡住或节奏不对。

### 8.2 HAL 时钟配置

`RCC_OscInitTypeDef` 选择 HSE 和 PLL x9，`RCC_ClkInitTypeDef` 设置 SYSCLK、HCLK、PCLK1、PCLK2。它对应寄存器版的 HSE、PLL、分频和 Flash 等待周期配置。

USART1 波特率由 HAL 根据时钟和 `BaudRate` 字段计算，底层仍然写 `USART1->BRR`。

### 8.3 HAL GPIO 配置

PC13 使用 `GPIO_MODE_OUTPUT_PP`。PA9 使用：

```c
gpio.Pin = GPIO_PIN_9;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_MODE_AF_PP` 对应复用推挽输出，使 USART1_TX 能输出到 PA9。

### 8.4 UART 句柄字段

`huart1.Instance = USART1` 选择 USART1。`BaudRate = 115200` 配置波特率。`WordLength = UART_WORDLENGTH_8B`、`StopBits = UART_STOPBITS_1`、`Parity = UART_PARITY_NONE` 对应 8N1。`Mode = UART_MODE_TX` 表示只发送。

这些字段最终会让 `HAL_UART_Init()` 写 USART 的 `BRR/CR1/CR2/CR3` 等寄存器。

### 8.5 DMA TX 句柄选择通道

```c
hdma_tx.Instance = DMA1_Channel4;
```

这对应 USART1_TX 的固定 DMA 通道。HAL 版也必须遵守芯片请求映射，不能随便换通道。

### 8.6 `Direction = DMA_MEMORY_TO_PERIPH`

它对应寄存器版 Channel4 的 `DIR=1`。数据从 `g_dst` 所在内存流向 USART1 数据寄存器，因此是内存到外设。

### 8.7 地址自增字段

`PeriphInc = DMA_PINC_DISABLE` 对应 `PINC=0`，因为 USART 数据寄存器地址固定。

`MemInc = DMA_MINC_ENABLE` 对应 `MINC=1`，因为要依次发送 `g_dst` 中的每个字节。

### 8.8 数据宽度字段

`PeriphDataAlignment = DMA_PDATAALIGN_BYTE` 和 `MemDataAlignment = DMA_MDATAALIGN_BYTE` 对应 8 位搬运。

USART 发送一次需要一个字节，缓冲区类型也是 `uint8_t`，所以字节宽度匹配。若误设成半字，发送内容会错位或包含异常字节。

### 8.9 `Mode = DMA_NORMAL`

USART 发送采用普通模式，不是循环模式。每次 `HAL_UART_Transmit_DMA()` 发送一段固定长度，发完后停止，下一轮主循环再启动。

如果设成循环模式，字符串可能不断重复发送，和本课“每秒一轮”的现象不一致。

### 8.10 `HAL_DMA_Init()`

`HAL_DMA_Init(&hdma_tx)` 根据方向、自增、宽度、模式和优先级配置 DMA1_Channel4。它准备 DMA 通道，但还没有开始发送。

### 8.11 `__HAL_LINKDMA`

```c
__HAL_LINKDMA(&huart1, hdmatx, hdma_tx);
```

这句把 UART 句柄的发送 DMA 成员 `hdmatx` 指向 `hdma_tx`。之后 `HAL_UART_Transmit_DMA()` 通过 `huart1.hdmatx` 找到 DMA1_Channel4。

### 8.12 HAL 版 DMA/USART 中断

HAL 版还配置：

```c
HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
HAL_NVIC_EnableIRQ(USART1_IRQn);
```

并实现：

```c
void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_tx);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
```

这不是为了让字节开始发送，而是为了让 HAL 在 DMA 搬运完成、USART 最后一位真正发完后更新内部状态。少了它们，`HAL_UART_Transmit_DMA()` 可能第一轮成功，第二轮因为 `huart1` 仍是 BUSY 而失败。

### 8.13 `memcpy()` 与寄存器版 MEM2MEM 的差异

HAL 版主循环中：

```c
memcpy(g_dst, g_src, sizeof(g_dst));
```

这一步由 CPU/标准库完成，不是 DMA MEM2MEM。它对应寄存器版 `dma_mem_copy()` 的功能结果，但不是同一种底层实现。文档里必须分清：HAL 版真正演示的 DMA 是 USART TX DMA。

### 8.14 `HAL_UART_Transmit_DMA()`

```c
HAL_UART_Transmit_DMA(&huart1, g_dst, sizeof(g_dst));
```

它启动 USART1 的 DMA 发送。HAL 内部会设置 DMA 源地址为 `g_dst`，目标外设为 USART1 数据寄存器，长度为 16，并打开 USART TX DMA 请求。

本课代码随后 `HAL_Delay(200)`，给 DMA 和 USART 留出发送时间，再翻转 LED。因为字符串很短，115200 波特率下 16 字节发送时间约几毫秒量级，200ms 足够。注意，延时只是保证时间足够，不负责更新 HAL 的 UART 状态；状态收尾靠上面的 DMA/USART 中断链完成。

## 9. 两个版本真正应该怎么学

寄存器版有两个 DMA 案例：Channel1 做内存拷贝，Channel4 做 USART1_TX 发送。它更适合学习 `MEM2MEM`、`PINC/MINC`、`TCIF` 和 USART `DMAT` 的硬件后果。

HAL 版保留同样的字符串缓冲区流向，但内存拷贝用 `memcpy()`，USART 发送用 DMA。它更适合学习 UART 句柄、DMA 句柄、`hdmatx` 关联和 `HAL_UART_Transmit_DMA()`。

读的时候不要把两个版本机械对号入座。要抓住共同目标：最终都是把 RAM 里的字符串通过 USART1_TX 发送出去。

## 10. 检验问题清单

### 10.1 本课为什么有两个 DMA 通道

因为寄存器版演示了两个不同案例：Channel1 用于 MEM2MEM 内存拷贝，Channel4 用于 USART1_TX 发送。USART1_TX 的通道由芯片映射固定为 DMA1_Channel4。

### 10.2 为什么 USART 发送 DMA 不能打开 `PINC`

因为外设地址始终是 `USART1->DR`。如果外设地址自增，后续字节会写到错误寄存器地址。

### 10.3 为什么 USART 发送 DMA 必须打开 `MINC`

因为内存源是字符串数组。每发送一个字节，DMA 要读取下一个数组元素；不开 `MINC` 会重复发送第一个字节。

### 10.4 `USART_CR3_DMAT` 控制什么

它控制 USART 发送器是否向 DMA 发请求。不开它，DMA1_Channel4 不会按 USART 发送节奏把字节写入 `DR`。

### 10.5 `TCIF1` 和 `TCIF4` 有什么区别

`TCIF1` 表示 DMA1_Channel1 的内存拷贝完成；`TCIF4` 表示 DMA1_Channel4 的 USART 发送搬运完成。两个标志对应不同通道，不能混用。

### 10.6 HAL 版为什么要 `__HAL_LINKDMA(&huart1, hdmatx, hdma_tx)`

因为 `HAL_UART_Transmit_DMA()` 从 UART 句柄里找发送 DMA 句柄。没有关联，UART 和 DMA 初始化各自存在，但 HAL 无法把发送动作交给 DMA1_Channel4。

### 10.7 HAL 版的内存拷贝是不是 DMA 完成的

不是。HAL 版用 `memcpy()` 拷贝 `g_src` 到 `g_dst`，这一步由 CPU/库函数完成；DMA 用在 `HAL_UART_Transmit_DMA()` 发送阶段。

### 10.8 串口输出乱码时优先查什么

优先查波特率、系统时钟、串口助手参数和 GND 共地。乱码通常更像时序问题，而不是 DMA 通道完全不工作。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求是周期性发送一段字符串，并通过 LED 表示主循环运行。寄存器版额外要求先用 DMA 完成一次 RAM 到 RAM 拷贝，再用 DMA 发送目标缓冲区。

### 11.2 硬件核查

PA9 接 USB-TTL RX，开发板 GND 接 USB-TTL GND。串口助手设置 115200、8N1。PC13 用于判断程序节奏。不要把 PA9 接到 USB-TTL TX，否则方向反了。

### 11.3 寄存器路线

进入 `19_dma_memory_uart_cases/reg`，重点读 `usart1_tx_pin_init()`、`usart1_init()`、`dma_init()`、`dma_mem_copy()`、`dma_uart_send()`。确认 GPIOA/USART1/DMA1 时钟打开，PA9 是复用推挽，USART1 打开 `DMAT/TE/UE`，Channel1 使用 `MEM2MEM`，Channel4 使用 `DIR/MINC`。

编译：

```sh
pio run
```

下载：

```sh
pio run -t upload
```

### 11.4 HAL 路线

进入 `19_dma_memory_uart_cases/hal`，重点读 `uart_dma_init()`、`DMA1_Channel4_IRQHandler()`、`USART1_IRQHandler()` 和主循环。确认 `huart1` 是 USART1 TX，`hdma_tx` 是 DMA1_Channel4，方向是 `DMA_MEMORY_TO_PERIPH`，执行了 `__HAL_LINKDMA(&huart1, hdmatx, hdma_tx)`，并打开了 DMA1_Channel4 和 USART1 的 NVIC。

### 11.5 工程思维

DMA 工程调试要分清“搬运是否完成”和“外设是否真的输出”。Channel4 `TCIF` 说明 DMA 已把数据交给 USART 数据寄存器，不代表电脑一定能看到正确字符；电脑侧还依赖 PA9 接线、波特率、USB-TTL 和共地。

### 11.6 常见工程陷阱

最常见的是 PA9/RX 接反、忘记共地、USART 波特率和系统时钟不匹配、忘记 `USART_CR3_DMAT`、DMA 通道选错、USART DMA 误开 `PINC`、HAL 版忘记 `__HAL_LINKDMA()` 或 DMA/USART IRQHandler。这些错误的现象不同，要按路径排查。

## 12. 运行现象

寄存器版正常运行时，串口助手周期性收到 `DMA UART demo`，PC13 每轮翻转一次。由于发送内容长度是 16 字节，可能包含字符串结尾的空字符；串口助手通常只显示可见字符和换行。

HAL 版正常运行时，也会周期性发送同样字符串，LED 约每秒翻转一次。HAL 版第一段拷贝由 `memcpy()` 完成，USART 发送由 DMA 完成。

## 13. 常见问题排查

### 13.1 串口助手完全没有输出

先查 PA9 是否接 USB-TTL RX，GND 是否共地，串口助手是否打开正确端口。再查 USART1 时钟、PA9 复用推挽、`CR1.TE/UE`、`CR3.DMAT` 和 DMA1_Channel4。

### 13.2 串口输出乱码

优先查波特率和时钟。寄存器版假设 PCLK2 为 72MHz，并用 `72000000U / 115200U` 写 `BRR`。如果系统时钟没到 72MHz，波特率就会偏。

### 13.3 只发送第一个字符或字符重复

重点查 DMA1_Channel4 的 `MINC`。USART 发送时内存地址必须自增，否则 DMA 每次都从 `g_dst[0]` 读取。

### 13.4 发送内容不是 `DMA UART demo`

先查内存拷贝是否完成。寄存器版看 Channel1 的 `MEM2MEM`、`PINC/MINC`、`TCIF1` 和 `g_dst` 内容。HAL 版看 `memcpy()` 长度是否正确。

### 13.5 LED 翻转但串口无输出，或 HAL DMA 发送无效

这说明主循环大概率在运行，优先排查 USART/DMA/接线，而不是系统时钟启动或下载问题。

HAL 版还要检查 `HAL_DMA_Init(&hdma_tx)` 是否成功，`hdma_tx.Instance` 是否为 `DMA1_Channel4`，`__HAL_LINKDMA(&huart1, hdmatx, hdma_tx)` 是否在发送前执行，以及 `DMA1_Channel4_IRQHandler()` / `USART1_IRQHandler()` 是否把中断交回 HAL。

## 14. 本课最核心的结论

1. DMA 可以做外设到内存、内存到外设，也可以做内存到内存；关键是方向、地址、自增和触发源不同。
2. MEM2MEM 拷贝不依赖 USART 这类外设请求，打开 `DMA_CCR_MEM2MEM` 后由 DMA 直接搬 RAM 数据。
3. USART1_TX DMA 依赖固定映射的 `DMA1_Channel4`，并且 USART 侧必须打开 `CR3.DMAT`。
4. USART 发送时外设地址固定为 `USART1->DR`，所以 `PINC` 关闭；内存缓冲区要逐字节前进，所以 `MINC` 打开。
5. `TCIF1` 和 `TCIF4` 是不同通道的完成标志，排查时必须对应到具体 DMA 通道。
6. HAL 版 UART DMA 的关键不是只调用发送函数，而是先把 UART 句柄和 TX DMA 句柄通过 `hdmatx` 关联，并提供 DMA/USART 中断入口让 HAL 完成状态收尾。
7. 串口 DMA 调试要同时看芯片内部搬运和板外物理连接，DMA 完成不等于电脑一定收到正确字符。

## 15. 建议你现在怎么读这节课

第一遍画两条路径：`g_src -> g_dst` 和 `g_dst -> USART1->DR -> PA9`。第二遍读寄存器版，分别标出 Channel1 和 Channel4 的 `CPAR/CMAR/CNDTR/CCR`。第三遍读 HAL 版，弄清 `hdma_tx` 如何挂到 `huart1.hdmatx`。第四遍上板，用串口助手观察输出，再用调试器查看 `g_dst`。

## 16. 扩展练习

- 把 `g_src` 改成另一个短字符串，观察串口输出。
- 在寄存器版中去掉 Channel4 的 `MINC`，观察重复字符现象。
- 在寄存器版中去掉 `USART_CR3_DMAT`，观察 DMA 发送是否触发。
- 把 HAL 版 `DMA_NORMAL` 改成 `DMA_CIRCULAR`，观察是否重复发送。
- 用调试器观察 `DMA1->ISR` 中 `TCIF1` 和 `TCIF4` 的变化。

## 17. 下一课预告

上一课：[18_dma_basic](../18_dma_basic/README.md)

下一课：[20_adc_dma](../20_adc_dma/README.md)
