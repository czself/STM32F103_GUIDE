# 第 30 课：SPI 基础

## 1. 本课到底在学什么

本课表面现象是：用一根杜邦线把 `PA7(MOSI)` 接到 `PA6(MISO)`，STM32 通过 SPI1 发送 1 个字节，如果收到的字节和发出的字节一致，PC13 LED 每秒翻转一次；如果不一致，LED 点亮表示错误。

真正要学的是 SPI 的同步全双工本质：

```text
主机写 1 个字节到 SPI1->DR
  -> SPI1 在 PA5 输出 8 个 SCK 时钟
  -> 每个时钟从 PA7(MOSI) 移出 1 bit
  -> 同时从 PA6(MISO) 移入 1 bit
  -> 8 bit 后 RXNE 置位
  -> 软件读 SPI1->DR 得到收到的字节
```

本课使用回环，不接外部 SPI 设备，是为了先排除器件协议干扰，只验证 SPI1 主模式、引脚复用、时钟相位极性和收发标志是否真正理解。

## 2. 本课学习目标

学完本课，你应该能回答：

1. SPI 为什么说发送和接收同时发生？
2. `PA5`、`PA6`、`PA7` 分别对应 SPI1 的哪根线？
3. 为什么 SCK/MOSI 要配成复用推挽，而 MISO 是输入？
4. `CPOL=0`、`CPHA=0` 为什么叫 Mode 0？
5. `BR=010` 为什么得到 9MHz SCK？
6. `TXE`、`RXNE`、`BSY` 三个标志各表示什么？
7. 为什么 SPI 读数据前必须先发送一个字节？
8. HAL 版 `HAL_SPI_TransmitReceive()` 对应寄存器版哪几步？

## 3. 本课目录结构

```text
30_spi_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接配置 SPI1 的 GPIO、`CR1`、`SR`、`DR`。  
`hal/` 使用 `SPI_HandleTypeDef` 和 `HAL_SPI_TransmitReceive()` 完成同样的回环测试。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 回环线：`PA7(MOSI)` 接 `PA6(MISO)`
- SPI 时钟脚：`PA5(SCK)` 可用逻辑分析仪观察
- LED：PC13，常见 BluePill 为低电平点亮

本课不需要外部 SPI 从机，也不使用 `PA4(NSS)` 物理片选，SPI1 使用软件 NSS。

## 5. 先建立一个最基本的脑图

```text
72MHz 系统时钟
  -> PCLK2 = 72MHz
  -> SPI1 时钟源来自 PCLK2

GPIOA 复用配置
  -> PA5 = SCK，复用推挽输出
  -> PA7 = MOSI，复用推挽输出
  -> PA6 = MISO，浮空输入

SPI1 配置
  -> 主模式 MSTR=1
  -> Mode 0：CPOL=0，CPHA=0
  -> BR=/8，SCK=9MHz
  -> 软件 NSS：SSM=1，SSI=1
  -> SPE=1 启动 SPI

回环测试
  -> 写 DR 发送 0xA5 或 0x3C
  -> PA7 输出位流
  -> 杜邦线送回 PA6
  -> RXNE 后读 DR
  -> 相等则 LED 翻转
