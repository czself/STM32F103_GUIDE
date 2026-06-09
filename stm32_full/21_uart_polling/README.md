# 21_uart_polling - USART1 轮询收发

## 1. 本课到底在学什么

本课表面现象是：串口助手收到欢迎信息；你从电脑发送 `1`、`0`、`t` 后，开发板回显字符并控制 PC13 LED。

真正要学的是 USART 轮询收发链路：

```text
电脑串口助手
  -> USB-TTL
  -> PA10 / USART1_RX
  -> USART1->DR
  -> RXNE 置位
  -> CPU 轮询 SR.RXNE 并读取 DR
  -> 判断命令并控制 LED

CPU 发送字符串
  -> 等待 TXE
  -> 写 USART1->DR
  -> USART1 按 BRR 产生串行波形
  -> PA9 / USART1_TX
  -> USB-TTL / 电脑串口助手
```

本课的关键词是“轮询”。CPU 主动反复查看状态位：发送时等 `TXE=1`，接收时查 `RXNE=1`。它简单、直观、适合入门，但 CPU 需要不断查看外设状态。下一课会把接收改成中断，让 USART 收到数据后主动通知 CPU。

## 2. 本课学习目标

学完本课，你至少要能做到：

- 说明 PA9、PA10 分别在 USART1 中承担什么角色。
- 解释为什么 PA9 要配置成复用推挽输出，PA10 要配置成输入。
- 解释 `BRR=0x0271` 和 72MHz、115200 波特率之间的关系。
- 说清楚 `TXE`、`RXNE`、`DR` 在收发中的硬件意义。
- 区分阻塞轮询和非阻塞轮询。
- 把 HAL 版 `HAL_UART_Transmit()`、`HAL_UART_Receive(..., 0)` 对应回 `TXE/RXNE` 轮询。
- 根据无输出、乱码、收不到命令、LED 逻辑反等现象排查。

## 3. 本课目录结构

