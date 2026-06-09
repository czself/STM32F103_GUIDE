# 24_uart_printf_redirect - printf 重定向到 USART1

## 1. 本课到底在学什么

本课表面现象是：代码里调用 `printf()`，串口助手能周期性看到计数输出，同时 PC13 LED 翻转。

真正要学的是 C 标准库输出和 USART 发送之间的连接：

```text
printf("count=%lu\r\n", count)
  -> C 标准库格式化字符串
  -> 逐字符调用 fputc()
  -> fputc 等待 USART1 TXE
  -> 写 USART1->DR
  -> USART1 从 PA9 输出串口波形
  -> USB-TTL
  -> 串口助手显示文本
```

寄存器版重写 `fputc()`，在里面轮询 `TXE` 并写 `DR`。HAL 版同样重写 `fputc()`，但在里面调用 `HAL_UART_Transmit()` 发送 1 个字节。你要理解：`printf` 本身不知道 USART1，重定向的本质是把标准库的“输出一个字符”钩子接到串口发送函数上。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 解释为什么实现 `fputc()` 后 `printf()` 会从 USART1 输出。
- 说明 `printf`、`fputc`、USART1 三者分别属于哪一层。
- 看懂寄存器版 `fputc()` 中为什么要等待 `TXE`。
- 看懂 HAL 版 `fputc()` 中为什么调用 `HAL_UART_Transmit()`。
- 区分串口没输出、乱码、程序卡住、只输出部分字符的排查方向。
- 理解为什么 `\r\n` 比单独 `\n` 更适合串口终端显示。
- 说明 printf 方便调试，但也会带来阻塞和代码体积开销。

## 3. 本课目录结构

