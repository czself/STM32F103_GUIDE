# 第 25 课：USART 数据包协议

## 1. 本课到底在学什么

本课表面现象是：PC 通过串口发送 4 个字节，STM32 解析出命令后控制 PC13 LED。

当前代码使用的帧格式是：

```text
AA CMD DATA 55
```

- `0xAA`：帧头，表示一包数据开始
- `CMD`：命令字节
- `DATA`：命令参数
- `0x55`：帧尾，表示一包数据结束

真正要学的是：USART 接收到的是连续字节流，不是天然分好包的“消息”。程序必须自己定义协议，再用状态机把一个个字节拼成一包，最后再执行动作。

本课的完整链路是：

```text
PC 串口工具发送字节
  -> USB 转串口模块输出 TX/RX 电平
  -> PA10 收到 USART1_RX 信号
  -> USART1 硬件把串行位流还原成字节
  -> RXNE 置位
  -> C 代码读取 USART1->DR
  -> 状态机识别 AA/CMD/DATA/55
  -> handle_packet() 根据 CMD/DATA 控制 PC13
```

这节课不能只记住 `while` 和 `switch`。你要把“线上的字节、电平变化、USART 寄存器、C 状态变量、LED 输出”连成一条因果链。

## 2. 本课学习目标

学完本课，你应该能回答：

1. 为什么串口接收不能默认“一次就是一包”？
2. `PA9` 和 `PA10` 在 USART1 里分别承担什么角色？
3. `USART1->BRR = 0x0271` 为什么对应 72MHz 下的 115200 波特率？
4. `USART_SR_RXNE` 为什么能告诉软件“有新字节到了”？
5. 读 `USART1->DR` 在硬件上会带来什么后果？
6. `WAIT_HEAD`、`WAIT_CMD`、`WAIT_DATA`、`WAIT_TAIL` 四个状态分别在等什么？
7. 为什么收到错误帧尾后要回到 `WAIT_HEAD`？
8. HAL 版的 `HAL_UART_Receive()` 对应寄存器版哪一个等待和读数据动作？
9. 为什么 PC13 LED 在很多 BluePill 上是低电平亮？

## 3. 本课目录结构

```text
25_uart_packet_protocol/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 使用 `USART1->SR`、`USART1->DR`、`USART1->BRR` 等寄存器直接收字节。  
`hal/` 使用 `UART_HandleTypeDef` 和 `HAL_UART_Receive()` 做同一件事。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 串口连接：USB 转串口模块
- 串口参数：115200，8 数据位，无校验，1 停止位
- LED：PC13 板载 LED，多数 BluePill 为低电平点亮

接线：

```text
USB-TTL TX  -> PA10 / USART1_RX
USB-TTL RX  -> PA9  / USART1_TX
USB-TTL GND -> BluePill GND
```

本课代码没有主动发送调试字符串，但仍配置了 `PA9` 作为 USART1_TX，方便你后续扩展回传 ACK 或调试信息。

## 5. 先建立一个最基本的脑图

```text
system_clock_72mhz_init()
  -> 让 SYSCLK=72MHz，PCLK2=72MHz

pc13_led_init()
  -> 打开 GPIOC
  -> 把 PC13 配成推挽输出
  -> 默认输出高电平，LED 熄灭

usart1_init()
  -> 打开 GPIOA 和 USART1 时钟
  -> PA9 配成复用推挽输出
  -> PA10 配成输入
  -> BRR 配成 115200
  -> CR1 使能发送、接收和 USART

main() while(1)
  -> 等 RXNE
  -> 读 DR 得到一个字节
  -> 状态机判断它属于帧头、命令、数据还是帧尾
  -> 帧完整且 CMD=0x01 时控制 LED
