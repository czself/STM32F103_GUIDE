# 22_uart_interrupt - USART1 中断接收

## 1. 本课到底在学什么

本课表面现象是：串口助手发送 `1`、`0`、`t` 后，开发板回显字符并控制 PC13 LED。

真正要学的是 USART 接收中断链路：

```text
电脑发送字符
  -> USB-TTL TX
  -> PA10 / USART1_RX
  -> USART1 接收完成
  -> DR 中有新字节，SR.RXNE 置位
  -> CR1.RXNEIE 允许 USART 发中断请求
  -> NVIC 放行 USART1_IRQn
  -> CPU 进入 USART1_IRQHandler
  -> 读 DR，保存字节，置 g_rx_ready
  -> 主循环处理命令并控制 LED
```

上一课轮询接收是 CPU 一直问“有没有数据”。本课中断接收是 USART 收到数据后主动通知 CPU。你要重点理解两层开关：外设侧 `RXNEIE` 负责“USART 愿不愿意发中断请求”，NVIC 侧 `USART1_IRQn` 负责“CPU 愿不愿意响应这个请求”。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 解释 `RXNE` 和 `RXNEIE` 的区别。
- 说明为什么只开 `RXNEIE` 不开 NVIC，中断仍然不会进。
- 说明为什么只开 NVIC 不开 `RXNEIE`，中断也不会进。
- 看懂 `USART1_IRQHandler()` 为什么先判断 `SR.RXNE` 再读 `DR`。
- 理解 `g_rx_byte`、`g_rx_ready` 为什么要加 `volatile`。
- 解释 HAL 版 `HAL_UART_Receive_IT()`、`HAL_UART_IRQHandler()`、`HAL_UART_RxCpltCallback()` 的分工。
- 说明 HAL 单字节中断接收为什么要在回调里再次启动。

## 3. 本课目录结构