```text
24_uart_printf_redirect/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

两个版本都包含 USART1 初始化和 `fputc()` 重写。区别是寄存器版直接操作 `SR/DR`，HAL 版通过 `HAL_UART_Transmit()` 发送单字节。

## 4. 实验硬件

- STM32F103C8T6 BluePill，板卡 `genericSTM32F103C8`。
- PA9 接 USB-TTL RX，GND 共地。
- PA10 在代码中也配置为输入/接收，但本课主要观察 TX 输出。
- 串口助手设置 115200、8N1。
- PC13 周期翻转，说明主循环在运行。

## 5. 先建立完整脑图

本课按六层理解：

1. 现象层：串口助手周期性打印 `printf redirect count=...` 或 `HAL printf count=...`，LED 翻转。
2. 物理层：PA9 输出 USART1_TX 电平，USB-TTL 转给电脑。
3. 芯片模块层：USART1 负责串口发送，GPIOA 提供 PA9 复用输出，GPIOC 控制 LED。
4. 寄存器层：`BRR` 决定波特率，`SR.TXE` 表示可写，`DR` 承载发送字节，`CR1.TE/UE` 使能 USART。
5. C/CMSIS 层：`printf()` 属于 C 标准库，`fputc()` 是输出钩子，寄存器版在这里写 `USART1->DR`。
6. HAL 工程层：`UART_HandleTypeDef huart1` 描述 USART1，HAL 版 `fputc()` 调用 `HAL_UART_Transmit()`。

## 6. 核心名词解释

### 6.1 `printf` 是什么

`printf()` 是 C 标准库的格式化输出函数，属于软件库层。

它负责把数字、字符串等格式化成字符流。它本身不知道 STM32 的 USART，也不知道 PA9。没有重定向时，嵌入式环境里的 `printf` 可能无输出，或走半主机调试通道。

它解决的是“把数据变成文本”的问题，不解决“文本从哪里出去”的问题。例如 `%lu` 会把 `count` 转成十进制字符，`\r\n` 会变成回车和换行两个字符；这些字符之后交给底层输出函数。桌面程序里底层输出通常是终端或文件，裸机 STM32 里没有默认终端，所以必须由我们接一条输出路径。

本课让 `printf` 的字符流走 USART1，是为了后续调试。以后你想打印变量、状态、错误码，不必每次手写 `usart1_send_string()`。但便利性有代价：格式化会占用 CPU，复杂格式会增加代码体积，发送字符还会阻塞。

### 6.2 `fputc` 是什么

`fputc()` 是 C 标准库输出单个字符的底层函数。

很多嵌入式工程通过重写 `fputc()` 接管 `printf` 的字符输出。本课中，`printf` 格式化出的每个字符最终都会进入我们写的 `fputc()`。

它属于 C 标准库和用户底层驱动之间的钩子层。`printf()` 负责把一整句格式化成一个个字符，`fputc()` 负责处理“现在要输出这一个字符”。所以只要我们把 `fputc()` 改成发送到 USART1，`printf()` 的整句输出就会自然从串口出现。

代码位置在两个版本的 `src/main.c`。寄存器版 `fputc()` 轮询 `USART1->SR.TXE` 后写 `USART1->DR`；HAL 版 `fputc()` 把字符放进 `uint8_t b`，再调用 `HAL_UART_Transmit()`。同一个函数名背后可以接不同底层实现，这就是重定向的本质。

### 6.3 `重定向` 是什么

重定向是把标准输出从默认位置改到 USART1。

本课不是改变 `printf` 的格式化能力，而是改变格式化后字符的去向。字符原本不知道发到哪里，现在被送到 USART1。

从六层链路看，重定向完全属于软件工程层：它不改变 USART1 的硬件结构，也不改变 PA9 的物理连接，只是在 C 标准库和串口发送函数之间搭桥。桥搭好后，应用层只写 `printf()`，底层自动走 `fputc -> USART1`。

如果没有这座桥，`printf()` 可能什么都不显示，或者依赖半主机机制通过调试器输出。半主机在调试时可能看似方便，但脱离调试器后容易卡住。本课的目标是把输出路径明确固定到 USART1，这样下载运行也能通过串口助手看到日志。

### 6.4 `FILE *stream` 是什么

`FILE *stream` 是 C 标准库传给 `fputc()` 的输出流参数。

本课不区分 stdout/stderr，所以用 `(void)stream;` 明确表示不使用它。它属于 C 标准库层，不是 STM32 外设。

桌面 C 程序里，`stream` 可以代表标准输出、标准错误或某个文件。裸机课程里我们只关心“所有 printf 字符都发到 USART1”，所以不需要判断它。`(void)stream;` 的作用是告诉编译器：这个参数我知道存在，只是本实现故意不用，避免未使用参数警告。

这个参数也提醒你：`fputc()` 的函数签名不是我们随便设计的，而是标准库期待的接口。签名写错时，`printf()` 未必会调用你的函数，现象就是你明明写了串口发送代码，但 `printf` 仍然没有输出。

### 6.5 `USART1` 是什么

USART1 是 STM32F103 的串口外设。

本课使用它的异步发送功能，把字符转换成 PA9 上的串行波形。USART1 时钟、GPIO 复用和波特率都必须先配置正确。

### 6.6 `PA9` 是什么

PA9 是 USART1_TX 默认引脚。

它必须配置为复用推挽输出，让 USART1 控制引脚电平。若 PA9 没接 USB-TTL RX，`printf` 在芯片内部发送了，电脑也看不到。

### 6.7 `BRR` 是什么

`BRR` 是 USART 波特率寄存器。

寄存器版用 `72000000U / 115200U` 写入近似值。HAL 版根据 `BaudRate=115200` 和时钟自动计算。波特率错会乱码。

这里要特别注意：前几课精确计算 72MHz、115200、16 倍过采样时 `BRR=0x0271`。本课寄存器源码为了突出 printf 重定向，写了一个简化近似值 `72000000U / 115200U`，结果约为 625，也接近 `0x0271` 的十进制 625。它能工作，但教学上仍要知道标准算法是整数部分和小数部分编码。

工程上更推荐沿用精确 `0x0271` 或写一个明确的 BRR 计算函数。HAL 版通过 `huart1.Init.BaudRate = 115200` 让 `HAL_UART_Init()` 根据当前 PCLK 自动算，减少手写误差。若输出乱码，本课也仍然优先查系统时钟、BRR、串口助手参数和共地。

### 6.8 `TXE` 是什么

`TXE` 是发送数据寄存器空标志。

寄存器版 `fputc()` 先等待 `TXE=1`，再写 `DR`。这样避免覆盖尚未处理的发送数据。

在 `printf()` 场景中，`TXE` 会被频繁检查。打印一行几十个字符，就会进入 `fputc()` 几十次，每次都要等待 USART 可以接收下一个字节。115200 下一个 8N1 字节约 86.8us，长日志会明显占用 CPU 时间。

它属于 USART 状态寄存器层，而不是 printf 自己的状态。`printf()` 不知道 TXE，`fputc()` 作为桥接函数必须知道。桥的一端接标准库字符流，另一端接 USART 的硬件发送规则。

### 6.9 `DR` 是什么

`DR` 是 USART 数据寄存器。

写入一个字节后，USART 硬件会按串口帧格式从 PA9 发出。`printf` 的每个字符最终都变成一次写 DR。

### 6.10 `HAL_UART_Transmit` 是什么

`HAL_UART_Transmit()` 是 HAL 的轮询发送函数。

HAL 版 `fputc()` 把单个字符放进 `uint8_t b`，然后调用 `HAL_UART_Transmit(&huart1, &b, 1, 20)`。它内部等待 TXE/TC 或超时。

这里的长度参数是 1，表示每次只发送一个字符。超时参数 20ms 表示如果 HAL 在这个时间内无法完成单字节发送，会返回超时或错误。本课没有检查返回值，是为了保持源码短；工程中更严谨的做法是判断返回值，避免 UART 异常时静默丢日志。

它对应寄存器版 `while TXE -> DR = ch`。HAL 会通过 `huart1` 找到 USART1，检查状态、等待标志、写数据寄存器。理解寄存器版后，你就知道 HAL 版 `fputc()` 不是魔法，只是把底层等待和写寄存器封装进 API。

### 6.11 `\r\n` 是什么

`\r\n` 是回车加换行。

许多串口助手需要 `\r` 回到行首、`\n` 换到下一行。只发 `\n` 可能出现光标不回行首的显示问题。

### 6.12 `半主机` 是什么

半主机是调试器辅助实现标准输入输出的一种机制。

如果工程没有正确重定向 `printf`，标准输出可能依赖调试器，脱离调试器后卡住或无输出。本课通过 `fputc()` 把输出明确接到 USART1。

半主机属于调试工具链层，不是 STM32 片上外设。它让目标板通过调试器向主机请求文件或控制台 I/O。问题是裸机独立运行时没有调试器响应这些请求，程序可能停在库函数里，表现为 LED 不翻转、串口没输出。

本课选择 USART1 重定向，就是为了让日志输出不依赖调试器。只要板子运行、PA9 接到 USB-TTL、串口助手参数正确，就能看到文本。这也是嵌入式工程里最常用的基础日志方式。

### 6.13 `阻塞发送` 是什么

阻塞发送是函数在字符发出前一直等待。

寄存器版等待 TXE，HAL 版等待 HAL_UART_Transmit 完成。`printf` 输出很长时，会占用 CPU 时间。

### 6.14 `代码体积开销` 是什么

`printf` 格式化功能会引入标准库代码，尤其浮点格式会显著增加 Flash 占用。

本课只打印整数计数，开销较可控。工程中要谨慎使用大量 `printf`。

### 6.15 `HAL UART 句柄` 是什么

`UART_HandleTypeDef huart1` 是 HAL 管理 USART1 的句柄。

HAL 版 `fputc()` 依赖它。若 `uart1_init()` 没在 `printf()` 前完成，`HAL_UART_Transmit()` 没有有效外设上下文。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟

`system_clock_72mhz_init()` 设置 Flash 等待周期、HSE、PLL 和总线分频。USART1 波特率依赖 PCLK2。

### 7.2 PC13 LED

`pc13_led_init()` 打开 GPIOC 时钟，配置 PC13 推挽输出，主循环中周期翻转。它说明程序没有卡死在 `printf` 或串口发送中。

### 7.3 USART1 GPIO

`uart1_init()` 打开 GPIOA、USART1、AFIO 时钟。PA9 配复用推挽输出，PA10 配输入。

### 7.4 USART1 波特率和收发使能

`USART1->BRR = 72000000U / 115200U` 设置波特率近似值。`CR1 = TE | RE | UE` 打开发送、接收和 USART 总使能。

这句近似值成立的前提仍然是 PCLK2 为 72MHz。严格的 F1 USART BRR 编码应写成 `0x0271`，本课源码的除法结果也是 625，恰好接近这个编码值。读源码时不要误以为所有波特率都能简单用 `时钟 / 波特率` 填进 `BRR`；这是本课为突出 `printf -> fputc` 链路而简化。

`TE` 是必须的，因为 printf 只用发送；`RE` 在本课不是观察重点，但代码也打开了接收器，保持 USART1 为收发模式。`UE` 是总使能，没有它，BRR 和 TE/RE 都不会让外设真正工作。

### 7.5 重写 `fputc`

函数签名是：

```c
int fputc(int ch, FILE *stream)
```

标准库调用它输出单字符。`stream` 本课不使用。

函数返回 `ch`，表示这个字符已经被处理。标准库会连续调用它输出整段格式化文本。比如 `printf("count=%lu\r\n", count)` 不是一次性把整行交给 USART，而是最终拆成 `c`、`o`、`u`、`n`、`t` 等字符逐个输出。

这也是为什么 `fputc()` 里不能写太复杂逻辑。它会被调用很多次，一行日志几十个字符就调用几十次。如果每个字符都做大量计算，`printf` 的阻塞时间会进一步变长。

### 7.6 等待 TXE

`while((USART1->SR & USART_SR_TXE)==0U){}` 等待发送数据寄存器空。这样写 DR 是安全的。

### 7.7 写 DR

`USART1->DR=(uint8_t)ch` 把字符交给 USART1。USART 硬件随后从 PA9 输出串口帧。

### 7.8 主循环 printf

主循环每轮调用：

```c
printf("printf redirect count=%lu\r\n", (unsigned long)count++);
```

`printf` 负责格式化，`fputc` 负责逐字符发送。

主循环里 LED 翻转和延时让你同时观察两个现象：串口文本是否出现，程序是否还在继续运行。如果串口没有输出但 LED 仍翻转，问题更可能在 USART/接线/重定向；如果 LED 也不翻转，程序可能卡在 `printf`、`fputc`、时钟初始化或半主机相关位置。

### 7.9 软件延时

`delay_cycles(7200000U)` 做粗略延时。它不精确，但足够让串口输出周期可观察。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化

`HAL_Init()` 准备 HAL Tick。`SysTick_Handler()` 调用 `HAL_IncTick()`，使 `HAL_Delay()` 工作。

### 8.2 HAL 时钟配置

`HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 配置 HSE、PLL、PCLK2 和 Flash 延迟。