```

你要特别注意：协议解析发生在 C 代码层，但它依赖 USART 硬件已经把电平波形恢复成字节。如果 GPIO 复用、波特率或接线错了，状态机再正确也收不到正确字节。

## 6. 先认识本课里出现的核心名词

### 6.1 `USART1` 是什么

`USART1` 是 STM32F103 里的通用同步/异步串行通信外设。本课使用它的异步串口能力，也就是常说的 UART 通信。

它属于芯片外设层，挂在 APB2 总线上。代码里的 `USART1->BRR`、`USART1->CR1`、`USART1->SR`、`USART1->DR` 都是在访问这个外设内部的寄存器。

它控制的行为是：按照设定波特率，把 `PA10` 上的串行电平转换成字节，或者把字节从 `PA9` 输出为串行电平。本课主要用接收方向。

它出现在本课，是因为数据包协议的输入来自 PC 串口工具。没有 USART1，STM32 只能看到引脚电平变化，不能自动按起始位、数据位、停止位还原成字节。

如果 USART1 时钟没开、CR1 没使能、波特率不对，现象通常是：程序卡在等待 `RXNE`，或者收到的字节全乱，LED 不按发送命令变化。

### 6.2 `PA9 / PA10` 是什么

`PA9` 和 `PA10` 是 GPIOA 的两个引脚，也是 USART1 的默认复用引脚。

- `PA9`：USART1_TX，发送脚
- `PA10`：USART1_RX，接收脚

它们属于物理引脚层和 GPIO 复用层之间的接口。USART1 硬件在芯片内部，外部 USB-TTL 模块只能接到芯片引脚，所以必须把 USART1 的 TX/RX 功能映射到对应引脚模式上。

本课寄存器版在 `GPIOA->CRH` 里配置：

- `PA9`：复用推挽输出，用于 TX
- `PA10`：输入模式，用于 RX

如果接线把 TX/RX 接反，或者 PA10 没接 USB-TTL 的 TX，程序通常一直等不到正确帧。若 GND 没共地，串口电平没有共同参考，也会表现为乱码或完全无响应。

### 6.3 `GPIOA->CRH` 是什么

`GPIOA->CRH` 是 GPIOA 的高位配置寄存器，负责配置 `PA8` 到 `PA15`。

本课的 `PA9` 和 `PA10` 都在 8-15 范围内，所以要改 `CRH`，不是 `CRL`。

它属于寄存器/bit 层，控制引脚模式：

- `MODE9=10`：PA9 作为输出，速度 2MHz
- `CNF9=10`：PA9 为复用推挽输出
- `MODE10=00`：PA10 为输入
- `CNF10=01`：PA10 为浮空输入

如果把 PA9 配成普通 GPIO 输出，USART1 的 TX 信号到不了引脚。如果把 PA10 配成输出，外部 USB-TTL 发送来的电平不能作为 USART RX 正常采样。

### 6.4 `USART1->BRR` 是什么

`BRR` 是 USART 的 Baud Rate Register，波特率寄存器。

它属于 USART1 的寄存器层，决定 USART 按多快的节奏采样和发送串口位。当前代码：

```c
USART1->BRR = 0x0271;
```

在 72MHz 的 PCLK2 下对应约 115200 波特率。串口两端必须使用相同波特率；PC 串口工具设 9600，而 STM32 设 115200，就会出现字节错误。

本课必须配置它，因为协议帧的 `0xAA` 和 `0x55` 最终都是一串高低电平。如果采样节奏错，硬件恢复出的字节就不是你发送的字节。

### 6.5 `USART1->CR1` 是什么

`CR1` 是 USART 控制寄存器 1。

当前代码写入：

```c
USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
```

含义是：

- `TE`：允许发送器工作
- `RE`：允许接收器工作
- `UE`：打开 USART 总使能

它控制的是 USART 外设是否真的开始接收/发送。只配置引脚和波特率还不够，`UE` 没有置位时 USART 外设仍不会工作。

如果漏掉 `RE`，接收方向不会产生 `RXNE`。如果漏掉 `UE`，整个 USART 不启动。

### 6.6 `USART_SR_RXNE` 是什么

`RXNE` 是 Receive data register Not Empty，接收数据寄存器非空标志。

它属于 USART 状态寄存器 `SR` 里的 bit。硬件收到一个完整字节后，把字节放进 `DR`，然后置位 `RXNE`，提醒软件可以读取。

本课寄存器版的等待代码是：

```c
while ((USART1->SR & USART_SR_RXNE) == 0U) {
}
```

它控制的是软件什么时候离开等待状态。没有新字节时主循环阻塞在这里；有新字节时继续向下读 `DR`。

如果一直没有 `RXNE`，通常查：USART1 时钟、PA10 接线、波特率、USB-TTL 电平和 GND。

### 6.7 `USART1->DR` 是什么

`DR` 是 Data Register，数据寄存器。

接收时，读 `USART1->DR` 得到刚收到的字节。发送时，写 `USART1->DR` 会把字节交给 USART 发送器。

本课使用：

```c
return (uint8_t)USART1->DR;
```

这一步不仅是取值，还会配合硬件清除接收相关状态。也就是说，软件读得太慢会丢字节；读了以后还不保存，那个字节就被消费掉了。

如果状态机偶尔错位，很常见的原因是上位机发送太快、程序处理中断/阻塞太久，或者没有设计缓冲区导致字节被覆盖。本课先用最简单的轮询方式，让你看清“一个字节进来、处理一个字节”的基本过程。

### 6.8 `帧头 0xAA` 是什么

帧头是协议层概念，不是 STM32 硬件寄存器。

`0xAA` 的二进制是 `10101010`，常被用作容易观察的同步字节。本课把它定义为一包数据的开始。

它控制的是状态机从“乱流中等待开始”进入“开始收命令”的时刻。代码里：

```c
case WAIT_HEAD:
    state = (b == 0xAAU) ? WAIT_CMD : WAIT_HEAD;
    break;