```text
22_uart_interrupt/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

寄存器版直接配置 `CR1.RXNEIE` 和 `NVIC_EnableIRQ()`；HAL 版用 `HAL_UART_Receive_IT()` 启动接收，用回调把数据交给主循环。

## 4. 实验硬件

- STM32F103C8T6 BluePill，板卡 `genericSTM32F103C8`。
- PA9 接 USB-TTL RX，PA10 接 USB-TTL TX，GND 共地。
- 串口助手设置 115200、8N1。
- PC13 为板载 LED，通常低电平点亮。

## 5. 先建立完整脑图

本课按六层理解：

1. 现象层：电脑发送命令，串口回显，LED 改变。
2. 物理层：PA10 接收 USB-TTL 发来的串口电平，PA9 输出回显信息。
3. 芯片模块层：USART1 负责收发，NVIC 负责中断分发，GPIOC 控制 LED。
4. 寄存器层：`SR.RXNE` 表示收到数据，`CR1.RXNEIE` 允许中断，`DR` 保存字节，NVIC 使能 `USART1_IRQn`。
5. C/CMSIS 层：`USART1_IRQHandler()` 是中断向量入口，`volatile` 变量完成 ISR 与主循环交接。
6. HAL 工程层：`HAL_UART_Receive_IT()` 建立接收任务，`HAL_UART_IRQHandler()` 分发中断，`HAL_UART_RxCpltCallback()` 做用户处理。

## 6. 核心名词解释

### 6.1 `USART1_IRQHandler` 是什么

`USART1_IRQHandler()` 是 USART1 的中断服务函数，属于 C/CMSIS 层。

函数名必须和启动文件里的中断向量表一致。CPU 响应 USART1 中断后，会跳到这个函数。写错名字时，代码能编译，但中断不会进到你写的函数。

它不是普通由 `main()` 调用的函数，而是由 Cortex-M3 异常入口硬件调用的函数。USART1 收到字节后，外设先置 `RXNE`，再在 `RXNEIE=1` 时向 NVIC 发请求；NVIC 放行后，CPU 暂停当前主循环，自动保存现场，然后跳到向量表里 `USART1_IRQn` 对应的入口，也就是这个函数。

代码位置在 `reg/src/main.c`。本课 ISR 里只判断 `SR.RXNE`、读 `DR`、写 `g_rx_byte`、置 `g_rx_ready`。如果把打印字符串、复杂协议解析都塞进这里，表面上能跑，但中断占用时间会变长，后续定时器、串口或其他实时事件可能被拖住。

### 6.2 `RXNE` 是什么

`RXNE` 是接收数据寄存器非空标志，位于 USART `SR`。

它表示 `DR` 中已有新字节等待读取。读 `DR` 会取出字节并清除 RXNE。它本身只是状态，不等于中断使能。

`RXNE` 属于 USART 外设寄存器层，含义和上一课轮询接收完全一样。区别只是上一课由主循环主动检查它，本课让它成为中断触发源之一。硬件收到完整 8N1 帧后，才会把数据放入 `DR` 并置位 `RXNE`，不是 PA10 电平一变化就进中断。

在 `USART1_IRQHandler()` 里先判断 `USART1->SR & USART_SR_RXNE`，是因为 USART1 中断线可能对应多个来源，例如发送完成、发送寄存器空、空闲线、错误等。本课只处理接收数据，所以要先确认 RXNE。若不判断来源就读 `DR`，以后打开其他 USART 中断源时会误处理。

### 6.3 `RXNEIE` 是什么

`RXNEIE` 是 RXNE Interrupt Enable，位于 USART `CR1`。

`RXNEIE=1` 后，当 `RXNE=1` 时 USART 会向 NVIC 发中断请求。它属于外设侧开关。不开它，RXNE 只会作为轮询标志存在。

它控制的是“状态能不能变成中断请求”。`RXNE` 表示事实：数据到了；`RXNEIE` 表示许可：这个事实是否要通知 CPU。寄存器版在 `usart1_init()` 中把 `USART_CR1_RXNEIE` 和 `TE/RE` 一起写入 `CR1`。

它和 NVIC 是两道门。只开 `RXNEIE` 不开 NVIC，USART 会举手但 CPU 不响应；只开 NVIC 不开 `RXNEIE`，CPU 愿意响应但 USART 根本不举手。完全进不了中断时，必须同时检查这两个层级。

### 6.4 `USART1_IRQn` 是什么

`USART1_IRQn` 是 USART1 在 NVIC 中的中断号。

它属于 Cortex-M3/NVIC 层。`NVIC_EnableIRQ(USART1_IRQn)` 允许 CPU 响应 USART1 中断请求。不开 NVIC，总闸关着，外设请求到不了 CPU。

`USART1_IRQn` 来自 CMSIS 头文件中的枚举，不是随便写的数字。它把 STM32 外设事件映射到 Cortex-M3 的中断控制器入口。寄存器版用 `NVIC_SetPriority(USART1_IRQn, 1U)` 和 `NVIC_EnableIRQ(USART1_IRQn)`，HAL 版用 `HAL_NVIC_SetPriority()` 和 `HAL_NVIC_EnableIRQ()`。

它负责的是 CPU 侧，不负责 USART 内部标志。换句话说，NVIC 不知道 `RXNE` 是什么，它只知道“USART1 这条中断线来了请求”。具体是 RXNE 还是别的 USART 事件，仍要在 ISR 或 HAL 分发函数里读取 USART 状态寄存器判断。

### 6.5 `NVIC_SetPriority` 是什么

`NVIC_SetPriority()` 设置中断优先级。

本课设置 USART1 优先级为 1。优先级决定多个中断同时发生时谁先处理。虽然本课中断少，但从这里开始要养成给中断明确优先级的习惯。

### 6.6 `DR` 在中断接收里是什么

`DR` 是 USART 数据寄存器。

ISR 读 `USART1->DR`，既取出收到的字节，也让硬件清除 RXNE。若不读 DR，中断可能反复触发，因为 RXNE 一直保持。

这个读操作是 ISR 的核心硬件动作。`RXNE` 告诉你“有数据”，但真正的数据在 `DR`。读 `DR` 后，硬件认为 CPU 已经取走数据，于是清除 `RXNE`，USART 接收器可以继续准备下一字节。

本课读出来后立刻放入 `g_rx_byte`，而不是在 ISR 里直接判断 `1/0/t`。这样 `DR` 这个硬件寄存器只和“取数据”相关，业务逻辑交给主循环。若连续输入很快，而主循环没来得及处理，新的 ISR 会覆盖 `g_rx_byte`，这正是本课后面引出环形缓冲区的原因。

### 6.7 `g_rx_byte` 是什么

`g_rx_byte` 是中断和主循环之间的数据交接变量。

ISR 把收到的字节写入它，主循环读取它。它被中断异步修改，所以需要 `volatile`。

它属于 C 软件状态层，不是硬件寄存器。硬件只把字节放到 `DR`；ISR 读 `DR` 后，才把数据复制到 `g_rx_byte`。主循环不直接读 `USART1->DR`，而是读这个交接变量，这样业务逻辑不用关心硬件标志何时清除。

`volatile` 的意义是防止编译器把主循环里的读取优化成一次缓存。因为 `g_rx_byte` 的写入发生在 ISR 中，从主循环代码表面看不到。没有 `volatile` 时，优化器可能认为它不会突然变化，造成主循环读不到最新字节。

### 6.8 `g_rx_ready` 是什么

`g_rx_ready` 是“有新数据”的状态标志。

ISR 置 1，主循环看到后取走数据并清 0。它把“数据内容”和“是否有新数据”分开，让逻辑更清楚。

它也是 ISR 与主循环之间的协议。`g_rx_byte` 存内容，`g_rx_ready` 存状态。主循环只有看到 `g_rx_ready != 0` 才把 `g_rx_byte` 复制到局部变量 `ch`，随后清零，表示这条数据已经处理。

如果没有这个标志，主循环无法区分 `g_rx_byte` 里的值是新收到的，还是上一次残留的。尤其当收到字符本身可能是 `0` 时，不能用数据值本身判断“有没有新数据”。工程上更复杂时，这个单 bit 标志会升级成读写索引、计数器或消息队列。

### 6.9 `快进快出` 是什么

快进快出是中断处理原则。

ISR 中只做必要动作：读 DR、保存数据、置标志。字符串打印、命令解析、LED 控制放在主循环，避免长时间占用中断上下文。

它属于工程设计原则，不是某个寄存器位。中断会打断主循环，也可能打断其他低优先级中断；如果在中断里停留太久，系统响应会变差。串口接收尤其容易诱惑你在 ISR 里直接打印、解析、控制外设，但这会把一个本来很短的硬件事件处理变成一段不可控业务逻辑。

本课用 `g_rx_ready` 把 ISR 和主循环隔开：ISR 只负责“数据到了”，主循环负责“数据代表什么”。这个结构和后续 DMA 完成中断、定时器中断都相通。你学到的不是 USART 专属技巧，而是中断驱动程序的基本分层。

### 6.10 `HAL_UART_Receive_IT` 是什么

`HAL_UART_Receive_IT()` 是 HAL 的中断接收启动函数。

它配置 HAL 句柄里的接收缓冲区、长度和状态，并使能 RXNEIE。它启动的是“一次接收任务”，不是永久自动接收。

本课调用 `HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1U)`。三个核心信息分别是：使用 USART1 句柄，把收到的数据写到 `g_rx_irq_byte`，本次只接收 1 个字节。调用成功后函数立即返回，主循环继续运行；真正收到字节时，后续由中断入口和 HAL 分发函数处理。

它属于 HAL 工程层，但底层仍然会操作 USART 接收中断相关位。只执行 `HAL_UART_Init()` 不会自动接收，因为初始化只配置波特率和收发模式；`HAL_UART_Receive_IT()` 才把“我要接收 N 个字节”这个任务交给 HAL 状态机。

### 6.11 `HAL_UART_IRQHandler` 是什么

`HAL_UART_IRQHandler()` 是 HAL 的中断分发函数。

HAL 版 `USART1_IRQHandler()` 里必须调用它，让 HAL 检查 RXNE、读 DR、维护计数和状态，并在接收完成后调用用户回调。

HAL 版中断入口通常很短：`void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }`。这不是偷懒，而是让 HAL 接管状态机。HAL 需要知道当前是否有接收任务、还差几个字节、缓冲区指针指向哪里、是否要关闭 `RXNEIE`、是否调用完成回调。

如果你绕过 `HAL_UART_IRQHandler()`，直接在 `USART1_IRQHandler()` 里读 `DR`，HAL 的内部计数和状态不会更新，`HAL_UART_RxCpltCallback()` 也不会按预期调用。HAL 版要么按 HAL 链路走完整，要么就写成纯寄存器版，不要混着抢同一个中断。

### 6.12 `HAL_UART_RxCpltCallback` 是什么

`HAL_UART_RxCpltCallback()` 是 HAL 接收完成回调。

本课接收长度是 1，所以每收到 1 个字节就进入回调。回调中把字节交给主循环，并再次调用 `HAL_UART_Receive_IT()` 启动下一轮。

它不是你在 `main()` 里主动调用的普通函数，而是 `HAL_UART_IRQHandler()` 在确认接收任务完成后调用的用户钩子。参数 `huart` 告诉你是哪一个 UART 触发回调，所以本课先判断 `huart->Instance == USART1`，避免以后多个串口共用回调时误处理。

回调里仍然遵守快进快出：复制 `g_rx_irq_byte`、置 `g_rx_ready`、重启接收。回显和命令解析留给主循环。如果在回调里大量 `printf` 或长时间阻塞，就和在 ISR 里做重活一样，会伤害系统实时性。

### 6.13 `g_rx_irq_byte` 是什么

`g_rx_irq_byte` 是 HAL 中断接收的目标缓冲字节。

HAL_UART_Receive_IT 把它的地址交给 HAL，HAL 在中断里把收到的字节写到这里。回调再复制到 `g_rx_byte` 给主循环处理。

### 6.14 `单字节中断接收` 是什么

单字节中断接收是每次只启动接收 1 个字节。

优点是命令响应简单；缺点是每次完成后必须重启。如果忘记重启，通常只能收到第一个字节。

寄存器版本质上也是单字节处理：每次 RXNE 来一个字节，ISR 读一个字节。但寄存器版保持 `RXNEIE=1`，所以读完后下一字节仍会继续触发。HAL 版把接收描述成“任务”，`HAL_UART_Receive_IT(..., 1)` 的任务长度就是 1，完成后任务结束，因此要在回调里重新提交下一次任务。

它适合本课这种单字符命令：`1`、`0`、`t`。如果要接收一条以换行结尾的字符串，单字节中断仍可用，但应把字节放进环形缓冲或命令缓冲，而不是只有一个 `g_rx_byte`。

### 6.15 `回显仍用轮询发送` 是什么

本课只把接收改成中断，发送字符串仍用轮询。

这是为了控制知识宽度。发送中断/DMA 会在后续课程再讲，本课重点是 RXNE 接收中断。

## 7. 寄存器版代码逐步讲解

### 7.1 时钟和 GPIO

系统时钟仍为 72MHz。PA9 配复用推挽输出，PA10 配浮空输入，PC13 配推挽输出。

### 7.2 USART 基础配置

`USART1->BRR = 0x0271U` 配置 115200 波特率。`CR1` 设置 `TE | RE | RXNEIE`，最后设置 `UE` 使能 USART。

这句 `BRR` 仍然依赖 PCLK2=72MHz，和上一课完全一致。中断接收改变的是“收到数据后 CPU 如何得知”，不改变串口电气格式。若这里波特率错，可能连 `RXNE` 都会以错误数据置位，最终回显乱码。

`CR1` 里新增的只有 `RXNEIE`。`TE` 仍用于回显和提示字符串发送，`RE` 仍用于接收 PA10 输入，`UE` 仍是 USART 总使能。读代码时要看到：中断版不是把轮询版推倒重来，而是在原有 USART 收发能力上增加接收通知机制。

### 7.3 外设侧中断使能

`RXNEIE=1` 是外设侧允许接收中断。收到字节后，硬件置 RXNE 并产生中断请求。

### 7.4 NVIC 侧中断使能

`NVIC_SetPriority(USART1_IRQn, 1U)` 设置优先级，`NVIC_EnableIRQ(USART1_IRQn)` 打开 CPU 总闸。

`NVIC_SetPriority` 写的是 Cortex-M3 内核侧的优先级寄存器。优先级数值越小通常优先级越高，本课使用 1，只是给 USART1 一个明确优先级。后续外设多起来后，定时器、串口、DMA、EXTI 之间的先后关系会变得重要。

`NVIC_EnableIRQ` 执行后，USART1 的中断请求才允许打断主循环。注意它不会主动触发一次中断，也不会清外设标志。它只是打开通道；真正进入 ISR 仍要等 USART1 产生请求。

### 7.5 ISR 判断中断源

`USART1_IRQHandler()` 先判断 `USART1->SR & USART_SR_RXNE`。USART 可能有多个中断源，先判断标志可以避免误处理。

这一步的硬件后果是读取 USART 状态寄存器，确认本次进入中断是不是因为接收数据。虽然本课只开启 `RXNEIE`，保持判断仍是好习惯，因为以后可能打开错误中断、空闲中断或发送中断。

如果判断为假，函数直接退出，不改软件变量。这样可以避免无效中断把 `g_rx_ready` 置位，导致主循环拿旧数据当新命令。

### 7.6 ISR 读 DR

读 `USART1->DR` 得到字节，并清 RXNE。随后写 `g_rx_byte`，置 `g_rx_ready=1`。

### 7.7 主循环取走数据

主循环看到 `g_rx_ready` 后，把共享变量复制到局部变量 `ch`，再清标志。这能减少后续处理时被新中断改写的影响。

这段顺序也有讲究：先复制 `g_rx_byte`，再清 `g_rx_ready`，然后用局部变量做回显和命令解析。局部变量 `ch` 不会被 ISR 修改，所以后续业务处理更稳定。

本课没有临界区保护，原因是只交接 1 个字节，教学上先保持简单。但严格工程里，如果担心复制和清标志之间又来新中断，就需要关中断短保护、环形缓冲区或其他无锁协议。这里要知道它的局限，而不是误以为单字节标志能解决所有串口接收。

### 7.8 主循环做业务

回显、命令解析、LED 控制都在主循环中完成。这体现中断快进快出的原则。

### 7.9 数据覆盖风险

本课只有单字节交接。如果主循环处理慢、串口连续高速输入，新字节可能覆盖旧字节。工程中应使用环形缓冲区。

覆盖发生的过程很简单：第一个字节进中断，`g_rx_byte='A'`、`g_rx_ready=1`；主循环还没处理，第二个字节又进中断，`g_rx_byte` 被改成 `'B'`。此时主循环只看见 `'B'`，`'A'` 丢了。

环形缓冲区的思路是让 ISR 每次把字节写到下一个槽位，主循环按读指针逐个取走。这样 ISR 和主循环不再抢同一个单字节变量，而是通过读写索引协作。本课暂时不实现，是为了先把 RXNE 中断链路讲清楚。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化

`HAL_Init()` 准备 HAL Tick。时钟和 GPIO 初始化与轮询版类似。

### 8.2 UART 基础配置

`HAL_UART_Init()` 配置 115200、8N1、TX_RX。注意它不自动开始接收中断。

### 8.3 NVIC 配置

HAL 版在 `usart1_init()` 中使能 `USART1_IRQn`。RXNEIE 则由 `HAL_UART_Receive_IT()` 启动接收时打开。

### 8.4 启动第一轮接收

main 中调用 `HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1U)`。没有这一句，NVIC 和 USART 初始化都完成了，也不会进入接收回调。

这一步在 HAL 版里等价于“告诉 HAL：接下来帮我收 1 个字节”。它会设置 `huart1` 内部的接收缓冲指针、剩余长度和接收状态，并打开接收中断。调用之后，CPU 不会停在这里等字符，而是继续打印欢迎信息、进入主循环。

若这句返回不是 `HAL_OK`，说明 HAL 当前状态不允许启动接收，或参数有问题。本课直接进 `error_handler()`，真实工程里可以读取 `huart1.ErrorCode` 或状态进一步分析。

### 8.5 中断入口

`USART1_IRQHandler()` 调用 `HAL_UART_IRQHandler(&huart1)`。用户业务不要直接写在这里，否则会绕开 HAL 状态机。

### 8.6 接收完成回调

`HAL_UART_RxCpltCallback()` 判断实例是 USART1，然后把 `g_rx_irq_byte` 复制给 `g_rx_byte`，置 `g_rx_ready`。

这里的 `g_rx_irq_byte` 是 HAL 写入的缓冲区，`g_rx_byte` 是主循环读取的交接区。虽然本课只有一个字节，看起来可以省掉一次复制，但分开后层次更清楚：HAL 的接收目标归 HAL 回调管理，业务层变量归主循环管理。

判断 `huart->Instance == USART1` 是 HAL 回调的标准习惯。HAL 的弱回调函数对所有 UART 共用，如果以后加入 USART2 或 USART3，不判断实例就可能把别的串口数据也当作 USART1 命令处理。

### 8.7 回调里重启接收

回调最后再次调用 `HAL_UART_Receive_IT()`。这是 HAL 单字节中断接收最关键的一步。

HAL 的接收任务是有长度的。本课长度是 1，所以收到 1 个字节后任务完成，HAL 会把接收状态恢复为就绪，并可能关闭本轮接收相关中断。想继续接收下一个字节，就必须再启动一次。

“只能收到第一个字节”是 HAL 单字节中断接收最典型的问题，几乎都和忘记重启有关。寄存器版没有这个 API 重启动作，因为它一直保持 `RXNEIE=1`；HAL 版多了状态机，所以要按 HAL 任务模型来理解。

### 8.8 HAL 发送仍是轮询

`uart1_send_string()` 用 `HAL_UART_Transmit()`。本课发送不走中断，避免把重点分散。

这也说明“一个外设可以同时用不同方式处理不同方向”。接收用中断，是因为外部数据到达时间不可控；发送欢迎信息和回显用轮询，是因为发送由 CPU 主动发起，等待时间短且逻辑简单。不要误以为用了中断接收后，发送也必须立刻改成中断。

### 8.9 HAL 状态和错误处理

`HAL_UART_Receive_IT()` 可能返回 `HAL_BUSY`，表示上一次接收任务还没结束；也可能返回 `HAL_ERROR`，表示参数或状态异常。本课失败就进入 `error_handler()`，是为了让错误明显暴露。

HAL 中断接收的核心是状态机。调用启动函数后，句柄内部会记录接收缓冲指针、剩余长度和忙碌状态；中断里每收一个字节就更新这些状态；完成后再回调。若用户绕开 HAL 直接读 `DR`，这些状态就会和硬件真实情况脱节。

排查 HAL 版时，要看三处证据：`HAL_UART_Receive_IT()` 是否返回 `HAL_OK`，`USART1_IRQHandler()` 是否进入并调用 `HAL_UART_IRQHandler()`，`HAL_UART_RxCpltCallback()` 是否触发并重启接收。三处断一处，接收链路都会断。

## 9. 两个版本真正应该怎么学

寄存器版看两层开关和 ISR 交接；HAL 版看“启动接收任务 -> IRQHandler 分发 -> 回调 -> 重启任务”。两者本质一致：硬件收到字节后通知 CPU，CPU 不再主动轮询 RXNE。

## 10. 检验问题清单

### 10.1 `RXNE` 和 `RXNEIE` 有什么区别

**答**：`RXNE` 是收到数据的状态；`RXNEIE` 是允许这个状态触发中断的使能位。

### 10.2 为什么要同时配置 NVIC

**答**：外设只能发请求，NVIC 决定 CPU 是否响应请求。两层缺一不可。

### 10.3 为什么 ISR 要读 DR

**答**：读 DR 取出数据，同时清 RXNE。否则中断可能持续触发。

### 10.4 为什么 ISR 不直接解析命令

**答**：为了快进快出，避免在中断中做耗时业务。

### 10.5 为什么共享变量要 `volatile`

**答**：它们会被 ISR 异步修改，编译器不能假设它们只按主循环代码变化。

### 10.6 HAL 版为什么要先调用 `HAL_UART_Receive_IT`

**答**：它启动接收任务并打开 RXNEIE。只初始化 UART 不会自动接收。

### 10.7 HAL 回调里为什么要重启接收

**答**：本课每次只接收 1 字节，完成后任务结束；不重启就只能收一次。

### 10.8 连续快速输入为什么可能丢字节

**答**：本课只有单字节缓存，主循环处理慢时新数据可能覆盖旧数据，需要环形缓冲区改进。

### 10.9 HAL 版 `HAL_UART_Init()` 会自动打开 RXNEIE 吗

**答**：不会。`HAL_UART_Init()` 只配置波特率、帧格式和收发模式；`RXNEIE` 是 `HAL_UART_Receive_IT()` 启动接收任务时打开的。

### 10.10 为什么发送欢迎信息仍可以用轮询

**答**：发送由 CPU 主动发起，等待时间可预期；本课重点是接收从轮询变中断，所以发送暂时保留轮询能减少干扰。

## 11. 工程实现步骤

### 11.1 需求分析

需求是 USART1 接收命令不再轮询等待，而是在收到字节时进入中断，再由主循环处理命令。

更完整地说，发送端仍然保留轮询发送欢迎信息和回显；接收端从“主循环一直查 RXNE”改成“RXNE 触发中断”。这样本课只改变一个变量：接收通知方式。教学上这样做，是为了让你清楚看到轮询到中断的最小差异。

### 11.2 硬件核查

PA9 接 USB-TTL RX，PA10 接 USB-TTL TX，GND 共地，串口助手 115200 8N1。

### 11.3 寄存器路线

进入 `22_uart_interrupt/reg`，重点读 `usart1_init()`、`USART1_IRQHandler()` 和主循环。

```sh
pio run
pio run -t upload
```

读 `usart1_init()` 时先圈出 `USART_CR1_RXNEIE` 和 `NVIC_EnableIRQ()`；读 ISR 时圈出 `SR.RXNE` 判断和 `DR` 读取；读主循环时圈出 `g_rx_ready` 的判断、取值、清零。三处连起来，才是完整中断接收路线。

### 11.4 HAL 路线

进入 `22_uart_interrupt/hal`，重点读 `HAL_UART_Receive_IT()` 首次启动、`USART1_IRQHandler()`、`HAL_UART_RxCpltCallback()`。

### 11.5 工程思维

中断只做数据搬运和状态通知，业务逻辑尽量放到主循环。复杂串口协议应使用环形缓冲区。

### 11.6 常见工程陷阱

只开 RXNEIE 不开 NVIC、只开 NVIC 不启动接收、HAL 回调里不重启接收、ISR 里不读 DR、共享变量不加 volatile，都会导致只收一次或完全不收。

还有一个陷阱是把中断优先级当成无关紧要。当前 demo 只有少量中断，优先级问题不明显；后续加入 SysTick、DMA、定时器、FreeRTOS 后，错误优先级可能导致中断响应延迟甚至 API 使用受限。现在先养成显式设置优先级的习惯。

另一个陷阱是把 ISR 和主循环共享变量当作普通变量处理。`volatile` 只能保证每次真的读写内存，不保证多个变量更新的原子一致性。复杂协议里不要继续用单字节标志堆逻辑，应改成环形缓冲区或消息队列。

## 12. 运行现象

串口助手显示欢迎信息。发送 `1` 点亮 LED，发送 `0` 熄灭 LED，发送 `t/T` 翻转 LED。每个命令都会被回显。

## 13. 常见问题排查

### 13.1 完全进不了中断

检查 `CR1.RXNEIE`、`NVIC_EnableIRQ(USART1_IRQn)`、函数名 `USART1_IRQHandler`、PA10 接线和 RE/UE。

建议按两层开关排查：先看 `USART1->SR.RXNE` 会不会在电脑发送字符后置位；若 RXNE 不置位，问题在 PA10、波特率、RE 或物理连接；若 RXNE 置位但 ISR 不进，问题在 `RXNEIE`、NVIC 或函数名。这样比盲目改代码更快。

### 13.2 只能收到第一个字节

HAL 版检查回调中是否再次调用 `HAL_UART_Receive_IT()`。寄存器版检查是否读了 DR 清 RXNE。

HAL 版“第一个字节正常，第二个开始没反应”几乎是接收任务没有重启。寄存器版如果没有读 `DR`，则更可能表现为中断反复进或状态异常。两个版本现象相似，但根因分别在 HAL 状态机和底层标志清除。

### 13.3 收到字符但命令不执行

检查 `g_rx_ready` 是否置位后被主循环清除，主循环是否取走 `g_rx_byte`，命令字符是否是 ASCII `1/0/t`。

如果回显能看到 `RX: x`，说明中断接收和主循环交接已经基本通了，问题多半在命令判断或 LED 控制。若 `g_rx_ready` 在调试器里置位但主循环没有进入处理分支，要检查变量是否被优化、是否加了 `volatile`，以及主循环是否被其他阻塞代码卡住。

也要确认串口助手发送的是单个字符，不是附带回车换行的整行命令。如果发送 `1\r\n`，本课会先处理 `1`，后续 `\r` 和 `\n` 会被当作未知命令回显。

### 13.4 串口乱码

优先查系统时钟、BRR、串口助手波特率和 8N1 设置。

### 13.5 连续输入丢字符

本课单字节交接区不能承受高频连续输入，改用环形缓冲区或 DMA 接收。

原因不是 USART 硬件一定收不到，而是软件只有一个 `g_rx_byte` 槽位。新中断来时会覆盖旧值，如果主循环还没取走旧值，旧字符就丢了。工程里常用环形缓冲区保存多个字节，或者用 DMA 空闲中断接收一段数据。

## 14. 本课最核心的结论

1. USART 中断接收的触发源是 `RXNE`。
2. `RXNEIE` 是外设侧开关，NVIC 是 CPU 侧开关。
3. ISR 读 `DR` 才能取数据并清 RXNE。
4. ISR 应快进快出，只交接数据和标志。
5. `volatile` 是 ISR 与主循环共享变量的基本要求。
6. HAL 单字节中断接收必须每次完成后重启。
7. 串口连续数据最终应走环形缓冲区或 DMA。

## 15. 建议你现在怎么读这节课

先把中断链路画出来：`RXNE -> RXNEIE -> NVIC -> USART1_IRQHandler -> DR -> g_rx_ready`。再对照 HAL 版，理解 `Receive_IT`、`IRQHandler`、`Callback` 三段关系。

## 16. 扩展练习

- 故意注释 `NVIC_EnableIRQ()`，观察中断不进。
- HAL 版注释回调中的重启接收，观察只能收一次。
- 增加一个 `?` 命令打印帮助信息。
- 尝试实现 8 字节环形缓冲区。

## 17. 下一课预告

上一课：[21_uart_polling](../21_uart_polling/README.md)

下一课：[23_uart_dma](../23_uart_dma/README.md)