### 8.3 HAL GPIO 配置

PA9 使用 `GPIO_MODE_AF_PP`，PA10 使用 `GPIO_MODE_INPUT`。PC13 使用 `GPIO_MODE_OUTPUT_PP`。

### 8.4 UART 句柄初始化

`huart1.Instance = USART1`，`BaudRate=115200`，`Mode=UART_MODE_TX_RX`。`HAL_UART_Init()` 写 BRR、CR1 等底层寄存器。

### 8.5 HAL 版 `fputc`

HAL 版把 `ch` 转成 `uint8_t b`，调用：

```c
HAL_UART_Transmit(&huart1, &b, 1, 20);
```

这相当于把寄存器版“等 TXE、写 DR”的动作交给 HAL。

HAL 版 `fputc()` 依赖全局 `huart1` 已经初始化完成。若在 `uart1_init()` 之前调用 `printf()`，`HAL_UART_Transmit()` 拿到的句柄没有正确配置，轻则返回错误无输出，重则卡在错误状态。主函数里的顺序必须是 `HAL_Init -> 时钟 -> GPIO/USART -> printf`。

本课每个字符都调用一次 `HAL_UART_Transmit()`，函数调用开销比直接寄存器写更大，但工程可读性更强。大量日志输出时，后续可以把 `printf` 重定向到底层环形缓冲或 DMA 发送，减少单字符阻塞。

