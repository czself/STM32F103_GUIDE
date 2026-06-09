# 23_uart_dma - USART1 DMA 发送

## 1. 本课到底在学什么

本课表面现象是：串口助手每秒收到一行 DMA 发送的字符串，PC13 在每次 DMA 发送完成后翻转。

真正要学的是 USART 发送 DMA 链路：

```text
内存中的字符串
  -> DMA1_Channel4
  -> USART1->DR
  -> USART1 发送移位器
  -> PA9 / USART1_TX
  -> USB-TTL
  -> 电脑串口助手

DMA 搬完整段字符串
  -> TCIF4 置位
  -> DMA1_Channel4_IRQn
  -> 清标志、关 DMA、关 DMAT
  -> g_uart_dma_done = 1
  -> 主循环翻转 LED
```

上一课讲 USART 接收中断，CPU 仍然在发送时逐字节写 `DR`。本课把发送整段字符串这件事交给 DMA。CPU 只启动一次，DMA 按 USART1_TX 的节奏把内存字节送进 `USART1->DR`。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 解释为什么 USART1_TX 使用 `DMA1_Channel4`。
- 说明 `USART_CR3_DMAT` 为什么是 USART 发送 DMA 的外设侧开关。
- 区分 `TXE`、`TC`、`TCIF4` 的含义。
- 看懂 `CPAR=&USART1->DR`、`CMAR=字符串地址`、`CNDTR=长度`。
- 解释为什么 USART TX DMA 需要 `DIR=1`、`MINC=1`、`PINC=0`。
- 说明 `g_uart_dma_busy` 和 `g_uart_dma_done` 的作用。
- 看懂 HAL 版 `__HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx)`、`HAL_UART_Transmit_DMA()`、`HAL_UART_TxCpltCallback()` 的关系。

## 3. 本课目录结构