```text
21_uart_polling/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

寄存器版直接读写 USART1 的 `SR/DR/BRR/CR1`；HAL 版用 UART 句柄和阻塞/非阻塞式 HAL API 表达同样行为。

## 4. 实验硬件

本课使用 STM32F103C8T6 BluePill，板卡配置为 `genericSTM32F103C8`，HSE 8MHz，PLL 到 72MHz。

- `PA9`：USART1_TX，接 USB-TTL 的 RX。
- `PA10`：USART1_RX，接 USB-TTL 的 TX。
- `GND`：开发板和 USB-TTL 必须共地。
- `PC13`：板载 LED，低电平通常点亮。
- 串口参数：115200，8 数据位，无校验，1 停止位。

## 5. 先建立完整脑图

本课按六层理解：

1. 现象层：串口助手看到欢迎信息，发送命令后收到回显，LED 状态变化。
2. 物理层：USB-TTL 把电脑 USB 数据转换成 TTL 串口电平；PA9 发，PA10 收。
3. 芯片模块层：GPIOA 提供复用引脚，USART1 完成异步串口收发，GPIOC 控制 LED。
4. 寄存器层：`BRR` 决定波特率，`CR1.TE/RE/UE` 使能收发，`SR.TXE/RXNE` 表示收发状态，`DR` 承载字节。
5. C/CMSIS 层：`usart1_send_byte()` 轮询 TXE，`usart1_receive_byte_nonblocking()` 检查 RXNE。
6. HAL 工程层：`UART_HandleTypeDef.Init` 描述串口格式，`HAL_UART_Transmit()` 和 `HAL_UART_Receive()` 封装轮询。

## 6. 核心名词解释

### 6.1 `USART1` 是什么

`USART1` 是 STM32F103 的通用同步/异步串行外设。本课只使用异步模式，所以也常说 UART。

它负责把 CPU 写入的并行字节转换成 TX 线上的串行波形，也负责把 RX 线上的串行波形还原成字节放进 `DR`。如果 USART1 时钟不开，收发寄存器配置不会生效。

从层级上看，`USART1` 属于 STM32 片上外设层，不是 Cortex-M3 内核的一部分，也不是普通 GPIO。CPU 通过 APB2 总线访问 `USART1->BRR`、`USART1->CR1`、`USART1->SR`、`USART1->DR` 这些寄存器；USART 外设再通过复用功能接管 PA9/PA10 的电平收发。

本课为什么从 GPIO 走到 USART？因为 LED 和按键只是在芯片内部读写引脚电平，串口则让开发板和电脑交换字节。它是后续调试输出、命令控制、printf 重定向、DMA 发送的重要基础。若 USART1 基础链路没建立，后面看到 `printf()` 没输出、DMA 发送没动静时就很难定位。

### 6.2 `PA9 / USART1_TX` 是什么

PA9 是 USART1 默认发送引脚，属于物理引脚和复用功能层。

它必须配置成复用推挽输出，因为引脚电平不再由 GPIO 输出寄存器直接控制，而由 USART1 发送器控制。若配成普通 GPIO，串口助手看不到正确发送波形。

`TX` 是 Transmit，发送方向是“STM32 -> USB-TTL -> 电脑”。所以接线时 PA9 要接 USB-TTL 模块的 RX。很多初学者会把 TX 接 TX，这是把两个发送端接在一起，双方都在说话，没有人接收，串口助手自然没有输出。

代码里 PA9 在 `GPIOA->CRH` 配置，因为 9 号引脚属于 8 到 15 的高半区。`MODE9=11` 选择 50MHz 输出速度，`CNF9=10` 选择复用推挽输出。这里的“复用”表示输出源来自 USART1_TX，而不是普通 `GPIOA->ODR`。

### 6.3 `PA10 / USART1_RX` 是什么

PA10 是 USART1 默认接收引脚。

它配置成输入模式，外部 USB-TTL 的 TX 信号从这里进入芯片。接线方向必须交叉：开发板 PA10 接 USB-TTL TX，开发板 PA9 接 USB-TTL RX。

`RX` 是 Receive，接收方向是“电脑 -> USB-TTL -> STM32”。PA10 不需要配置成复用推挽输出，因为它不是驱动电平的一方；它要做的是把外部串口电平送入 USART1 接收器。寄存器版设置 `MODE10=00, CNF10=01`，也就是浮空输入。

如果 PA10 没接、接反、没有共地，`USART1->SR.RXNE` 通常不会按你发送的字符置位，或者收到随机乱码。排查接收问题时，先不要看命令解析，先看物理连接和 `RXNE` 是否变化。

### 6.4 `AFIO` 是什么

`AFIO` 是复用功能 I/O 模块，属于芯片引脚复用层。

本课使用 PA9 的 USART1_TX 复用输出，所以寄存器版打开 `RCC_APB2ENR_AFIOEN`。虽然默认映射不需要改重映射寄存器，但开启 AFIO 时钟是 F1 复用功能配置里的稳妥做法。

在 STM32F1 中，很多外设信号都可以映射到 GPIO 引脚上，AFIO 负责这类复用功能管理。USART1 默认映射是 PA9/PA10；如果以后用重映射，才会进一步操作 `AFIO->MAPR`。本课不改映射，但 PA9 的模式仍然是“复用功能输出”。

它和 GPIO 的关系要分清：GPIO 配置引脚电气模式，AFIO 管复用信号通路。你不能只开 USART1 时钟而忘了 GPIOA，也不能只配 GPIOA 而没开 USART1。三者配合后，PA9 上才会出现 USART1_TX 波形。

### 6.5 `BRR` 是什么

`BRR` 是 USART Baud Rate Register，波特率寄存器。

USART1 时钟来自 PCLK2，本课为 72MHz。115200、16 倍过采样时，`USARTDIV=39.0625`，编码后 `BRR=0x0271`。如果系统时钟或 BRR 错，串口助手通常显示乱码。

`BRR` 属于 USART 寄存器层，它控制发送和接收的位时间。串口没有独立时钟线，双方只能约定“每一位持续多久”。STM32 端靠 `BRR` 从 PCLK2 分频得到采样节奏，电脑串口助手靠你设置的 115200 来解码。如果两边节奏不一致，同一串电平会被切成错误的 bit。

寄存器版在 `usart1_init()` 里直接写 `USART1->BRR = 0x0271U`。这个值成立的前提是 `system_clock_72mhz_init()` 让 PCLK2 等于 72MHz。HAL 版不直接写数字，而是通过 `huart1.Init.BaudRate = 115200` 让 `HAL_UART_Init()` 根据当前时钟自动计算。

### 6.6 `8N1` 是什么

`8N1` 是串口帧格式：8 个数据位、无校验、1 个停止位。

电脑串口助手和 STM32 必须使用相同格式。格式不一致会导致接收错误或乱码。

### 6.7 `TXE` 是什么

`TXE` 是 Transmit Data Register Empty，发送数据寄存器空标志，位于 USART `SR`。

`TXE=1` 表示可以向 `DR` 写下一个字节。若 `TXE=0` 时强行写，可能覆盖尚未处理的数据。

`TXE` 控制的是 CPU 能不能把下一个字节交给 USART，不等于这个字节已经从 PA9 线上完全发完。写 `DR` 后，数据会进入发送数据寄存器或移位流程，硬件按起始位、8 个数据位、停止位逐位输出。一个 8N1 字节在 115200 下约 86.8us。

代码位置是 `usart1_send_byte()`：`while ((USART1->SR & USART_SR_TXE) == 0U) {}`。这个 while 就是发送侧轮询。它会占用 CPU，但逻辑直观，非常适合第一节串口课。

### 6.8 `RXNE` 是什么

`RXNE` 是 Read Data Register Not Empty，接收数据寄存器非空标志，位于 USART `SR`。

`RXNE=1` 表示 `DR` 里已有新字节。读取 `DR` 后，硬件会清除 RXNE。

`RXNE` 是接收侧最重要的状态位。外部串口波形进入 PA10 后，USART 接收器按 `BRR` 设置的节奏采样，拼出一个完整字节，再把它放入 `DR` 并置位 `RXNE`。CPU 看到 `RXNE=1` 后再读 `DR`，才是有效接收。

本课用非阻塞方式检查 `RXNE`：有数据就读，没有数据就立即返回。下一课中断接收其实还是同一个标志，只是把“CPU 主动检查 RXNE”变成“RXNE 置位后触发中断通知 CPU”。

### 6.9 `DR` 是什么

`DR` 是 USART Data Register。

写 `DR` 会启动发送路径；读 `DR` 会取出接收字节。它是 CPU 和 USART 数据交换的入口。

`DR` 是一个名字、两个方向。发送时 CPU 写 `USART1->DR = byte`，硬件把这个字节送到发送移位器；接收时 CPU 读 `(uint8_t)USART1->DR`，硬件返回刚收到的字节，并清掉 `RXNE`。理解这个双向角色后，`TXE` 和 `RXNE` 就不会混。

如果接收时忘记读 `DR`，`RXNE` 会一直保持已接收状态，后续数据可能溢出或无法正常处理。如果发送时不等 `TXE` 就写 `DR`，可能导致字节丢失。`DR` 本身简单，难点是必须和状态位配合使用。

### 6.10 `轮询` 是什么

轮询是 CPU 主动反复检查状态位。

本课发送时等待 `TXE`，接收时检查 `RXNE`。优点是简单；缺点是 CPU 要花时间看状态，尤其接收不知道什么时候到来。

轮询属于软件控制方式，不是某个单独硬件模块。硬件只提供 `SR` 标志位，CPU 用 while 或 if 一遍遍读取这些标志。发送字符串时，轮询等待通常还能接受，因为 CPU 主动知道自己要发多少字节；接收轮询更浪费，因为外部数据什么时候来并不由 CPU 决定。

本课特意把接收写成非阻塞轮询，而不是 `while (!RXNE)` 死等。这样主循环即使没收到字符，也可以继续执行别的任务。虽然本课主循环没安排别的任务，但这个结构为下一课中断接收做铺垫。

### 6.11 `非阻塞接收` 是什么

非阻塞接收是检查一次状态后立即返回。

寄存器版 `usart1_receive_byte_nonblocking()` 中，`RXNE=1` 就读字节并返回 1；否则返回 0。HAL 版 `HAL_UART_Receive(..., 0U)` 也采用立即检查，没有数据就返回超时。

### 6.12 `HAL_UART_Init` 是什么

`HAL_UART_Init()` 是 HAL 的 UART 初始化函数。

它根据 `UART_HandleTypeDef.Init` 写 `BRR/CR1/CR2/CR3` 等寄存器，包括波特率、数据位、停止位、校验、收发使能。

### 6.13 `HAL_UART_Transmit` 是什么

`HAL_UART_Transmit()` 是 HAL 的轮询发送函数。

它内部等待 TXE，把缓冲区字节逐个写入 USART 数据寄存器，并等待发送完成或超时。它对应寄存器版 `usart1_send_byte()` 和 `usart1_send_string()`。

### 6.14 `HAL_UART_Receive` 是什么

`HAL_UART_Receive()` 是 HAL 的轮询接收函数。

本课超时参数为 0，含义是立即检查：有数据返回 `HAL_OK`，没数据返回 `HAL_TIMEOUT`。它对应寄存器版检查 `SR.RXNE` 再读 `DR`。

### 6.15 `回显` 是什么

回显是收到一个字符后再发回给电脑。

它不是 USART 必需功能，而是调试手段。看到 `RX: x` 可以确认 RX 接收、命令解析和 TX 发送都工作。

### 6.16 `PC13 低电平点亮` 是什么

BluePill 的 PC13 LED 常见连接方式是低电平点亮。

所以 `led_on()` 写 `BRR` 或 HAL 写 `GPIO_PIN_RESET`；`led_off()` 写 `BSRR` 或 HAL 写 `GPIO_PIN_SET`。逻辑反了时，命令效果会看起来相反。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟

`system_clock_72mhz_init()` 把 SYSCLK 配到 72MHz，PCLK2 也为 72MHz。USART1 挂在 APB2 上，`BRR` 计算依赖这个频率。

### 7.2 PC13 初始化

打开 GPIOC 时钟，配置 PC13 为推挽输出，初始拉高熄灭 LED。

### 7.3 打开 GPIOA、AFIO、USART1 时钟

`RCC->APB2ENR` 中打开 `IOPAEN/AFIOEN/USART1EN`。GPIO 引脚、复用功能和 USART 外设都需要时钟。

这一步有三个对象：GPIOA 让 PA9/PA10 的配置寄存器可用，AFIO 让复用功能路径可用，USART1 让串口外设本体可用。它们都挂在 APB2，所以都写 `RCC->APB2ENR`。如果只开 GPIOA，PA9/PA10 模式能配，但 USART 不会发送；如果只开 USART1，不配 GPIO，外设内部工作也出不了引脚。

看到串口完全无输出时，这一组时钟是第一批检查对象。寄存器版能很直观地看到三个使能位，HAL 版则分散在 `__HAL_RCC_GPIOA_CLK_ENABLE()` 和 `__HAL_RCC_USART1_CLK_ENABLE()` 里。

### 7.4 配置 PA9

PA9 在 `GPIOA->CRH` 中配置。`MODE9=11` 表示 50MHz 输出，`CNF9=10` 表示复用推挽输出。这样 USART1_TX 能驱动 PA9。

### 7.5 配置 PA10

PA10 在 `GPIOA->CRH` 中配置为 `MODE10=00, CNF10=01`，即浮空输入。外部 USB-TTL TX 驱动它。

### 7.6 设置 `BRR=0x0271`

这句把 USART1 配成 115200 波特率。只要系统时钟变了，就必须重新计算 BRR。

完整计算是：`USARTDIV = 72000000 / (16 * 115200) = 39.0625`。整数部分 39 写入 `DIV_Mantissa`，小数部分 `0.0625 * 16 = 1` 写入 `DIV_Fraction`，所以编码为 `(39 << 4) | 1 = 0x0271`。

这一步的硬件后果是 USART 之后按 115200 bit/s 解释 PA10 的输入，也按同样节奏驱动 PA9 输出。乱码最常见原因就是这里的时钟假设和实际板子不一致，例如 HSE 没起、PCLK2 不是 72MHz、串口助手不是 115200。

### 7.7 设置 `CR1`

`USART1->CR1 = TE | RE` 打开发送器和接收器，再设置 `UE` 打开 USART 总使能。没有 UE，TE/RE 不生效。

`TE` 是 Transmitter Enable，让发送器接管 TX 功能；`RE` 是 Receiver Enable，让接收器开始监听 RX；`UE` 是 USART Enable，是整个 USART 的总开关。代码先写 TE/RE，再置 UE，是常见初始化顺序：先把工作模式准备好，再开外设。

本课没有配置校验、字长和停止位的特殊值，使用默认 8 数据位、无校验、1 停止位。也就是说，`CR1` 里没有额外设置 `M/PCE`，`CR2` 里没有改 `STOP`。HAL 版中这些默认意图由 `WordLength=8B`、`Parity=NONE`、`StopBits=1` 明确表达。

### 7.8 发送单字节

`usart1_send_byte()` 先轮询 `SR.TXE`，然后写 `DR`。写入后硬件自动按 8N1 帧格式发送。

从硬件后果看，等待 `TXE=1` 是在确认发送数据寄存器有空位；写 `DR` 是把一个并行字节交给 USART；之后 USART 自动添加起始位和停止位，并通过 PA9 逐位输出。CPU 不需要自己翻转 PA9，也不需要延时产生每个 bit。

这个函数是后面 `usart1_send_string()`、回显、提示语输出的最小单元。若欢迎信息只输出第一个字符或缺字符，通常说明 TXE 等待、BRR、PA9 复用输出或 USB-TTL 接线有问题。

### 7.9 发送字符串

`usart1_send_string()` 循环发送字符，直到遇到 `'\0'`。每个字符都会走一次 TXE 等待和 DR 写入。

### 7.10 非阻塞接收

`usart1_receive_byte_nonblocking()` 检查 `RXNE`。有数据就读 `DR` 并返回 1；没数据返回 0。读 DR 会清 RXNE。

函数参数 `uint8_t *byte` 是输出参数。因为函数返回值已经用来表示“有没有收到”，真正的数据要通过指针写回给调用者。主循环只有在返回 1 时才使用 `ch`，这样不会把旧字符误当作新命令。

这就是 C 代码层和硬件状态层的配合：`SR.RXNE` 是硬件状态，`*byte` 是软件交接变量，返回值是函数协议。以后写驱动时，经常会看到这种“状态位 + 输出参数 + 成功/失败返回值”的组合。

### 7.11 命令解析

主循环收到字符后回显，再判断 `1/0/t/T` 控制 LED。这个业务层建立在 USART 收发链路正确之上。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init`