```

如果收到的不是 `0xAA`，状态机继续等待。这让接收端即使从半包、乱码或错误字节中进入，也能重新找到下一包的起点。

### 6.9 `帧尾 0x55` 是什么

帧尾也是协议层概念。

`0x55` 的二进制是 `01010101`，和 `0xAA` 形成明显区分。本课用它表示一包结束。

代码在 `WAIT_TAIL` 状态判断：

```c
if (b == 0x55U) handle_packet(cmd, data);
state = WAIT_HEAD;
```

它控制的是命令是否执行。只有帧尾正确时才调用 `handle_packet()`。帧尾错误时直接丢弃当前包并回到等待帧头，避免错误数据控制 LED。

### 6.10 `状态机` 是什么

状态机是 C 代码层的协议解析方法。它用一个变量记录“当前正在等什么”，再根据新字节决定下一步。

本课状态：

- `WAIT_HEAD`：等待 `0xAA`
- `WAIT_CMD`：保存命令字节
- `WAIT_DATA`：保存参数字节
- `WAIT_TAIL`：等待 `0x55`，正确则执行

它控制的是字节流如何变成结构化数据包。串口硬件只负责给你字节，不负责告诉你哪个字节是命令、哪个字节是参数。

如果状态切换写错，比如收到帧尾后不回 `WAIT_HEAD`，下一包就会错位。若 `cmd` 或 `data` 没保存好，LED 动作会和上位机发送内容不一致。

### 6.11 `handle_packet()` 是什么

`handle_packet()` 是协议层到动作层的分界函数。

它接收已经解析出的 `cmd` 和 `data`：

```c
if (cmd == 0x01U) {
    if (data != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}
```

`cmd=0x01` 表示控制 LED。`data!=0` 时把 PC13 拉低，LED 亮；`data=0` 时把 PC13 拉高，LED 灭。

它属于 C/CMSIS 层，但最终动作落到 GPIO 寄存器层。协议解析不直接散落在主循环里，而是集中到这个函数，后续增加 `cmd=0x02`、`cmd=0x03` 时更清楚。

### 6.12 `HAL_UART_Receive()` 是什么

`HAL_UART_Receive()` 是 HAL 层的阻塞式串口接收 API。

本课 HAL 版：

```c
HAL_UART_Receive(&huart1, &b, 1, HAL_MAX_DELAY)
```

含义是：使用 `huart1` 对应的 USART1，接收 1 个字节到变量 `b`，一直等到收到为止。

它封装了寄存器版的“等待 RXNE，再读 DR”。它不替你解析协议，也不会自动识别帧头帧尾；状态机仍然由 `switch (state)` 完成。

如果 `huart1.Instance` 不是 `USART1`，或者 `HAL_UART_Init()` 没成功，`HAL_UART_Receive()` 会等不到正确数据。

### 6.13 `UART_HandleTypeDef huart1` 是什么

`UART_HandleTypeDef` 是 HAL 用来描述一个 UART 外设的软件结构体。

本课里 `huart1.Instance = USART1`，表示这个句柄绑定 USART1。`huart1.Init.BaudRate = 115200`、`WordLength = 8B`、`StopBits = 1` 等字段描述串口格式。

它属于 HAL/工程层。HAL API 通过句柄知道该操作哪个外设、按什么参数初始化、当前状态是什么。

如果句柄字段漏填或填错，HAL 可能会把错误配置写进 USART 寄存器，表现为乱码、收不到数据或初始化失败。

## 7. 寄存器版代码逐步讲解

### 7.1 `system_clock_72mhz_init()`：先建立波特率基准

USART 波特率来自外设时钟。USART1 挂在 APB2，总线时钟在本课配置为 72MHz。

函数先设置：

```c
FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
```

72MHz 下 Flash 取指需要等待周期，否则 CPU 高速运行时可能取指不稳定。

然后打开 HSE：

```c
RCC->CR |= RCC_CR_HSEON;
while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
}
```

`HSEON` 让外部 8MHz 晶振启动，`HSERDY` 表示稳定。没有稳定时就切 PLL，会让系统时钟不可靠。

随后配置分频和 PLL：

```c
RCC->CFGR |= RCC_CFGR_HPRE_DIV1 |
             RCC_CFGR_PPRE1_DIV2 |
             RCC_CFGR_PPRE2_DIV1 |
             RCC_CFGR_PLLSRC |
             RCC_CFGR_PLLMULL9;