```

SPI 的核心不是“写出去一个字节后再读”，而是“写出去的同时就读回来一个字节”。这一点后面读 Flash、OLED、无线模块时都要用。

## 6. 先认识本课里出现的核心名词

### 6.1 `SPI` 是什么

`SPI` 是 Serial Peripheral Interface，同步串行外设接口。

它属于板级通信层和芯片外设层之间的接口。STM32 做主机时输出时钟 `SCK`，并通过 `MOSI/MISO` 同步移位收发数据。

如果没有 SCK，从机不会自己把数据发出来；这和 UART 这种异步通信不同。

### 6.2 `SPI1` 是什么

`SPI1` 是 STM32F103 内部的 SPI 外设，挂在 APB2 总线上。

代码里的 `SPI1->CR1`、`SPI1->SR`、`SPI1->DR` 都是访问 SPI1 寄存器。它负责按照配置产生 SCK，并在 MOSI/MISO 上移位收发。

如果 `RCC_APB2ENR_SPI1EN` 没打开，配置 SPI1 寄存器不会让外设工作。

### 6.3 `SCK` 是什么

`SCK` 是 SPI 时钟线。本课使用 `PA5`。

它由主机输出，决定每一 bit 什么时候被移出和采样。代码把 PA5 配成复用推挽输出，让 SPI1 外设控制它。

如果 PA5 配成普通 GPIO，SPI1 内部产生的时钟不会正确到达引脚。

### 6.4 `MOSI` 是什么

`MOSI` 是 Master Out Slave In，主机输出、从机输入。本课使用 `PA7`。

回环实验里没有外部从机，而是把 PA7 直接接到 PA6，因此 MOSI 输出的每一 bit 会被 MISO 读回。

### 6.5 `MISO` 是什么

`MISO` 是 Master In Slave Out，主机输入、从机输出。本课使用 `PA6`。

作为主机输入脚，PA6 配成浮空输入。若没有回环线，MISO 悬空，读回值可能随机，LED 很可能进入错误状态。

### 6.6 `回环` 是什么

回环就是把输出信号直接接回输入端。

本课把 `PA7(MOSI)` 接 `PA6(MISO)`，让 STM32 自己发给自己收。它属于实验验证方法，不是 SPI 协议要求。

回环成功说明 SPI 主模式、引脚复用、SCK 时序和收发寄存器基本工作。

### 6.7 `CPOL` 是什么

`CPOL` 是 Clock Polarity，时钟极性。

`CPOL=0` 表示 SCK 空闲时为低电平。它属于 SPI 时序层，决定没有传输时 SCK 停在哪个电平。

如果和外部从机要求不一致，从机可能在错误边沿采样。

### 6.8 `CPHA` 是什么

`CPHA` 是 Clock Phase，时钟相位。

`CPHA=0` 表示第一个时钟边沿采样数据。本课配合 `CPOL=0`，就是 Mode 0：空闲低，上升沿采样。

SPI 设备手册通常会写支持 Mode 0/1/2/3，配置前要查清楚。

### 6.9 `BR` 是什么

`BR` 是 SPI1 `CR1` 里的 Baud Rate Control，波特率分频字段。

本课 `BR=010`，表示 `PCLK2/8`。PCLK2 是 72MHz，所以 SCK 是 9MHz。

如果 SCK 太快，外部器件或连线可能跟不上；太慢则通信效率低。

### 6.10 `SSM / SSI` 是什么

`SSM` 是软件管理 NSS，`SSI` 是内部 NSS 电平。

本课不使用物理 `PA4(NSS)`，所以设置 `SSM=1`、`SSI=1`，告诉 SPI1 内部片选处于有效状态。

如果主模式下 NSS 管理错误，SPI 可能不产生正常时钟或进入模式错误状态。

### 6.11 `TXE` 是什么

`TXE` 是 Transmit buffer Empty，发送缓冲区空标志。

`TXE=1` 表示可以向 `SPI1->DR` 写入下一个字节。若 `TXE=0` 时强行写，可能覆盖或破坏正在发送的数据。

### 6.12 `RXNE` 是什么

`RXNE` 是 Receive buffer Not Empty，接收缓冲区非空标志。

SPI 每发送 1 字节就同时收到 1 字节。`RXNE=1` 表示收到的字节已经在 `DR` 中，可以读取。

### 6.13 `BSY` 是什么

`BSY` 是 Busy，总线忙标志。

它表示 SPI 仍在移位或总线还没完全空闲。读到 RXNE 后仍等待 `BSY=0`，可以避免下一次操作过早开始，尤其对片选控制很重要。

### 6.14 `HAL_SPI_TransmitReceive()` 是什么

这是 HAL 的同步收发 API。

它一次完成发送缓冲区和接收缓冲区的等长交换，最符合 SPI 全双工本质。本课发送 1 字节，也同步接收 1 字节。

它属于 HAL 工程层，但直接对应 SPI 的硬件本质：主机只要产生 SCK，MOSI 和 MISO 就同时移位。`HAL_SPI_Transmit()` 只关心发，`HAL_SPI_Receive()` 在主模式下也要靠发送 dummy 产生时钟；而 `HAL_SPI_TransmitReceive()` 把“边发边收”表达得最清楚。

本课长度参数是 1，所以 HAL 内部完成一次 8 位交换。后续和 Flash、屏幕、传感器通信时，长度可以变成多字节缓冲区，但仍要记住：接收缓冲区中的每个字节都对应发送期间同一组 SCK 时钟。

### 6.15 `全双工` 是什么

全双工表示发送和接收同时发生。SPI 的移位寄存器在每个 SCK 边沿一边把 MOSI 数据推出去，一边把 MISO 数据采进来。

这和 UART 的发送、接收可以相互独立不同。SPI 主机写 `DR` 后，即使你只关心发送，也会收到一个字节；即使你只想读取从机，也必须发送 dummy byte 提供时钟。忘记这个点，后面读 W25Q64 ID 时就会疑惑为什么要发 `0xFF`。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟和 SysTick

代码把系统时钟配置为 72MHz，PCLK2 也是 72MHz。SPI1 挂在 APB2，所以后续 `/8` 分频基于 72MHz。

SysTick 配成 1ms，用于 `delay_ms(1000)` 控制测试周期。

### 7.2 PC13 LED

PC13 配成推挽输出。回环成功时 LED 翻转；失败时 LED 点亮。它是现象层反馈，不参与 SPI 总线。

### 7.3 `spi1_gpio_init()` 打开时钟

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
```