```text
23_uart_dma/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

寄存器版直接配置 DMA1_Channel4 和 USART1 的 `DMAT`。HAL 版用 UART TX DMA 句柄和回调完成同样链路。

## 4. 实验硬件

- STM32F103C8T6 BluePill，板卡 `genericSTM32F103C8`。
- PA9 接 USB-TTL RX，GND 共地。
- 本课只发送，不需要 PA10 接收也能观察现象。
- 串口助手设置 115200、8N1。
- PC13 为发送完成指示。

## 5. 先建立完整脑图

本课按六层理解：

1. 现象层：串口每秒出现一行字符串，LED 每次完成发送后翻转。
2. 物理层：PA9 输出 USART1_TX 串行波形，经 USB-TTL 到电脑。
3. 芯片模块层：DMA1_Channel4 搬运字符串，USART1 发送字节，SysTick 提供 1ms 延时，GPIOC 控制 LED。
4. 寄存器层：`DMA1_Channel4->CPAR/CMAR/CNDTR/CCR` 配搬运，`USART1->CR3.DMAT` 允许发送 DMA 请求，`DMA1->ISR.TCIF4` 表示搬运完成。
5. C/CMSIS 层：`g_uart_dma_busy/done` 是 DMA ISR 与主循环之间的状态交接，`SysTick_Handler` 驱动 `delay_ms()`。
6. HAL 工程层：`DMA_HandleTypeDef` 描述 Channel4，`huart1.hdmatx` 关联 TX DMA，回调通知发送完成。

## 6. 核心名词解释

### 6.1 `DMA1_Channel4` 是什么

`DMA1_Channel4` 是 DMA1 的第 4 通道，属于 DMA 硬件层。

STM32F103 中 USART1_TX 的 DMA 请求固定映射到它。这个映射不是软件随便选的。选错通道时，USART1_TX 请求不会触发 DMA 搬运。

它和前面 ADC DMA 的 `DMA1_Channel1` 是同一个 DMA 控制器里的不同通道。ADC1 使用 Channel1，USART1_TX 使用 Channel4，USART1_RX 使用 Channel5，这些都是芯片内部请求线的固定连接。软件能配置通道里的地址、长度、方向和宽度，但不能把 USART1_TX 随便改到 Channel2。

本课必须讲通道映射，因为 DMA 出问题时，寄存器看起来可能都写了，`CCR.EN` 也置位了，但如果通道不是外设请求对应的通道，硬件请求根本不会到这个 DMA 通道。现象通常是欢迎信息能用轮询发出，DMA 字符串完全没有。

### 6.2 `USART1_TX DMA` 是什么

USART1_TX DMA 是用 DMA 给 USART1 发送器喂数据的方式。

USART 发送器需要一个字节一个字节写 `DR`。DMA 可以自动从内存取下一个字节写入 `DR`，减少 CPU 重复等待 TXE 的工作。

它属于外设请求驱动的内存到外设搬运。数据源是内存里的 `g_dma_message`，目标是固定的 `USART1->DR`。USART1 发送器在需要下一个字节时产生 DMA 请求，DMA 才搬一个字节过去，因此发送速度仍由 USART 波特率决定，不是 DMA 想多快就多快。

和前面 ADC DMA 的方向正好相反：ADC 是外设产生数据，DMA 写到内存；UART TX 是内存已有数据，DMA 写到外设。对比这两课，你应该能看到 `CPAR/CMAR` 名字不变，但 `DIR` 决定数据到底从哪边流向哪边。

### 6.3 `USART_CR3_DMAT` 是什么

`DMAT` 是 USART 发送 DMA 使能位，位于 USART `CR3`。

它是 USART 外设侧开关。打开后，USART1 发送侧才会向 DMA 发请求。DMA 通道使能但 `DMAT=0` 时，发送 DMA 不会按 USART 节奏工作。

`DMAT` 可以理解为 DMA enable transmitter。它不配置 DMA 地址，也不决定 DMA 通道；它只决定 USART1 的发送器在需要数据时，是否向 DMA 控制器发请求。寄存器版在 `usart1_dma_send()` 里启动前设置 `USART1->CR3 |= USART_CR3_DMAT`，完成中断里再清掉。

这也是 DMA 外设联动里常见的双侧开关：DMA 通道自己要 `CCR.EN=1`，外设侧也要允许 DMA 请求。只打开 DMA 通道，好比搬运者站好了但没人叫；只打开 `DMAT`，好比 USART 在叫但 DMA 通道还没准备好。

### 6.4 `CPAR = &USART1->DR` 是什么

`CPAR` 是 DMA 外设地址寄存器。

本课方向是内存到外设，所以 `CPAR` 是目标外设地址，也就是 `USART1->DR`。DMA 每次把一个字节写到这个固定地址。

这里特别容易受 ADC DMA 课程影响而误解。`CPAR` 不永远代表数据源，它代表外设端地址。当 `DIR=0` 时外设到内存，`CPAR` 是源；当 `DIR=1` 时内存到外设，`CPAR` 是目标。本课 `DIR=1`，所以 DMA 会把内存字节写入 `USART1->DR`。

`PINC=0` 必须和这句配套，因为 USART 数据寄存器只有一个固定地址。每个字符都要写到同一个 `DR`，由 USART 硬件再串行发出。若外设地址自增，第二个字节就会写到 `DR` 后面的错误地址，轻则不发送，重则破坏 USART 其他寄存器。

### 6.5 `CMAR = data` 是什么

`CMAR` 是 DMA 内存地址寄存器。

本课它指向待发送字符串的首地址。字符串在内存中连续排列，DMA 通过 `MINC=1` 逐字节向后读取。

`data` 在 `usart1_dma_send(const uint8_t *data, uint16_t len)` 中是函数参数，调用时传入 `g_dma_message`。这说明 DMA 发送函数并不绑定某一个固定字符串；只要传入内存地址和长度，就能发送一段连续字节。

由于 DMA 是异步发送，启动后这段内存在发送完成前不能被修改或释放。本课 `g_dma_message` 是全局静态数据，生命周期足够长。真实工程若用局部数组启动 DMA，函数返回后栈内容可能被覆盖，串口输出就会乱。

### 6.6 `CNDTR = len` 是什么

`CNDTR` 是 DMA 传输数量寄存器。

USART 发送按 8 位字节搬运，所以本课 `len` 表示发送多少个字节。代码用 `sizeof(g_dma_message) - 1` 去掉字符串末尾 `\0`。

和 ADC DMA 的 `CNDTR=16` 一样，`CNDTR` 的单位是数据项，不是固定字节。但本课 `PSIZE/MSIZE=8 位`，所以一个数据项正好是一个字节。DMA 每成功写一次 `USART1->DR`，内部计数减 1，减到 0 后置位 Channel4 的传输完成标志。

为什么要 `sizeof(g_dma_message) - 1`？C 字符串末尾有 `'\0'` 结束符，它用于 C 函数判断字符串结束，不需要发到串口助手。若把 `'\0'` 也发出去，很多串口助手看不见这个字符，但它确实占用一个发送时隙，调试协议时可能造成困惑。

### 6.7 `DIR=1` 是什么

`DIR` 是 DMA 方向位。

`DIR=1` 表示内存到外设。本课数据从字符串内存流向 `USART1->DR`，所以必须为 1。若方向反了，DMA 会尝试从外设读到内存，不会完成发送。

方向位决定 `CPAR` 和 `CMAR` 谁读谁写。寄存器版设置 `DMA1_Channel4->CCR |= DMA_CCR_DIR`；HAL 版对应 `hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH`。这两句表达同一件事：内存中的字符串是源，USART 数据寄存器是目标。

方向错误的现象通常不是乱码，而是 DMA 发送根本不按预期发生，因为 USART TX 的请求语义是让 DMA 往 `DR` 填数据。看到 DMA 信息没有而欢迎信息正常时，要把 `DIR` 和 `DMAT` 放在同一层检查。

### 6.8 `MINC=1` 是什么

`MINC` 是内存地址自增。

字符串每个字节在不同地址，DMA 每发送一个字节后必须读下一个地址。不开 `MINC` 会重复发送第一个字符。

### 6.9 `PINC=0` 是什么

`PINC` 是外设地址自增。

USART 数据寄存器地址固定，每个字节都写 `USART1->DR`。所以 `PINC` 必须关闭。误开后会写到错误寄存器地址。

### 6.10 `DMA_CCR_TCIE` 是什么

`TCIE` 是 DMA 传输完成中断使能位。

本课打开它，让 DMA 搬完整段字符串后进入 `DMA1_Channel4_IRQHandler()`。没有它，DMA 可能搬完了，但 CPU 不知道何时完成。

`TCIE` 控制的是 DMA 侧完成事件要不要通知 CPU。DMA 即使不打开中断，也可以把数据搬完并置位 `TCIF4`；但主循环不知道什么时候完成，就无法清 busy、翻转 LED、准备下一轮。本课用完成中断把硬件完成事件转换成软件标志。

它和 USART 的 `TC` 不是一回事。`TCIE/TCIF4` 属于 DMA 通道，表示内存到外设数据寄存器的搬运完成；`USART_SR_TC` 属于 USART，表示最后一个字节已经从物理 TX 线上发完。DMA 搬完时，最后一个字节可能刚进入 USART，还没完全发出。

### 6.11 `TCIF4` 是什么

`TCIF4` 是 DMA1 Channel4 传输完成标志，位于 `DMA1->ISR`。

ISR 检查它确认是 Channel4 完成，再写 `DMA1->IFCR` 清标志。标志不清会影响下一次判断。

`TCIF4` 中的 4 表示 Channel4。DMA1 的多个通道共用 `ISR/IFCR` 这类状态寄存器，所以每个通道都有自己的完成、半传输、错误、全局标志。ISR 里判断 `DMA1->ISR & DMA_ISR_TCIF4`，就是确认本次处理的是 Channel4 传输完成。

清标志不是写回 `ISR`，而是写 `IFCR`。寄存器版执行 `DMA1->IFCR = DMA_IFCR_CGIF4 | DMA_IFCR_CTCIF4`。如果忘记清，下一轮启动前旧完成标志还在，软件可能误以为新一轮已经完成。

### 6.12 `TC` 是什么

`TC` 是 USART Transmission Complete，位于 USART `SR`。

`TXE` 只表示数据寄存器空，`TC` 表示最后一个字节物理发送完成。本课启动 DMA 前等待 TC，避免上一段发送尾巴还没结束。

`TXE`、`DMA TCIF4`、`USART TC` 是三个不同阶段。`TXE` 表示 USART 可以接收下一个字节；`TCIF4` 表示 DMA 已把最后一个字节写进 USART；`USART TC` 表示最后一个字节连停止位都已经从 PA9 发完。串口物理发送有时间，不能把写入 `DR` 误认为电脑已经收到。

寄存器版在 `usart1_send_string_polling()` 和 `usart1_dma_send()` 中都关注 `TC`。前者为了欢迎信息完全发完，后者为了启动新一轮 DMA 前避免上一段发送尾部还在路上。

### 6.13 `g_uart_dma_busy` 是什么

`g_uart_dma_busy` 是软件状态标志。

DMA 发送进行中时置 1，防止主循环重复启动同一个 DMA 通道。完成中断中清 0。

它属于 C 软件状态层，保护的是异步 DMA 操作。DMA 通道启动后不会阻塞 `main()` 等它结束，主循环可能很快又调用发送函数。如果没有 busy 标志，就可能在上一轮 `CNDTR/CMAR` 还在工作时改写这些寄存器，导致输出截断、重复或状态卡死。

寄存器版在 `usart1_dma_send()` 开头检查 busy，启动前置 1，DMA 完成 ISR 清 0。HAL 版也在主循环启动前置 busy，在 `HAL_UART_TxCpltCallback()` 中清 busy。两个版本都体现启动和完成不在同一个时间点。

### 6.14 `g_uart_dma_done` 是什么

`g_uart_dma_done` 是完成通知标志。

DMA ISR 置 1，主循环看到后清 0 并翻转 LED。它把中断完成事件交给主循环处理。

### 6.15 `HAL_UART_Transmit_DMA` 是什么

`HAL_UART_Transmit_DMA()` 是 HAL 的 UART DMA 发送函数。

它通过 `huart1.hdmatx` 找到 DMA 句柄，配置源地址、目标地址、长度，打开 USART DMAT，并启动 DMA。

### 6.16 `__HAL_LINKDMA` 是什么

`__HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx)` 把 UART 句柄的发送 DMA 成员指向 DMA1_Channel4 句柄。

没有它，HAL UART 发送函数不知道该使用哪个 DMA 通道。

### 6.17 `HAL_UART_TxCpltCallback` 是什么

这是 HAL UART 发送完成回调。

HAL 在 DMA 和 USART 发送收尾完成后调用它。本课在回调里清 busy、置 done，让主循环翻转 LED。

## 7. 寄存器版代码逐步讲解

### 7.1 SysTick 周期基准

`systick_init()` 配置 SysTick 每 1ms 中断一次，`g_ms_ticks` 自增。`delay_ms(1000)` 用它实现每秒发送。

### 7.2 USART1 GPIO

PA9 配成复用推挽输出。PA10 在本课保持输入配置，但实际只发送，不依赖接收。

### 7.3 USART1 基础发送配置

`BRR=0x0271` 配 115200。`CR1=TE` 打开发送器，再设置 `UE` 打开 USART。因为本课不接收，所以不设 RE。

### 7.4 DMA1_Channel4 初始化

打开 DMA1 时钟，关闭 Channel4，设置 `CPAR=&USART1->DR`，清相关 `CCR` 位，再设置 `DIR/MINC/TCIE`。

这一步没有写 `CMAR/CNDTR`，因为本课每次发送的缓冲区地址和长度可能不同，放到 `usart1_dma_send()` 启动时再写。初始化阶段只配置那些不随消息变化的规则：通道、外设地址、方向、自增、宽度、完成中断。

`PSIZE/MSIZE` 保持默认 8 位是有意的。USART 发送就是一个字节一个字节，所以不需要设置半字或字。若从 ADC DMA 复制来 16 位宽度，串口会按错误宽度取数据，输出字符可能错位或夹杂异常。

### 7.5 DMA 完成中断

`NVIC_EnableIRQ(DMA1_Channel4_IRQn)` 放行 DMA 通道 4 中断。DMA 搬完后，CPU 进入 `DMA1_Channel4_IRQHandler()`。

### 7.6 启动 DMA 发送前检查 busy

`usart1_dma_send()` 如果正在忙或长度为 0，就直接返回。这样避免同一通道被重复启动。

### 7.7 等待 USART TC

启动新 DMA 前等待 `USART_SR_TC`。这确保上一次物理发送完成，减少串口波形衔接问题。

### 7.8 设置 `CMAR` 和 `CNDTR`

关闭 DMA 通道后，写源地址和长度。DMA 通道使能时改这些寄存器不可靠，所以必须先关。

`CMAR` 写的是本轮消息地址，`CNDTR` 写的是本轮消息长度。这个设计让同一个 DMA 通道可以发送不同字符串，只要每次启动前重新装载地址和数量。

必须先关通道再写，是因为 `EN=1` 后 DMA 硬件正在使用这些寄存器。边运行边改地址和长度，相当于搬运中途换工单，硬件行为不稳定。寄存器版启动函数按“检查 busy -> 等 TC -> 关通道 -> 写地址长度 -> 清标志 -> 开 DMAT -> 开通道”的顺序，是需要记住的工程顺序。

### 7.9 清 DMA 标志

写 `DMA1->IFCR` 清 Channel4 的全局、完成、半传输和错误标志，避免旧标志影响新一轮。

### 7.10 打开 `DMAT` 再使能 DMA

`USART1->CR3 |= USART_CR3_DMAT` 打开 USART 发送 DMA 请求；随后 `CCR.EN=1` 让 DMA 开始响应。

这两步共同启动 USART 请求驱动的 DMA 发送。如果只使能 DMA 通道，没有 `DMAT`，DMA 不会被 USART TX 请求节奏驱动；如果只开 `DMAT`，DMA 通道没开，也没有搬运动作。

顺序上本课先打开 USART 侧请求，再开 DMA 通道。关键不是这两句谁先一条指令，而是地址、长度、标志都必须已经准备好。启动后，USART 发送器一旦需要数据，DMA 就会把 `CMAR` 指向的第一个字节写入 `DR`。

### 7.11 DMA ISR 收尾

完成中断里清标志、关闭 DMA 通道、关闭 `DMAT`、清 busy、置 done。这些动作让下一轮可以干净启动。

清 DMA 标志是为了避免旧完成事件影响下一轮；关闭 DMA 通道是为了允许下次重新写 `CMAR/CNDTR`；关闭 `DMAT` 是为了让 USART 停止继续发 DMA 请求；清 busy 和置 done 则是把硬件完成事件交给主循环。

注意 LED 翻转没有放在 ISR 里，而是 ISR 只置 `g_uart_dma_done`。这和上一课中断接收一样：中断里做收尾和通知，主循环做现象层动作。即使翻转 LED 很快，也保持这种结构，有助于后续扩展更复杂业务。

### 7.12 主循环

主循环每秒调用一次 `usart1_dma_send()`，若 `g_uart_dma_done` 置位就翻转 LED。欢迎信息仍用轮询发送，突出 DMA 发送主流程。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 时钟与 GPIO

HAL 版同样配置 72MHz 时钟、PC13、PA9/PA10。PA9 是复用推挽输出。

### 8.2 DMA TX 句柄

`hdma_usart1_tx.Instance = DMA1_Channel4` 选择 USART1_TX 对应通道。

### 8.3 DMA 方向和宽度

`Direction = DMA_MEMORY_TO_PERIPH` 对应 `DIR=1`。`MemInc = ENABLE` 对应 `MINC=1`。两端数据宽度都是 byte，对应 USART 字节发送。

### 8.4 DMA 普通模式

`Mode = DMA_NORMAL` 表示发完一段后停止。USART 周期发送每次重新启动，不用循环模式。

### 8.5 `__HAL_LINKDMA`

把 `huart1.hdmatx` 指向 `hdma_usart1_tx`。这是 `HAL_UART_Transmit_DMA()` 能找到 DMA1_Channel4 的前提。

HAL 的 UART 句柄和 DMA 句柄是两个对象。`huart1` 知道自己是 USART1，`hdma_usart1_tx` 知道自己是 DMA1_Channel4；但在调用 `__HAL_LINKDMA()` 前，二者没有发送 DMA 关系。这个宏把 `huart1.hdmatx` 指向 DMA 句柄，并让 DMA 句柄反向知道父对象。

如果漏掉这一步，`HAL_UART_Transmit_DMA()` 可能返回错误或状态异常，因为它找不到应该启动哪个 DMA 通道。寄存器版没有这个概念，是因为你手写了 `DMA1_Channel4`；HAL 版需要句柄关系来替代硬编码路径。

### 8.6 DMA 和 USART 中断

HAL 版同时使能 `DMA1_Channel4_IRQn` 和 `USART1_IRQn`。DMA 搬完后 HAL 还要配合 USART 的发送完成状态做收尾，所以两个 IRQHandler 都调用 HAL。

这点很容易被忽略。直觉上“我用 DMA 发送，只开 DMA 中断不就行了吗？”但 HAL 的 UART DMA 发送完成流程不只是 DMA 搬完，还要确认 UART 发送状态并维护 HAL 内部状态。源码里 `DMA1_Channel4_IRQHandler()` 调 `HAL_DMA_IRQHandler()`，`USART1_IRQHandler()` 调 `HAL_UART_IRQHandler()`，两边共同完成收尾。

如果 DMA IRQHandler 没有调用 HAL，DMA 完成回调不会正确进入；如果 USART IRQHandler 漏掉，HAL UART 状态可能一直忙，`HAL_UART_TxCpltCallback()` 不触发或后续发送卡住。看到 HAL 版一直 busy，要优先查这两个中断入口。

### 8.7 UART 初始化

`Mode = UART_MODE_TX`，只使能发送。`HAL_UART_Init()` 配置 BRR、帧格式、TE/UE。

### 8.8 `HAL_UART_Transmit_DMA`

主循环在不忙时调用它发送 `g_dma_message`。它返回后不代表发送完成，只代表启动成功。完成通过回调通知。

参数包括 UART 句柄、发送缓冲区地址和长度。HAL 内部会通过 `huart1.hdmatx` 找到 DMA 句柄，设置 DMA 源地址为缓冲区，目标地址为 `USART1->DR`，长度为字节数，然后打开 USART 的 `DMAT` 并启动 DMA。

因此，调用返回 `HAL_OK` 只是说明这条异步发送流程已经启动。此时不能立刻修改发送缓冲区，也不能认为串口助手已经收到完整字符串。真正完成要等 `HAL_UART_TxCpltCallback()`，或者查询 HAL 状态。

### 8.9 `HAL_UART_TxCpltCallback`

回调中清 busy、置 done。主循环看到 done 后翻转 LED 并延时 1 秒。

这个回调表示 HAL 认为一次 UART DMA 发送流程已经完成。它不是你主动调用的函数，而是 HAL 中断处理链路在合适时机调用的用户钩子。参数 `huart` 用来区分哪个 UART 完成，本课判断 `huart->Instance == USART1`。

回调里不直接 `HAL_Delay(1000)`，也不做复杂打印，只更新状态。这样能保持中断上下文短小。主循环看到 `g_uart_dma_done` 后再翻转 LED 和延时，符合“中断通知、主循环处理”的工程模式。

### 8.10 HAL busy/done 与句柄状态

本课自己维护 `g_uart_dma_busy`，同时 HAL 内部也维护 UART 句柄状态。两者关注层级不同：HAL 状态保护 API 自身不被重入；用户 busy 标志让主循环逻辑清楚知道“本轮应用发送是否完成”。

如果只依赖 HAL 状态，代码会和 HAL 内部枚举绑定更紧；如果只用用户 busy 而不管 HAL 返回值，又可能在 HAL 启动失败时误以为发送中。本课启动失败会清 busy 并进入 `error_handler()`，就是为了让两个状态保持一致。

### 8.11 HAL_Delay 与 SysTick

HAL 版主循环完成一次 DMA 发送后调用 `HAL_Delay(DMA_TX_PERIOD_MS)`。这个延时依赖 `HAL_Init()` 配好的 SysTick，以及源码里的 `SysTick_Handler()` 调用 `HAL_IncTick()`。

如果 HAL_Delay 不工作，DMA 发送周期就不对，甚至主循环可能卡住。排查时要确认没有重复定义错误的 SysTick_Handler，也要确认系统时钟配置后 HAL Tick 仍然正常推进。

## 9. 两个版本真正应该怎么学

寄存器版抓住 `CPAR/CMAR/CNDTR/CCR/DMAT/TCIF4`；HAL 版抓住 `hdmatx` 关联、`Transmit_DMA` 启动、IRQHandler 分发、TxCpltCallback 完成。两者都在做同一件事：把一整段内存数据交给 DMA 送到 USART1。

## 10. 检验问题清单

### 10.1 为什么 USART1_TX 是 DMA1_Channel4

**答**：这是 STM32F103 固定 DMA 请求映射。USART1_RX 则是 DMA1_Channel5。

### 10.2 为什么要打开 `USART_CR3_DMAT`

**答**：它允许 USART 发送器向 DMA 发请求。不开它，DMA 不会按 TX 节奏写 DR。

### 10.3 USART TX DMA 为什么 `PINC=0`

**答**：因为外设地址固定为 `USART1->DR`。

### 10.4 USART TX DMA 为什么 `MINC=1`

**答**：因为字符串在内存中是连续字节，DMA 必须逐字节向后读取。

### 10.5 `TXE`、`TC`、`TCIF4` 有什么区别

**答**：`TXE` 是 USART 数据寄存器可写；`TC` 是 USART 物理发送完成；`TCIF4` 是 DMA Channel4 搬运完成。

### 10.6 为什么要有 busy 标志

**答**：防止上一次 DMA 未完成时重复启动同一通道。

### 10.7 HAL 版为什么要 `__HAL_LINKDMA`

**答**：因为 HAL UART 发送函数通过 `huart1.hdmatx` 找 TX DMA 句柄。

### 10.8 `HAL_UART_Transmit_DMA()` 返回 `HAL_OK` 是否表示发送完成

**答**：不是。它只表示启动成功。发送完成要看回调或状态标志。

## 11. 工程实现步骤

### 11.1 需求分析

需求是每秒通过 DMA 发送一段 USART1 字符串，并在发送完成后翻转 LED。

这个需求包含两个异步事件：定时触发发送、DMA/USART 完成后通知。定时由 SysTick 或 HAL_Delay 提供，发送由 DMA 完成，LED 翻转不能放在启动函数后立刻做，否则只能表示“启动了”，不能表示“发完了”。

### 11.2 硬件核查

PA9 接 USB-TTL RX，GND 共地，串口助手 115200 8N1。PC13 用于完成指示。

### 11.3 寄存器路线

进入 `23_uart_dma/reg`，重点读 `dma1_channel4_init()`、`usart1_dma_send()`、`DMA1_Channel4_IRQHandler()`。

```sh
pio run
pio run -t upload
```

### 11.4 HAL 路线

进入 `23_uart_dma/hal`，重点读 `dma1_channel4_init()`、`HAL_UART_Transmit_DMA()` 调用、两个 IRQHandler、`HAL_UART_TxCpltCallback()`。

### 11.5 工程思维

DMA 发送是异步操作。启动函数返回后，缓冲区不能随意修改或释放，直到完成回调或完成标志出现。

本课使用固定全局字符串，所以不会踩到生命周期问题。但如果你以后把一个局部数组传给 DMA，函数返回后栈空间可能被别的函数使用，DMA 还在读那块内存，串口就会发出被改写的数据。异步外设最重要的工程意识就是：启动和完成之间，相关资源必须保持有效。

### 11.6 常见工程陷阱

通道选错、忘记 DMAT、忘记 MINC、重复启动 DMA、没有清 DMA 标志、HAL 未关联 hdmatx、未开 DMA/USART 中断，都会导致无输出或只发送一次。

还有缓冲区生命周期问题：DMA 发送开始后，源缓冲区必须保持有效且内容稳定，直到完成回调。全局字符串安全，局部数组和临时格式化缓冲则要特别小心。

另一个陷阱是把 DMA 完成等同于串口物理发送完成。DMA 写完 `DR` 后，USART 还可能在移位发送最后一个字节。HAL 帮你处理了一部分收尾；寄存器版则要理解 `TCIF4` 和 `USART_SR_TC` 的区别。

## 12. 运行现象

串口助手先看到欢迎信息，然后每秒收到一行 DMA 发送字符串。每次 DMA 发送完成后 PC13 翻转一次。

## 13. 常见问题排查

### 13.1 串口完全无输出

检查 PA9 接线、GND、USART1 时钟、PA9 复用推挽、BRR、TE/UE、串口助手参数。

### 13.2 欢迎信息有，DMA 信息没有

说明 USART 基础发送正常，重点查 DMA1_Channel4、`DMAT`、`CMAR/CNDTR`、`CCR.EN` 和 DMA 中断。

欢迎信息用轮询发送，它证明 PA9、BRR、TE/UE 和串口助手基本没问题。此时不要反复改波特率，应转向 DMA 链路：Channel4 是否选对、`USART_CR3_DMAT` 是否打开、`CNDTR` 是否非 0、`CMAR` 是否指向消息、`TCIF4` 是否置位。

### 13.3 字符重复或只有第一个字符

检查 `MINC` 是否启用。不开内存自增会重复读同一地址。

### 13.4 只能发送一次

检查完成中断是否清标志、关闭通道、清 busy。HAL 版检查回调是否执行。

寄存器版如果完成后没清 `g_uart_dma_busy`，下一次 `usart1_dma_send()` 会直接返回；如果没关 DMA 通道，下一轮写 `CMAR/CNDTR` 可能不可靠；如果没清 DMA 标志，下一轮中断判断会混乱。HAL 版则重点看 `HAL_UART_TxCpltCallback()` 是否被调用。

### 13.5 HAL 版一直 busy

检查 `DMA1_Channel4_IRQHandler()` 和 `USART1_IRQHandler()` 是否都调用 HAL 对应 IRQHandler，`HAL_UART_TxCpltCallback()` 是否触发。

## 14. 本课最核心的结论

1. USART1_TX 的 DMA 请求固定映射到 DMA1_Channel4。
2. USART 发送 DMA 必须同时配置 DMA 通道和 USART `DMAT`。
3. `CPAR` 指向 `USART1->DR`，`CMAR` 指向字符串，`CNDTR` 是字节长度。
4. 发送字符串必须 `MINC=1`，外设地址必须 `PINC=0`。
5. DMA 完成不等于启动函数返回，必须通过完成标志或回调判断。
6. busy/done 状态是异步 DMA 发送的基本工程保护。
7. HAL UART DMA 的关键是 `hdmatx` 句柄关联和 IRQHandler/Callback 收尾。

## 15. 建议你现在怎么读这节课

先画 `g_dma_message -> DMA1_Channel4 -> USART1->DR -> PA9`。再读启动函数，标出地址、长度、清标志、DMAT、EN 的顺序。最后看 HAL 版如何用句柄把这些动作串起来。

## 16. 扩展练习

- 把 DMA 发送周期改成 200ms，观察 busy 保护是否仍然有效。
- 去掉 `MINC`，观察输出内容。
- 注释 `USART_CR3_DMAT`，观察欢迎信息和 DMA 信息差异。
- HAL 版注释 `USART1_IRQHandler()`，观察完成回调是否异常。
- 增加一个更长的发送字符串。

## 17. 下一课预告

上一课：[22_uart_interrupt](../22_uart_interrupt/README.md)

下一课：[24_uart_printf_redirect](../24_uart_printf_redirect/README.md)