```

结果是：

- HCLK = 72MHz
- PCLK1 = 36MHz
- PCLK2 = 72MHz
- USART1 使用 PCLK2

这一步和后面的 `BRR=0x0271` 是因果关系。时钟不是 72MHz，波特率常数就不再正确。

### 7.2 `pc13_led_init()`：准备动作输出

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
```

打开 GPIOC 时钟。不开时钟，写 `GPIOC->CRH` 不会让 PC13 进入正确输出状态。

```c
GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
GPIOC->CRH |= GPIO_CRH_MODE13_1;
```

PC13 属于 8-15 号引脚，所以在 `CRH` 中配置。先清 `MODE13/CNF13`，再设置 `MODE13=10`、`CNF13=00`，含义是 2MHz 通用推挽输出。

```c
GPIOC->BSRR = GPIO_BSRR_BS13;
```

初始化输出高电平。多数 BluePill 的 PC13 LED 低电平亮，所以高电平表示先熄灭。

### 7.3 `usart1_init()`：打开 USART1 和 GPIOA

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
```

GPIOA 和 USART1 都在 APB2 上。本课既要配置 PA9/PA10，又要配置 USART1 寄存器，所以两个时钟都要开。

如果只开 USART1 不开 GPIOA，引脚模式无法正确设置。只开 GPIOA 不开 USART1，串口外设不工作。

### 7.4 配置 PA9/PA10 的复用关系

```c
GPIOA->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 |
                GPIO_CRH_MODE10 | GPIO_CRH_CNF10);
GPIOA->CRH |= GPIO_CRH_MODE9_1 |
              GPIO_CRH_CNF9_1 |
              GPIO_CRH_CNF10_0;