GPIOA 用于 PA5/PA6/PA7，AFIO 支持复用功能，SPI1 是外设本体。三者对应引脚层、复用层、外设层。

### 7.4 配置 PA5 和 PA7

PA5/SCK 和 PA7/MOSI 都是 SPI 主机输出信号，因此配置为复用推挽输出：

- `MODE=11`：50MHz 输出能力
- `CNF=10`：复用推挽

这让引脚由 SPI1 外设驱动，而不是普通 GPIO 软件驱动。

### 7.5 配置 PA6

PA6/MISO 是主机输入，配置为浮空输入：

- `MODE6=00`
- `CNF6=01`

回环线把 PA7 输出接到 PA6，SPI1 接收器从 PA6 采样。

### 7.6 清理 `SPI1->CR1`

初始化前先清掉双向模式、CRC、16 位格式、只接收、低位优先、分频、主从、CPOL/CPHA 等位，避免旧配置残留。

这对应约束里的“先清位再设位”：不清旧位，最后硬件配置可能不是你以为的组合。

### 7.7 配置主模式、分频、软件 NSS

```c
SPI1->CR1 |= SPI_CR1_MSTR;
SPI1->CR1 |= SPI_CR1_BR_1;
SPI1->CR1 |= SPI_CR1_SSM;
SPI1->CR1 |= SPI_CR1_SSI;
```

结果是主模式、`PCLK2/8`、软件 NSS 且内部 NSS 有效。CPOL/CPHA 保持 0，所以是 Mode 0。

### 7.8 打开 SPI

```c
SPI1->CR1 |= SPI_CR1_SPE;
```

`SPE` 是 SPI 总使能。所有关键配置完成后再打开，避免初始化过程中产生异常时钟。

### 7.9 `spi1_transfer_byte()` 等待 TXE

```c
while ((SPI1->SR & SPI_SR_TXE) == 0U) {}
```

等待发送缓冲区空。此时可以写 `DR` 启动新一轮 8 bit 传输。

### 7.10 写 `DR` 启动传输

```c
*(__IO uint8_t *)&SPI1->DR = tx_byte;
```

写入低 8 位数据后，SPI1 自动输出 8 个 SCK 脉冲，同时在 MOSI/MISO 上移位。强转为 8 位访问，是为了按 8 位数据帧操作 `DR`。

### 7.11 等待 RXNE 并读 DR

```c
while ((SPI1->SR & SPI_SR_RXNE) == 0U) {}
rx_byte = *(__IO uint8_t *)&SPI1->DR;
```

8 个时钟结束后，收到的字节进入 `DR`，`RXNE` 置位。读 `DR` 取走数据，并清除接收非空状态。

### 7.12 等待 BSY 清零

```c
while ((SPI1->SR & SPI_SR_BSY) != 0U) {}
```

确保 SPI 总线完全空闲。后续涉及片选的外部设备时，通常要等 BSY 清零后才能拉高 CS。

本课回环没有片选线，等 `BSY` 看起来不是必须，但它是在培养正确习惯。真实从机通常用 CS 的上升沿结束事务，如果你在 `BSY=1` 时就拉高 CS，最后一个 bit 可能还在路上，从机会收到不完整命令。

`RXNE=1` 和 `BSY=0` 不是同一个阶段。`RXNE` 说明接收缓冲区有数据可读；`BSY` 说明 SPI 移位器和总线是否还忙。结束事务看 `BSY`，取数据看 `RXNE`。

### 7.13 主循环交替测试

代码交替发送 `0xA5` 和 `0x3C`。这两个 bit 图案不同，能更容易发现线没接、采样错或数据固定的问题。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和系统时钟

HAL 版先初始化 HAL Tick，再配置 72MHz 时钟。PCLK2 仍是 SPI1 分频基准。

### 8.2 HAL GPIO 配置

`GPIO_PIN_5 | GPIO_PIN_7` 使用 `GPIO_MODE_AF_PP`，对应寄存器版 PA5/PA7 复用推挽。`GPIO_PIN_6` 使用 `GPIO_MODE_INPUT`，对应 MISO 输入。