### 8.6 `printf` 调用顺序

必须先 `HAL_Init()`、配置时钟、初始化 GPIO 和 USART1，再调用 `printf()`。否则 `fputc()` 中的 UART 发送没有可用硬件配置。

### 8.7 `HAL_Delay`

HAL 版主循环用 `HAL_Delay(1000)`。它依赖 SysTick，比寄存器版粗略循环更清晰。

`HAL_Delay()` 不是忙等空循环，它依赖 HAL Tick 计数。源码里重写 `SysTick_Handler()` 并调用 `HAL_IncTick()`，就是为了让 HAL 的毫秒计数推进。如果这个中断函数缺失或被别的文件覆盖，`HAL_Delay()` 可能一直等不到时间变化，表现为只打印一次或 LED 不再翻转。

### 8.8 HAL 时钟返回值

本课 HAL 源码为了短小，没有检查 `HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 的返回值。但按工程写法，这两个 API 应该判断是否返回 `HAL_OK`，失败就进入错误处理。

原因是后面的 USART 波特率计算依赖时钟配置。如果 HSE 没起或 PLL 没切成功，`huart1.Init.BaudRate=115200` 算出来的 BRR 会基于错误时钟，串口可能乱码。文档里必须看见这层依赖，不能只说 HAL 自动计算。

### 8.9 HAL_UART_Init 与 fputc 的先后关系

`HAL_UART_Init(&huart1)` 必须在第一次 `printf()` 前完成。这个 API 会根据句柄字段配置 `BRR`、`CR1`、`CR2`、`CR3`，让 USART1 处于可发送状态。

`fputc()` 只是 printf 的出口，它不负责初始化 UART。如果初始化顺序反了，`fputc()` 里调用 `HAL_UART_Transmit()` 时，HAL 句柄还没有可用硬件上下文。排查“上电第一句 printf 卡住”时，初始化顺序是第一批检查项。

### 8.10 单字符发送的效率问题

HAL 版每输出一个字符都调用一次 `HAL_UART_Transmit()`，这比一次发送整段字符串开销更大。对本课低频日志来说没问题，但对高频日志、控制环或中断上下文并不合适。

后续工程可以把 `printf` 输出先放入软件环形缓冲区，再由 USART 中断或 DMA 批量发送。那样 `printf` 和串口物理发送之间会多一层缓冲，本课先用阻塞单字符方式，是为了把重定向机制讲清楚。

## 9. 两个版本真正应该怎么学

寄存器版能看到重定向的底层本质：`fputc -> TXE -> DR`。HAL 版能看到工程封装：`fputc -> HAL_UART_Transmit -> UART 句柄 -> USART1`。

两者都说明一件事：`printf` 不会自动跑到串口，必须给标准库一个输出字符的实现。

## 10. 检验问题清单

### 10.1 为什么实现 `fputc` 后 printf 能输出到串口

**答**：因为 printf 格式化后会通过底层字符输出函数输出，重写 fputc 后，每个字符都被送到 USART1。

### 10.2 `printf` 和 `USART1` 之间直接认识吗

**答**：不认识。它们通过 `fputc()` 这个软件钩子连接。

### 10.3 寄存器版为什么要等 TXE

**答**：TXE=1 表示可以写下一个发送字节，避免覆盖数据。

### 10.4 HAL 版 `fputc` 为什么要先初始化 huart1

**答**：`HAL_UART_Transmit()` 需要有效 UART 句柄和已初始化的 USART1。

### 10.5 为什么推荐 `\r\n`

**答**：串口终端通常需要回车和换行一起使用，显示才规整。

### 10.6 串口乱码优先查什么

**答**：查系统时钟、波特率、串口助手 115200 8N1、GND 共地。

### 10.7 printf 有什么工程代价

**答**：它可能阻塞 CPU，并增加 Flash 代码体积，尤其复杂格式和浮点输出。

### 10.8 如果 LED 不翻转但串口也没输出，可能卡在哪

**答**：可能卡在 printf/fputc 的发送等待、USART 未初始化、半主机或时钟配置问题。

## 11. 工程实现步骤

### 11.1 需求分析

需求是让普通 `printf()` 文本从 USART1 PA9 输出，作为后续调试日志基础。

拆成链路就是三步：先把 USART1 和 PA9 配成能正常发送；再实现标准库期望的字符输出函数 `fputc()`；最后在主循环里调用普通 `printf()` 验证。只改 `printf` 调用不够，只配 USART 也不够，中间的 `fputc` 桥必须存在。

### 11.2 硬件核查

PA9 接 USB-TTL RX，GND 共地，串口助手 115200 8N1。PC13 用于确认主循环运行。

### 11.3 寄存器路线

进入 `24_uart_printf_redirect/reg`，重点读 `uart1_init()` 和 `fputc()`。

```sh
pio run
pio run -t upload
```

### 11.4 HAL 路线

进入 `24_uart_printf_redirect/hal`，重点读 `uart1_init()`、HAL 版 `fputc()` 和 `SysTick_Handler()`。

### 11.5 工程思维

printf 很适合调试，但不能无限制使用。频繁打印会影响实时性，发布版本常需要降低日志量或改用环形缓冲/DMA 日志。

本课使用阻塞式单字符发送，是最容易理解的日志方式。工程上如果在中断里调用 `printf`，或者在高频控制循环里大量打印，会严重影响时序。更稳的方式是主循环低频打印、设置日志等级、把日志写入缓冲区，再由串口 DMA 后台发送。

### 11.6 常见工程陷阱

忘记实现 `fputc()`、在 USART 初始化前调用 `printf()`、PA9 接线错误、波特率不一致、半主机配置导致卡住、打印太长导致主循环阻塞，都会造成问题。

还要注意链接库配置。有些工具链使用 `_write()` 作为更底层的输出钩子，有些使用 `fputc()` 就足够。本课代码在当前 PlatformIO/stm32cube 环境下通过重写 `fputc()` 建立路径；如果迁移到别的工程，发现 `fputc()` 不进，就要检查所用 C 库的 retarget 规则。

另一个陷阱是把 printf 当作实时日志。串口 115200 的吞吐有限，一秒最多也就一万多个字符量级，实际还要扣掉格式化和阻塞等待。打印太密会改变程序时序，调试时尤其要知道“观察行为本身也会影响系统”。

## 12. 运行现象

寄存器版串口周期输出：

```text
printf redirect count=0
printf redirect count=1
...
```

HAL 版串口周期输出：

```text
HAL printf count=0
HAL printf count=1
...
```

同时 PC13 周期翻转。

## 13. 常见问题排查

### 13.1 串口没有任何 printf 输出

检查 PA9 接线、USART1 时钟、PA9 复用推挽、TE/UE、`fputc()` 是否被编译进工程。

可以先分层判断：LED 是否翻转，证明主循环运行；`printf()` 是否执行到，证明应用层没跳过；`fputc()` 能否打断点进入，证明标准库确实调用了你的重定向；最后再查 TXE、DR、PA9 和 USB-TTL。这样能区分“printf 没走到 fputc”和“fputc 走了但串口没出波形”。

### 13.2 输出乱码

检查系统时钟、BRR 或 HAL BaudRate、串口助手参数和 GND。

### 13.3 程序卡住不翻转 LED

检查是否卡在 `fputc()` 等 TXE，USART 是否已经初始化，是否存在半主机依赖。

若卡在寄存器版 `while TXE`，说明 USART 发送状态没有变成可写，重点查 `UE/TE`、USART1 时钟和初始化顺序。若卡在库函数内部而不是你的 `fputc()`，要怀疑半主机或链接配置。HAL 版则看 `HAL_UART_Transmit()` 是否长时间等待状态或超时处理不当。

### 13.4 只输出部分字符

检查 TXE 等待、HAL_UART_Transmit 超时时间、USB-TTL 连接是否稳定，打印频率是否过高。

寄存器版若等待 TXE 逻辑被删掉，连续写 `DR` 可能丢字符；HAL 版若超时时间太短，单字符发送还没完成就返回，后续字符可能丢失。本课 HAL 超时 20ms 对 115200 单字节足够，但如果系统时钟错、USART 状态异常，仍可能超时。

### 13.5 HAL_Delay 不工作

检查 `SysTick_Handler()` 是否调用 `HAL_IncTick()`，以及 `HAL_Init()` 是否执行。

## 14. 本课最核心的结论

1. `printf` 是标准库格式化输出，不天然知道 USART。
2. `fputc()` 是把 printf 字符流接到 USART1 的关键钩子。
3. 寄存器版 `fputc` 的核心是等待 TXE 后写 DR。
4. HAL 版 `fputc` 的核心是调用 `HAL_UART_Transmit()` 发送单字节。
5. USART1 的 PA9、BRR、TE/UE 仍然必须先配置正确。
6. printf 调试方便，但会阻塞并增加代码体积。
7. 后续复杂日志可考虑中断、DMA 或环形缓冲减少阻塞。

## 15. 建议你现在怎么读这节课

先读 `main()` 里普通的 `printf()`，再跳到 `fputc()` 看字符如何进入 USART。最后对照寄存器版和 HAL 版，确认一个是直接写 DR，一个是让 HAL 帮你写 DR。

## 16. 扩展练习

- 修改输出格式，增加十六进制计数。
- 把 `\r\n` 改成 `\n`，观察串口助手显示差异。
- 在 HAL 版把 `HAL_UART_Transmit` 超时改小，观察大量输出时的影响。
- 尝试打印浮点数，观察固件体积变化。

## 17. 下一课预告

上一课：[23_uart_dma](../23_uart_dma/README.md)

下一课：[25_uart_packet_protocol](../25_uart_packet_protocol/README.md)