```

这段代码一次配置两个引脚：

- PA9：`MODE9=10`，`CNF9=10`，复用推挽输出
- PA10：`MODE10=00`，`CNF10=01`，浮空输入

硬件后果是：USART1_TX 能从 PA9 输出，USART1_RX 能从 PA10 输入。

本课虽然没有主动发送数据，但 PA9 仍按 USART TX 配好，这是完整串口初始化的一部分。

### 7.5 配置 `USART1->BRR`

```c
USART1->BRR = 0x0271;
```

`BRR` 让 USART1 知道每一位持续多久。PC 串口工具也要设置 115200。

如果 PC 设置成 9600，STM32 仍按 115200 采样，`0xAA` 可能会被读成别的值，状态机就一直等不到正确帧头。

### 7.6 配置 `USART1->CR1`

```c
USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
```

这句打开三个能力：

- `TE`：发送器
- `RE`：接收器
- `UE`：USART 总开关

寄存器配置到这里，硬件才具备“从 PA10 收字节”的能力。

### 7.7 `usart1_read_byte()`：轮询等待一个字节

```c
while ((USART1->SR & USART_SR_RXNE) == 0U) {
}
return (uint8_t)USART1->DR;
```

这就是寄存器版接收的核心。

当没有新字节时，CPU 会停在 `while` 里。收到完整字节后，硬件置位 `RXNE`，循环退出，代码读取 `DR`。

这个写法简单直观，但缺点也明显：等待期间 CPU 不能做别的事。后续中断、DMA 或 RTOS 课程会解决这个工程问题；本课先把字节流和协议解析讲清楚。

### 7.8 `main()` 中的状态变量

```c
enum { WAIT_HEAD, WAIT_CMD, WAIT_DATA, WAIT_TAIL } state = WAIT_HEAD;
uint8_t cmd = 0, data = 0;
```

`state` 记录解析进度，`cmd/data` 保存当前包内容。

这三个变量属于 C 软件层，但它们处理的数据来自 `USART1->DR`。你可以在调试器 Watch 里观察它们，发送 `AA 01 01 55` 时会看到状态依次变化。

### 7.9 `WAIT_HEAD`：寻找帧头

```c
case WAIT_HEAD:
    state = (b == 0xAAU) ? WAIT_CMD : WAIT_HEAD;
    break;
```

只有收到 `0xAA` 才认为一包开始。收到其他字节就继续等。

这个状态让协议具备重新同步能力。即使上电时 PC 已经发了一半数据，STM32 也会丢弃无关字节，直到下一个 `0xAA`。

### 7.10 `WAIT_CMD` 和 `WAIT_DATA`：保存包内容

```c
case WAIT_CMD:
    cmd = b;
    state = WAIT_DATA;
    break;

case WAIT_DATA:
    data = b;
    state = WAIT_TAIL;
    break;
```

这里没有立即执行动作，因为一包还没确认结束。先保存命令和参数，等帧尾正确后再处理。

如果你以后增加长度字段、校验字段，也是在这个阶段扩展状态机。

### 7.11 `WAIT_TAIL`：确认帧尾并执行

```c
case WAIT_TAIL:
    if (b == 0x55U) handle_packet(cmd, data);
    state = WAIT_HEAD;
    break;