`HAL_Init()` 准备 HAL Tick 和基础状态。HAL 的超时逻辑依赖 Tick。

### 8.2 HAL 时钟配置

`HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 对应寄存器版 HSE、PLL、分频和 Flash 延迟。

### 8.3 HAL GPIO 配置

PA9 的 `GPIO_MODE_AF_PP` 对应复用推挽输出；PA10 的 `GPIO_MODE_INPUT` 和 `GPIO_NOPULL` 对应输入无上下拉。

### 8.4 UART 句柄

`huart1.Instance = USART1` 选择外设。`BaudRate=115200`、`WordLength=8B`、`StopBits=1`、`Parity=NONE`、`Mode=TX_RX` 描述 8N1 收发。

`UART_HandleTypeDef` 是 HAL 工程层的入口对象。它不只是初始化参数，还包含 HAL 内部状态、锁和错误码。`Instance` 把这个句柄绑定到具体硬件 USART1；`Init` 里的字段告诉 HAL 要把 USART1 配成什么格式。

把字段翻译成寄存器：`BaudRate` 影响 `BRR`，`WordLength/Parity` 影响 `CR1`，`StopBits` 影响 `CR2`，`Mode=TX_RX` 影响 `CR1.TE/RE`，`HwFlowCtl=NONE` 影响 `CR3`。HAL 不是绕开寄存器，而是用结构体字段描述要写哪些寄存器位。

### 8.5 `HAL_UART_Init`

HAL 根据句柄字段自动计算 BRR，并配置 CR1/CR2/CR3。它对应寄存器版 `BRR` 和 `CR1.TE/RE/UE`。

### 8.6 `HAL_UART_Transmit`

`uart1_send_string()` 调用 `HAL_UART_Transmit()`，长度来自 `strlen()`，超时为 `HAL_MAX_DELAY`。内部轮询 TXE/TC 完成发送。

这个 API 的参数分别是句柄、数据缓冲区、长度和超时。长度很关键：HAL 不认识 C 字符串结尾的 `'\0'`，它只按你给的长度发送指定字节数。本课用 `strlen()` 计算长度，所以不会把字符串结束符发出去。

`HAL_MAX_DELAY` 基本表示愿意一直等到发送完成。它适合本课这种提示语输出；但在真实项目中，如果串口硬件异常或状态机卡住，长阻塞会影响主循环响应。理解寄存器版 TXE 等待后，你就知道 HAL 的“阻塞发送”到底卡在哪里。

### 8.7 `HAL_UART_Receive` 超时 0

主循环调用 `HAL_UART_Receive(&huart1, &ch, 1U, 0U)`。超时 0 表示立即检查，不等待。收到 1 字节返回 `HAL_OK`，否则返回 `HAL_TIMEOUT`。

这行代码是 HAL 版的非阻塞轮询核心。第三个参数 `1U` 表示本次只想收 1 个字节；第四个参数 `0U` 表示不愿等待。如果这一刻 `RXNE` 已经置位，HAL 读 `DR` 并返回 `HAL_OK`；如果没有，立刻返回超时，主循环继续转。

不要把这个 API 和下一课的 `HAL_UART_Receive_IT()` 混淆。`HAL_UART_Receive()` 即使超时为 0，本质仍是当前函数调用里检查状态；`HAL_UART_Receive_IT()` 则是配置好中断接收任务后立即返回，真正数据到来时由 IRQ 和回调处理。

### 8.8 HAL LED 控制

`HAL_GPIO_WritePin()` 对应写 BSRR/BRR，`HAL_GPIO_TogglePin()` 对应读取当前输出再翻转。PC13 仍然是低电平点亮。

HAL 版 LED 控制和 UART 无直接关系，但它是命令解析结果的可见出口。发送 `1` 后调用 `led_on()`，底层写的是 PC13 低电平；发送 `0` 后调用 `led_off()`，底层写的是高电平。若串口回显正常但 LED 现象反了，不要查 USART，先查 PC13 极性。

### 8.9 HAL 返回值和超时意识

`HAL_UART_Transmit()` 和 `HAL_UART_Receive()` 都有返回值。发送字符串时，本课返回非 `HAL_OK` 就进入 `error_handler()`；接收时，`HAL_TIMEOUT` 不是错误，而是“这一刻没有收到数据”。

这点很重要。HAL 的超时机制把轮询等待包装成 API 返回值：`HAL_OK` 表示指定字节数完成，`HAL_TIMEOUT` 表示时间到了还没完成，`HAL_ERROR` 表示硬件或参数错误。主循环中只在 `HAL_OK` 时处理命令，正是非阻塞轮询的 HAL 写法。

如果把 `HAL_TIMEOUT` 当错误处理，程序会在没收到字符时不断进错误处理；如果完全忽略返回值，又会把旧的 `ch` 当新数据。HAL 版并不是少思考，而是把思考点从状态寄存器换成 API 状态。

## 9. 两个版本真正应该怎么学

寄存器版帮助你看清 USART 的基本状态机：TXE 可写、RXNE 可读、BRR 定速、DR 承载字节。HAL 版把这些动作封装成句柄和函数，但本质仍然是轮询状态位。

先用寄存器版建立“状态位驱动收发”的直觉，再用 HAL 版学习工程写法。

## 10. 检验问题清单

### 10.1 PA9 为什么不是普通推挽输出

**答**：因为 PA9 要输出 USART1_TX 复用信号，引脚电平由 USART 外设控制，不由 GPIO 软件直接控制。

### 10.2 PA10 为什么是输入

**答**：PA10 接收 USB-TTL 发来的串口电平，方向是外部到芯片，所以配置为输入。

### 10.3 `BRR=0x0271` 的前提是什么

**答**：前提是 USART1 时钟 PCLK2 为 72MHz，目标波特率为 115200，采用 16 倍过采样。

### 10.4 发送前为什么等 TXE

**答**：TXE=1 表示发送数据寄存器空，可以写下一个字节；TXE=0 时写可能覆盖数据。

### 10.5 接收时为什么读 DR

**答**：RXNE=1 表示 DR 中有新数据。读 DR 取出字节，同时硬件清 RXNE。

### 10.6 非阻塞接收的好处是什么

**答**：没数据时立即返回，主循环可以继续做其他事，不会一直卡在等待 RXNE。

### 10.7 HAL 版超时 0 表示什么

**答**：表示立即检查接收状态，有数据就返回 `HAL_OK`，没数据就返回超时。

### 10.8 串口乱码优先查什么

**答**：优先查波特率、系统时钟、串口助手 8N1 参数和 GND 共地。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求是 USART1 用轮询方式收发单字节命令，并用命令控制 PC13 LED。

更具体地拆开：上电后要先向电脑输出提示信息；主循环要不断检查是否收到 1 个字节；收到后要回显，证明 RX 和 TX 都通；最后把字符解释成 LED 命令。这个需求同时覆盖发送、接收、命令解析和 GPIO 输出，不是单纯“让串口打印一行字”。

### 11.2 硬件核查

PA9 接 USB-TTL RX，PA10 接 USB-TTL TX，GND 共地。串口助手使用 115200 8N1。

还要确认 USB-TTL 是 3.3V TTL 电平，不是 RS232 电平，也不要把 5V 输出直接接到 STM32 RX。若模块有 3.3V/5V 跳线，优先设为 3.3V。电脑端串口号要选对，打开串口助手后再复位开发板，最容易看到欢迎信息。

### 11.3 寄存器路线

进入 `21_uart_polling/reg`，重点读 `usart1_gpio_init()`、`usart1_init()`、`usart1_send_byte()`、`usart1_receive_byte_nonblocking()`。

```sh
pio run
pio run -t upload
```

### 11.4 HAL 路线

进入 `21_uart_polling/hal`，重点读 `UART_HandleTypeDef` 初始化、`HAL_UART_Transmit()` 和 `HAL_UART_Receive(..., 0U)`。

### 11.5 工程思维

轮询适合简单实验和短等待，不适合长时间等待未知到来的接收数据。接收场景通常更适合中断或 DMA。

发送和接收的工程价值不一样。发送是 CPU 主动发起的，短字符串轮询发送可以接受；接收是外部异步到来的，如果 CPU 一直轮询，就会浪费大量时间。下一课把 RXNE 改成中断，就是为了解决“外部什么时候发数据，CPU 不应该一直猜”的问题。

### 11.6 常见工程陷阱

TX/RX 接反、忘记共地、波特率不一致、PA9 没配复用输出、PA10 没配输入、PC13 LED 极性理解反，都会造成现象异常。

还有一个常见误区是把“能发送”当成“接收也一定没问题”。发送只验证 PA9 到 USB-TTL RX 的方向，接收还要验证 USB-TTL TX 到 PA10、RE 使能和 RXNE 标志。看到欢迎信息正常但命令没反应时，应该切到接收路径排查。

另一个误区是把轮询写成永久阻塞。若把本课的非阻塞接收改成 `while (!RXNE)`，主循环就会停在等待输入的位置，其他任务无法执行。轮询不是错，关键是要知道等待时间是否可接受。

## 12. 运行现象

下载后串口助手会看到欢迎信息。发送 `1`，LED 点亮并返回 `LED ON`；发送 `0`，LED 熄灭并返回 `LED OFF`；发送 `t` 或 `T`，LED 翻转并返回 `LED TOGGLE`。

## 13. 常见问题排查

### 13.1 串口助手无输出

检查 PA9 是否接 USB-TTL RX，GND 是否共地，USART1 时钟是否打开，PA9 是否复用推挽，TE/UE 是否使能。

排查时可以分层看：先确认开发板在运行，PC13 命令或调试断点能证明主循环没死；再确认 PA9 电气连接；然后看 `USART1->SR.TXE` 是否会置位、`USART1->BRR` 是否是预期值。若 TXE 正常但电脑没显示，多半是接线、串口号或波特率问题。

### 13.2 输出乱码

检查系统时钟是否 72MHz、BRR 是否为 0x0271、串口助手是否 115200 8N1。

### 13.3 能输出但收不到命令

检查 PA10 是否接 USB-TTL TX，PA10 是否输入模式，RE 是否使能，`RXNE` 是否置位。

“能输出”说明 PA9、BRR、TE、UE 大概率是好的，但不代表 PA10 也接对了。此时重点看接收方向：USB-TTL TX 到 PA10、GND 共地、`USART1->CR1.RE=1`、串口助手确实发送了字符。调试器里观察 `USART1->SR.RXNE` 是最快的硬件证据。

### 13.4 命令收到但 LED 反了

检查 PC13 低电平点亮逻辑。`BRR`/`GPIO_PIN_RESET` 是点亮，`BSRR`/`GPIO_PIN_SET` 是熄灭。

### 13.5 主循环像卡住

确认接收是否使用非阻塞检查。若改成阻塞等待 RXNE，没数据时主循环会一直停在那里。

## 14. 本课最核心的结论

1. USART1 把 CPU 字节和 PA9/PA10 串行波形连接起来。
2. PA9 必须是复用推挽输出，PA10 是输入。
3. `BRR` 决定波特率，时钟基准错会直接乱码。
4. `TXE` 表示可以写发送数据，`RXNE` 表示可以读接收数据。
5. `DR` 是 USART 收发字节的共同数据寄存器。
6. 轮询简单但会消耗 CPU 等待状态位。
7. HAL 的 UART 收发函数底层仍然在等待或检查 USART 状态位。

## 15. 建议你现在怎么读这节课

先画 PA9/PA10 与 USB-TTL 的交叉接线，再读 `BRR/TXE/RXNE/DR`。最后对照 HAL 版，确认 `HAL_UART_Receive(..., 0)` 为什么是非阻塞轮询。

## 16. 扩展练习

- 把波特率改成 9600，并同步修改串口助手。
- 把 HAL 接收超时从 0 改成 1000，观察主循环等待变化。
- 故意接反 TX/RX，验证无输出或无接收现象。
- 增加一个命令 `b`，让 LED 闪烁三次。

## 17. 下一课预告

上一课：[20_adc_dma](../20_adc_dma/README.md)

下一课：[22_uart_interrupt](../22_uart_interrupt/README.md)