### 8.3 `SPI_HandleTypeDef hspi1`

`hspi1.Instance = SPI1` 绑定 SPI1 外设。`hspi1.Init` 字段描述主从、方向、数据位、模式、NSS、分频和位序。

### 8.4 `Mode`

`SPI_MODE_MASTER` 对应 `CR1.MSTR=1`。本课由 STM32 主动产生 SCK。

### 8.5 `Direction`

`SPI_DIRECTION_2LINES` 对应双线全双工，使用 MOSI 和 MISO。它对应寄存器版清除 `BIDIMODE/RXONLY`。

### 8.6 `CLKPolarity / CLKPhase`

`SPI_POLARITY_LOW` 和 `SPI_PHASE_1EDGE` 对应 `CPOL=0`、`CPHA=0`，也就是 Mode 0。

### 8.7 `NSS`

`SPI_NSS_SOFT` 对应软件 NSS 管理，底层会配置 `SSM/SSI`，避免本课使用 PA4 片选。

软件 NSS 不是说 SPI 没有片选概念，而是本课不让硬件 NSS 引脚参与主模式使能。后续接 W25Q64 时，PA4 会作为普通 GPIO 手动控制 CS；SPI 外设内部仍靠 `SSM/SSI` 保持主机状态有效。

### 8.7.1 HAL 状态和超时

HAL SPI API 会维护句柄状态，防止同一个 SPI 在忙碌时被重复调用。`HAL_SPI_TransmitReceive()` 返回非 `HAL_OK` 时，可能是超时、参数错误或外设状态异常。

本课源码检查返回值并进入 `error_handler()`，这是正确习惯。不要只比较 `rx_byte` 和 `tx_byte`，因为 API 自身失败时，接收缓冲区里的值可能没有意义。

### 8.8 `BaudRatePrescaler`

`SPI_BAUDRATEPRESCALER_8` 对应 `BR=010`，SCK 为 72MHz/8=9MHz。

### 8.9 `HAL_SPI_TransmitReceive()`

```c
HAL_SPI_TransmitReceive(&hspi1, &tx_byte, &rx_byte, 1U, HAL_MAX_DELAY)
```

它封装了等待 TXE、写 DR、等待 RXNE、读 DR、等待完成等流程。数据长度为 1，所以发送 1 字节、接收 1 字节。

## 9. 两个版本真正应该怎么学

寄存器版重点看：

```text
GPIO 复用 -> CR1 模式 -> TXE -> DR -> RXNE -> DR -> BSY
```

HAL 版重点看：

```text
SPI_HandleTypeDef.Init 字段 -> SPI1->CR1
HAL_SPI_TransmitReceive -> 同步收发一个或多个字节
```

SPI 的“读”通常也需要“写”来提供时钟，这个模型后面访问 W25Q64 时会立刻用到。

## 10. 检验问题清单

### 10.1 为什么 SPI 发送和接收同时发生？

**答**：SPI 主机每产生一个 SCK 时钟，MOSI 移出 1 bit，同时 MISO 移入 1 bit。8 个时钟后，发送和接收各完成 1 字节。

### 10.2 PA7 为什么要接 PA6？

**答**：这是回环验证。PA7 输出的 MOSI 数据直接送到 PA6 的 MISO 输入，收到值应等于发送值。

### 10.3 `CPOL=0, CPHA=0` 表示什么？

**答**：SCK 空闲低电平，第一个边沿采样数据，也就是 SPI Mode 0。

### 10.4 `TXE=1` 表示可以直接读数据吗？

**答**：不是。`TXE=1` 只表示发送缓冲区空，可以写入待发送数据。接收完成要看 `RXNE`。

### 10.5 为什么还要等 `BSY=0`？

**答**：`RXNE` 表示接收缓冲区有数据，但总线移位或收尾可能尚未完全结束。等 `BSY=0` 可以确认 SPI 空闲。

### 10.6 `HAL_SPI_TransmitReceive()` 为什么比单独 Transmit 更适合本课？

**答**：因为 SPI 本质是同步收发。回环测试既要发送也要检查收到的数据，TransmitReceive 正好对应这个模型。

### 10.7 如果没接 PA7 到 PA6，会怎样？

**答**：MISO 悬空，读回值可能随机或固定错误，LED 会点亮或不按预期翻转。

### 10.8 SCK 频率由什么决定？

**答**：由 SPI1 时钟源 PCLK2 和 `BR` 分频决定。本课 PCLK2=72MHz，BR=/8，所以 SCK=9MHz。