```

只有帧尾正确才执行命令。无论正确与否，最后都回到 `WAIT_HEAD`，准备下一包。

如果这里不回到 `WAIT_HEAD`，状态机会卡在某个阶段，下一包无法正确解析。

### 7.12 `handle_packet()`：从协议到 GPIO

```c
if (cmd == 0x01U) {
    if (data != 0U) GPIOC->BRR = GPIO_BRR_BR13;
    else GPIOC->BSRR = GPIO_BSRR_BS13;
}
```

`CMD=0x01` 被定义为 LED 控制命令。

- `DATA=1`：写 `BRR`，PC13 低电平，LED 亮
- `DATA=0`：写 `BSRR`，PC13 高电平，LED 灭

这一步把协议层命令落到寄存器层动作。若 PC13 硬件接法不同，亮灭逻辑可能相反。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 是什么

`HAL_Init()` 初始化 HAL 基础环境，包括 HAL Tick。

本课 HAL 版的串口接收使用 `HAL_MAX_DELAY` 阻塞等待，虽然不直接用 `HAL_Delay()` 做接收节拍，但 HAL 的基础初始化仍是 HAL 工程入口。

### 8.2 `RCC_OscInitTypeDef` 和 `RCC_ClkInitTypeDef`

HAL 版用两个结构体描述时钟：

- `RCC_OscInitTypeDef`：选择 HSE、PLL 源、PLL 倍频
- `RCC_ClkInitTypeDef`：选择 SYSCLK、AHB/APB 分频

它们对应寄存器版的 `RCC->CR` 和 `RCC->CFGR` 操作。

`FLASH_LATENCY_2` 对应寄存器版 `FLASH_ACR_LATENCY_2`。这说明 HAL 不是跳过硬件，而是换了一种写法配置同一批寄存器位。

### 8.3 `GPIO_InitTypeDef` 配置 PC13

```c
gpio.Pin = GPIO_PIN_13;
gpio.Mode = GPIO_MODE_OUTPUT_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &gpio);
```

这些字段对应寄存器版的 `GPIOC->CRH`：

- `GPIO_PIN_13`：选择 PC13
- `GPIO_MODE_OUTPUT_PP`：通用推挽输出
- `GPIO_SPEED_FREQ_LOW`：低速输出
- `GPIO_NOPULL`：输出模式下不需要内部上下拉

`HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET)` 对应写 `BSRR`，让 LED 默认熄灭。

### 8.4 `usart1_init()` 中的 PA9

```c
gpio.Pin = GPIO_PIN_9;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_MODE_AF_PP` 对应寄存器版 PA9 的复用推挽输出。它让 USART1_TX 信号能驱动 PA9。

如果把它写成 `GPIO_MODE_OUTPUT_PP`，PA9 只是普通输出脚，不再由 USART1 控制。

### 8.5 `usart1_init()` 中的 PA10

```c
gpio.Pin = GPIO_PIN_10;
gpio.Mode = GPIO_MODE_INPUT;
gpio.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOA, &gpio);
```

这对应寄存器版 PA10 的输入配置。`GPIO_NOPULL` 表示不启用内部上下拉，外部 USB-TTL 模块直接驱动 RX 电平。

如果 RX 线悬空，可能出现随机字节；实际接线时必须确保 USB-TTL TX 接 PA10，并且共地。

### 8.6 `UART_HandleTypeDef` 字段映射

```c
huart1.Instance = USART1;
huart1.Init.BaudRate = 115200;
huart1.Init.WordLength = UART_WORDLENGTH_8B;
huart1.Init.StopBits = UART_STOPBITS_1;
huart1.Init.Parity = UART_PARITY_NONE;
huart1.Init.Mode = UART_MODE_TX_RX;
huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
huart1.Init.OverSampling = UART_OVERSAMPLING_16;
```

这些字段共同决定 USART1 的硬件配置：

- `Instance`：要操作哪个 USART 外设
- `BaudRate`：HAL 根据 PCLK2 计算 BRR
- `WordLength/StopBits/Parity`：决定帧格式
- `Mode`：设置发送和接收使能
- `OverSampling`：决定采样方式

### 8.7 `HAL_UART_Init()` 做了什么

`HAL_UART_Init(&huart1)` 会根据句柄字段写 USART1 的底层寄存器。

在本课里，它主要对应：

- 配置波特率 `BRR`
- 配置帧格式
- 设置 `TE/RE`
- 使能 USART

所以 HAL 版看起来没有手写 `USART1->BRR`，但底层仍必须写这个寄存器。

### 8.8 `HAL_UART_Receive()` 与寄存器版接收

```c
HAL_UART_Receive(&huart1, &b, 1, HAL_MAX_DELAY)
```

参数含义：

- `&huart1`：使用 USART1
- `&b`：接收结果放到变量 `b`
- `1`：只收 1 个字节
- `HAL_MAX_DELAY`：一直阻塞等待

它对应寄存器版：

```c
while ((USART1->SR & USART_SR_RXNE) == 0U) {}
b = (uint8_t)USART1->DR;
```

HAL 只是帮你封装等待和读取，不负责协议解析。后面的 `switch (state)` 与寄存器版完全相同。

### 8.9 `HAL_GPIO_WritePin()` 控制 LED

```c
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
                  data ? GPIO_PIN_RESET : GPIO_PIN_SET);
```

`GPIO_PIN_RESET` 对应把 PC13 拉低，`GPIO_PIN_SET` 对应把 PC13 拉高。

在多数 BluePill 上：

- `data=1` -> RESET -> LED 亮
- `data=0` -> SET -> LED 灭

HAL 版这里对应寄存器版的 `BRR/BSRR`。

## 9. 两个版本真正应该怎么学

寄存器版重点看硬件顺序：

```text
RCC 时钟 -> GPIO 复用 -> USART 波特率/使能 -> RXNE/DR -> 状态机 -> GPIO 输出
```

HAL 版重点看字段映射：

```text
GPIO_InitTypeDef -> GPIO CRH
UART_HandleTypeDef.Init -> USART BRR/CR1
HAL_UART_Receive -> 等 RXNE + 读 DR
HAL_GPIO_WritePin -> BSRR/BRR
```

两份代码的协议状态机几乎一样，这正好说明：HAL 能封装硬件寄存器配置，但不会替你设计通信协议。协议是工程层约定，必须由你明确规定每个字节的含义。

## 10. 检验问题清单

### 10.1 为什么 USART 收到的是字节流，不是天然的一包数据？

**答**：USART 硬件只按起始位、数据位、停止位恢复单个字节。它不知道你的应用协议，所以不会自动区分帧头、命令、数据和帧尾。分包必须由软件状态机完成。

### 10.2 本课为什么要先配置 PA9/PA10？

**答**：USART1 在芯片内部，外部 USB-TTL 模块只能接到引脚。PA9/PA10 的 GPIO 模式决定 USART1 的 TX/RX 信号能不能正确进出芯片。

### 10.3 `RXNE=1` 表示什么？

**答**：表示 USART 接收数据寄存器非空，硬件已经收到一个完整字节并放入 `DR`。软件此时可以读取 `USART1->DR`。

### 10.4 为什么 `BRR` 配错会导致状态机失效？

**答**：`BRR` 决定串口采样节奏。波特率错时，硬件恢复出的字节可能不是 PC 发送的字节，状态机就等不到正确的 `0xAA` 或 `0x55`。

### 10.5 为什么帧尾错误时不执行 `handle_packet()`？

**答**：帧尾错误说明这一包不完整或错位。直接执行可能把错误字节当命令，所以代码丢弃当前包并回到 `WAIT_HEAD`。

### 10.6 `HAL_UART_Receive(&huart1, &b, 1, HAL_MAX_DELAY)` 封装了什么？

**答**：它封装了等待接收完成和读取数据的过程，底层对应轮询 USART 状态并取出接收字节。它不封装协议解析。

### 10.7 发送 `AA 01 01 55` 应该发生什么？

**答**：状态机识别到完整帧，`cmd=0x01`，`data=0x01`，然后 `handle_packet()` 把 PC13 拉低，多数 BluePill 的 LED 会亮。

### 10.8 发送 `AA 01 00 55` 应该发生什么？

**答**：状态机识别到完整帧，`cmd=0x01`，`data=0x00`，然后把 PC13 拉高，多数 BluePill 的 LED 会灭。

## 11. 工程实现步骤

### 11.1 需求分析

本课需求不是“串口能收到字符”这么简单，而是“从连续字节中识别固定格式命令”。所以必须同时完成硬件接收和软件协议两部分。

当前协议只有 4 字节，没有长度和校验，优点是容易入门；缺点是抗干扰能力有限。后续要做真实协议时，应增加长度、校验、转义或超时机制。

### 11.2 硬件核查

确认 USB-TTL 是 3.3V 电平或至少 RX/TX 与 STM32 兼容。接线要交叉：

- USB-TTL TX 接 PA10
- USB-TTL RX 接 PA9
- GND 必须共地

串口工具设置 115200、8N1。发送十六进制字节，不要发送 ASCII 字符串 `"AA010155"`，否则 STM32 收到的是 `0x41 0x41 0x30...`。

### 11.3 寄存器路线

寄存器版实现顺序：

1. 配 72MHz 时钟，保证 USART1 PCLK2 为 72MHz。
2. 打开 GPIOA、GPIOC、USART1 时钟。
3. PC13 配成推挽输出。
4. PA9 配成复用推挽，PA10 配成输入。
5. 设置 `USART1->BRR`。
6. 设置 `TE/RE/UE`。
7. 循环等待 `RXNE`，读 `DR`，交给状态机。

### 11.4 HAL 路线

HAL 版实现顺序：

1. `HAL_Init()`。
2. 用 RCC 结构体配置 72MHz。
3. 用 `GPIO_InitTypeDef` 配 PC13、PA9、PA10。
4. 填写 `UART_HandleTypeDef`。
5. `HAL_UART_Init()` 写入 USART 寄存器。
6. `HAL_UART_Receive()` 每次取 1 字节。
7. 使用同样状态机解析协议。

### 11.5 工程思维

协议解析要和动作执行分层。主循环只负责收字节和推进状态，`handle_packet()` 负责把命令转成动作。

这样做的好处是：后续你增加“查询状态”“设置 PWM”“读取 ADC”等命令时，不需要破坏接收流程，只扩展命令处理函数。

### 11.6 常见工程陷阱

不要把串口工具的显示文本和实际发送字节混淆。十六进制发送 `AA 01 01 55` 和字符串发送 `"AA 01 01 55"` 完全不同。

不要以为 HAL 接收 API 会自动处理协议。HAL 只给你字节，包格式仍由自己解析。

不要忘记 PC13 是低电平亮。若你发送 `DATA=1` 看到 LED 亮，这是符合当前代码和板载电路的。

## 12. 运行现象

下载任一版本后，打开串口工具，选择 115200、8N1、十六进制发送。

发送：

```text
AA 01 01 55
```

多数 BluePill 的 PC13 LED 会亮。

发送：

```text
AA 01 00 55
```

多数 BluePill 的 PC13 LED 会灭。

发送错误帧，例如：

```text
AA 01 01 00
```

因为帧尾不是 `0x55`，LED 不应该按这包命令变化。

## 13. 常见问题排查

### 13.1 LED 完全不变

先确认串口发送的是十六进制字节，不是 ASCII 文本。再确认 USB-TTL TX 接 PA10、GND 共地、串口参数 115200。

如果仍无变化，用调试器看程序是否卡在等待 `RXNE`。卡在这里说明字节没有进入 USART1。

### 13.2 收到命令但亮灭反着

多数 BluePill 的 PC13 LED 是低电平点亮。当前代码 `data!=0` 时写 `BRR` 拉低，所以 LED 亮是正常现象。

如果你的板子 LED 接法不同，亮灭逻辑可能相反。

### 13.3 只有偶尔能控制成功

检查波特率是否一致，USB-TTL 线是否松动，GND 是否可靠。也要确认上位机发送的是 4 个连续字节，中间没有插入空格字符或换行字节。

### 13.4 发送 `AA010155` 没反应

很多串口工具在普通文本模式下会发送 ASCII 字符：`0x41 0x41 0x30 0x31...`。本课需要十六进制发送模式，真正发送 `0xAA 0x01 0x01 0x55`。

### 13.5 程序卡死在接收函数

寄存器版卡在 `while RXNE==0`，HAL 版卡在 `HAL_UART_Receive()`，本质都是没收到字节。按 RCC、GPIO、接线、波特率、串口工具模式顺序排查。

## 14. 本课最核心的结论

1. USART 硬件只恢复字节，应用层的数据包必须由软件定义和解析。
2. 帧头让接收端从字节流中找到一包的开始，帧尾用于确认这一包结束。
3. 状态机的作用是记住“当前等什么”，它是串口协议解析的基础方法。
4. `RXNE` 和 `DR` 是寄存器版接收一个字节的核心。
5. `HAL_UART_Receive()` 封装了接收一个字节，但不替代协议状态机。
6. PC13 LED 的最终变化来自 GPIO 寄存器，串口命令只是触发这一步的上游输入。

## 15. 建议你现在怎么读这节课

第一遍只看第 5 章脑图，确认每个模块的顺序。第二遍读第 6 章，把 USART、GPIO、协议状态机分清层次。第三遍对照第 7、8 章，找出 HAL 字段和寄存器位的对应关系。最后再上板发送 `AA 01 01 55` 和 `AA 01 00 55`。

## 16. 扩展练习

1. 增加 `cmd=0x02`，让它执行 PC13 翻转。
2. 增加一个简单校验字节，例如 `CMD + DATA`，帧格式改成 `AA CMD DATA SUM 55`。
3. 在 HAL 版里把接收从阻塞式改成中断式，再比较代码结构变化。
4. 给状态机增加超时：收到帧头后太久没有收完整包，就回到 `WAIT_HEAD`。

## 17. 下一课预告

- 上一课：[24_uart_printf_redirect](../24_uart_printf_redirect/README.md)
- 下一课：[26_i2c_basic](../26_i2c_basic/README.md)