## 11. 工程实现步骤

### 11.1 需求分析

本课先验证 SPI1 最小收发链路，不接外部从机。回环能证明主机模式和引脚复用是否正确。

### 11.2 硬件核查

确认 PA7 和 PA6 已用杜邦线相连。若用逻辑分析仪，可同时观察 PA5 SCK、PA7 MOSI、PA6 MISO。

### 11.3 寄存器路线

打开 GPIOA、AFIO、SPI1 时钟，配置 PA5/PA7 复用推挽、PA6 输入，设置 SPI1 主模式、Mode 0、/8 分频、软件 NSS，最后轮询 TXE/RXNE/BSY。

### 11.4 HAL 路线

用 `GPIO_InitTypeDef` 配引脚，用 `SPI_HandleTypeDef` 配 SPI1，用 `HAL_SPI_TransmitReceive()` 完成同步收发。

### 11.5 工程思维

先做回环，再接外部器件。这样接 W25Q64 或 OLED 出问题时，可以区分是 SPI 基础配置错，还是器件协议/片选/命令错。

### 11.6 常见工程陷阱

MOSI/MISO 接反、PA5/PA7 没配复用推挽、NSS 管理没配好、CPOL/CPHA 与器件不匹配、忘记读 DR 清 RXNE，都会导致通信失败。

还有一个常见误区是只看 MOSI，不看 MISO。SPI 回环必须同时验证 PA7 输出和 PA6 输入；逻辑分析仪上 MOSI 有波形但 MISO 没回到 PA6，LED 仍会报错。

另一个陷阱是把 SPI 速度一开始就开太高。本课 9MHz 对短回环线通常没问题；接长线、面包板或外部模块时，可以先把分频降到 /16、/32，确认协议正确后再提速。

## 12. 运行现象

接好 PA7 到 PA6 后，LED 每秒翻转一次。拔掉回环线或改错引脚模式后，LED 会点亮表示回环失败。

逻辑分析仪上能看到 PA5 每次传输输出 8 个时钟脉冲，PA7 输出 `0xA5/0x3C` 交替位型。

## 13. 常见问题排查

### 13.1 LED 一直亮

先检查 PA7 是否真正接到 PA6，再查 GPIOA 和 SPI1 时钟是否打开、PA5/PA7 是否复用推挽。

### 13.2 逻辑分析仪看不到 SCK

检查 `SPE` 是否置位、`MSTR/SSM/SSI` 是否配置正确，以及代码是否真的写入 `DR`。

### 13.3 收到值总是固定错误

检查 MISO 是否悬空、PA6 是否配置输入、回环线是否接错到 PA5 或 PA4。

### 13.4 HAL 版卡在收发函数

检查 `HAL_SPI_Init()` 是否成功，`hspi1.Instance` 是否为 SPI1，GPIO 复用是否配置在 HAL 调用之前。

### 13.5 接外部器件后失败

回到本课先验证回环。回环正常后，再查外部器件的 CS、CPOL/CPHA、最高 SCK、命令格式和供电。

## 14. 本课最核心的结论

1. SPI 是同步全双工，发送和接收在同一组 SCK 时钟里同时完成。
2. SPI1 默认使用 PA5/SCK、PA6/MISO、PA7/MOSI。
3. SCK/MOSI 必须配成复用推挽，MISO 配成输入。
4. Mode 0 表示 `CPOL=0`、`CPHA=0`。
5. `TXE/RXNE/BSY` 分别对应可写、可读、总线忙三个阶段。
6. HAL 的 `TransmitReceive` 是对 SPI 同步收发模型的直接封装。

## 15. 建议你现在怎么读这节课

先用第 5 章脑图理解一次字节交换，再读第 6 章把 SCK/MOSI/MISO 和 `CR1/SR/DR` 对上。最后逐行读 `spi1_transfer_byte()`，确认每个等待标志对应哪个硬件阶段。

## 16. 扩展练习

1. 把分频改成 `/16`，用逻辑分析仪观察 SCK 变化。
2. 改成 Mode 3，观察空闲电平和采样边沿变化。
3. 连续发送 4 个字节，写一个 `spi1_transfer_buffer()`。
4. 在 HAL 版检查 `HAL_SPI_TransmitReceive()` 的返回值并加入错误闪烁。

## 17. 下一课预告

- 上一课：[29_i2c_mpu6050](../29_i2c_mpu6050/README.md)
- 下一课：[31_spi_w25q64](../31_spi_w25q64/README.md)
