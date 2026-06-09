# 第 26 课：I2C 基础

## 1. 本课到底在学什么

本课表面现象是：STM32 通过 I2C1 向 AT24C02 EEPROM 的 `0x00` 地址写入 1 个字节，再读回来比较；如果读写一致，PC13 LED 周期翻转。

真正要学的是 I2C 总线的一次完整事务：

```text
START
  -> 发送器件地址
  -> 从机 ACK
  -> 发送 EEPROM 内部地址
  -> 发送或读取数据
  -> STOP
```

读 EEPROM 时还会出现更重要的一步：

```text
先写内部地址 -> 重复起始 RESTART -> 再切到读方向 -> 读数据
```

这节课要把现象层、物理总线层、I2C1 外设层、寄存器 bit 层、C/CMSIS 层和 HAL 工程层全部连起来。否则你只会背 `HAL_I2C_Mem_Read()`，一旦遇到 `ADDR` 卡住、`AF` 置位、总线一直 `BUSY`，就不知道该查哪里。

## 2. 本课学习目标

学完本课，你应该能回答：

1. I2C 为什么只用 SCL/SDA 两根线？
2. 为什么 I2C 引脚必须配置成开漏，而不是推挽？
3. AT24C02 的 7 位地址 `0x50` 为什么发送时会变成 `0xA0` 或 `0xA1`？
4. `CR2.FREQ=36`、`CCR=180`、`TRISE=37` 分别控制什么？
5. `START`、`ADDR`、`TXE`、`BTF`、`RXNE` 这些标志分别表示哪一步完成？
6. 为什么清除 `ADDR` 必须先读 `SR1` 再读 `SR2`？
7. 为什么 EEPROM 写完后要等约 10ms 再读？
8. `HAL_I2C_Mem_Write()` 和寄存器版写字节流程如何对应？
9. `HAL_I2C_Mem_Read()` 为什么内部需要重复起始？

## 3. 本课目录结构

```text
26_i2c_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接操作 I2C1 寄存器，并把 START、ADDR、BTF、RXNE 等流程摊开。  
`hal/` 使用 `I2C_HandleTypeDef`、`HAL_I2C_Init()`、`HAL_I2C_Mem_Write()`、`HAL_I2C_Mem_Read()` 完成同一件事。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 外部模块：AT24C02 EEPROM 模块
- I2C 引脚：PB6 = I2C1_SCL，PB7 = I2C1_SDA
- LED：PC13，常见 BluePill 为低电平点亮

接线：

```text
AT24C02 VCC -> 3.3V
AT24C02 GND -> GND
AT24C02 SCL -> PB6
AT24C02 SDA -> PB7
```

很多 EEPROM 模块自带 4.7k 上拉电阻。如果你的模块没有上拉，SCL/SDA 必须外接上拉到 3.3V，否则总线高电平不能可靠形成。

## 5. 先建立一个最基本的脑图

```text
72MHz 系统时钟
  -> APB1 = 36MHz
  -> I2C1 时序计算基于 PCLK1

PB6/PB7 复用开漏
  -> 外部上拉把总线释放成高电平
  -> 任意设备只能主动拉低

I2C1 初始化
  -> CR2.FREQ = 36
  -> CCR = 180，得到 100kHz SCL
  -> TRISE = 37
  -> PE = 1 打开 I2C

写 EEPROM
  -> START -> 0xA0 -> 内部地址 -> 数据 -> STOP

读 EEPROM
  -> START -> 0xA0 -> 内部地址
  -> RESTART -> 0xA1 -> 读 1 字节 -> STOP

比较结果
  -> 一致则 PC13 翻转
  -> 失败则 PC13 点亮作为错误提示
```

本课最关键的因果关系是：I2C 电气层要求开漏和上拉；I2C 时序层要求正确 PCLK1、CCR、TRISE；I2C 协议层要求地址、ACK、重复起始按顺序出现。

## 6. 先认识本课里出现的核心名词

### 6.1 `I2C` 是什么

`I2C` 是一种同步串行总线，常用两根线连接多个器件。

- `SCL`：Serial Clock，时钟线，由主机产生节拍
- `SDA`：Serial Data，数据线，按 SCL 节拍传输数据

它属于板级通信层和芯片外设层之间的接口。STM32 的 I2C1 外设负责产生 START、STOP、ACK 和 SCL 时序，AT24C02 作为从机响应地址并读写内部存储单元。

如果你把 I2C 当成 UART，就会误以为两端各自按波特率收发。实际上 I2C 有主机时钟，所有位都跟着 SCL 走。

### 6.2 `开漏输出` 是什么

开漏输出表示器件只能主动拉低总线，不能主动推高总线；高电平依靠上拉电阻形成。

它属于物理/电气层，是 I2C 能多个设备共享总线的原因。多个器件同时接在 SDA 上时，只要任何一个器件拉低，总线就是低；没有器件拉低时，上拉电阻把总线拉高。

本课 PB6/PB7 必须配成复用开漏。如果错配成推挽，两个设备可能一个输出高、一个输出低，轻则通信异常，重则出现电气冲突。

### 6.3 `PB6 / PB7` 是什么

PB6 和 PB7 是 GPIOB 的引脚，也是 STM32F103 上 I2C1 的默认引脚。

- PB6：I2C1_SCL
- PB7：I2C1_SDA

它们属于引脚复用层。代码必须先打开 GPIOB 时钟，再在 `GPIOB->CRL` 中把它们配成复用开漏输出。

如果线接到 PB8/PB9，但代码没有做 I2C1 重映射，I2C1 默认信号不会出现在那些引脚上。

### 6.4 `AT24C02` 是什么

AT24C02 是一颗 I2C EEPROM，容量 2Kbit，也就是 256 字节。

它属于外部器件层。本课假设 A0/A1/A2 地址脚接地，因此 7 位 I2C 地址是 `0x50`。

EEPROM 的特点是掉电不丢数据，但写入需要内部编程时间。当前代码写完后等待 10ms，再读回校验。如果不等，读回可能还是旧值，或者设备在写周期中不响应 ACK。

### 6.5 `7 位地址 0x50` 是什么

I2C 常说的设备地址通常是 7 位地址。AT24C02 在本课中的 7 位地址是 `0x50`。

在线上传输地址时，地址字节最低位还要放 R/W 位：

```text
写：0x50 << 1 | 0 = 0xA0
读：0x50 << 1 | 1 = 0xA1
```

它属于协议层。寄存器版手动发送 `0xA0/0xA1`，HAL 版传入左移后的 `0xA0`，HAL 根据读写方向处理最低位。

地址错时，典型现象是 `ADDR` 不置位，`AF` 置位，LED 进入错误提示。

### 6.6 `START` 是什么

START 是 I2C 起始条件：SCL 为高时，SDA 从高变低。

它属于总线时序层。寄存器版设置 `I2C_CR1_START`，硬件自动在 PB6/PB7 上产生起始条件，完成后 `SR1.SB` 置位。

没有 START，从机不会把后面的地址当成一次新通信的开始。

### 6.7 `STOP` 是什么

STOP 是 I2C 停止条件：SCL 为高时，SDA 从低变高。

寄存器版设置 `I2C_CR1_STOP`，硬件产生 STOP 并释放总线。STOP 后 `SR2.BUSY` 最终应回到 0。

如果 STOP 没发出或总线被从机拉住，下一次通信前等待 `BUSY` 可能超时。

### 6.8 `ACK / NACK` 是什么

ACK 是应答，NACK 是不应答。I2C 每传完 8 bit 后，第 9 个时钟由接收方回应。

它属于协议层和硬件状态层之间的桥梁。主机发送地址后，如果从机存在且地址匹配，就拉低 SDA 给 ACK，STM32 硬件再置位 `ADDR`。

如果收到 NACK，寄存器版会看到 `SR1.AF`。常见原因是地址错、设备没供电、SCL/SDA 没上拉、接线错误。

### 6.9 `I2C1->CR1` 是什么

`CR1` 是 I2C 控制寄存器 1。

本课用它控制：

- `PE`：打开 I2C 外设
- `ACK`：接收时是否回应 ACK
- `START`：产生起始或重复起始
- `STOP`：产生停止条件

它属于寄存器/bit 层。写错这些 bit，会直接改变总线上的 START/STOP 和接收应答行为。

### 6.10 `I2C1->CR2` 是什么

`CR2` 在 I2C 中保存 APB1 时钟频率，单位 MHz。

本课 PCLK1 = 36MHz，所以：

```c
I2C1->CR2 |= 36U;
```

它属于时序配置层。I2C 硬件用这个值计算内部时序，尤其影响标准模式和快速模式的时间约束。填错会导致 SCL 时序不准。

### 6.11 `I2C1->CCR` 是什么

`CCR` 是 I2C Clock Control Register，时钟控制寄存器。

标准模式下：

```text
CCR = PCLK1 / (2 * Fscl)
    = 36MHz / (2 * 100kHz)
    = 180
```

它控制 SCL 频率。本课目标是 100kHz，所以寄存器版写 `I2C1->CCR = 180U`。

如果 CCR 太小，SCL 过快，EEPROM 可能响应不稳定；太大则通信变慢。

### 6.12 `I2C1->TRISE` 是什么

`TRISE` 是最大上升时间寄存器。

标准模式 100kHz 下，公式是：

```text
TRISE = PCLK1(MHz) + 1 = 36 + 1 = 37
```

它属于 I2C 时序层，用来约束 SCL 上升沿时间。I2C 的高电平由上拉电阻形成，上升沿不像推挽那样陡，所以需要这个配置。

### 6.13 `SR1 / SR2` 是什么

`SR1` 和 `SR2` 是 I2C 状态寄存器。

本课重点标志：

- `SB`：START 已发送
- `ADDR`：地址已发送且收到 ACK
- `TXE`：发送数据寄存器空
- `BTF`：字节传输完成
- `RXNE`：接收数据寄存器非空
- `BUSY`：总线忙
- `AF`：应答失败

`ADDR` 的清除方式很特殊：必须先读 `SR1`，再读 `SR2`。少一步或顺序错，硬件状态可能卡住。

### 6.14 `重复起始 RESTART` 是什么

重复起始是在不发送 STOP 的情况下再次发送 START。

读 EEPROM 的随机地址时，主机先以写方向告诉 EEPROM “我要读哪个内部地址”，再用重复起始切换到读方向读取数据。

它属于协议时序层。若中间发 STOP，有些设备会改变内部地址指针行为；使用重复起始是随机读的标准流程。

### 6.15 `HAL_I2C_Mem_Write()` 是什么

这是 HAL 用于“带内部地址设备”的写函数。

本课参数含义：

- `&hi2c1`：使用 I2C1
- `AT24C02_ADDR_HAL`：设备地址 `0xA0`
- `EEPROM_MEM_ADDR`：EEPROM 内部地址 `0x00`
- `I2C_MEMADD_SIZE_8BIT`：内部地址是 8 位
- `&tx_byte`：待写数据
- `1U`：写 1 字节

它封装寄存器版的 START、地址 ACK、内部地址、数据、STOP。

### 6.16 `HAL_I2C_Mem_Read()` 是什么

这是 HAL 用于“带内部地址设备”的读函数。

它封装了随机读流程：

```text
START -> 地址+写 -> 内部地址
  -> RESTART -> 地址+读 -> 接收数据 -> STOP
```

单字节读时，HAL 还会处理 ACK/NACK 和 STOP 的时序。寄存器版需要你手动在清 `ADDR` 前后安排 `ACK` 和 `STOP`。

## 7. 寄存器版代码逐步讲解

### 7.1 系统时钟：为什么 PCLK1 是 36MHz

`system_clock_72mhz_init()` 把 HSE 8MHz 经过 PLL x9 得到 SYSCLK 72MHz，同时设置：

```c
RCC_CFGR_PPRE1_DIV2
```

APB1 因此是 36MHz。I2C1 挂在 APB1 上，所以后面 `CR2=36`、`CCR=180`、`TRISE=37` 都依赖这个前提。

### 7.2 SysTick：为什么本课需要毫秒延时

寄存器版使用 SysTick 每 1ms 进入一次中断，`g_ms_ticks++`。

EEPROM 写入不是瞬时完成。代码写完一个字节后：

```c
delay_ms(10U);
```

这是等待 AT24C02 内部写周期完成。如果不等，马上读可能失败或读到旧数据。

### 7.3 LED 初始化：错误和成功的可见反馈

PC13 被配置成推挽输出。写读一致时 LED 翻转；失败时 LED 点亮。

这不是 I2C 的一部分，而是现象层反馈。没有它，你只能用逻辑分析仪或调试器判断结果。

### 7.4 `i2c1_gpio_init()`：打开 GPIOB、AFIO、I2C1

代码打开：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
```

GPIOB 用于 PB6/PB7，引脚复用需要 AFIO 支持，I2C1 本体在 APB1。三个层次缺一不可。

### 7.5 配置 PB6/PB7 为复用开漏

```c
GPIOB->CRL |= GPIO_CRL_MODE6 | GPIO_CRL_CNF6 |
              GPIO_CRL_MODE7 | GPIO_CRL_CNF7;
```

在 F103 GPIO 中，`MODE=11` 表示 50MHz 输出能力，`CNF=11` 表示复用开漏输出。

硬件后果是：PB6/PB7 被 I2C1 接管，而且只能主动拉低，总线释放时由上拉电阻拉高。

### 7.6 `i2c1_init()`：配置前先关闭 PE

```c
I2C1->CR1 &= ~I2C_CR1_PE;
```

关闭 I2C 后再配置 CR2、CCR、TRISE。很多外设配置寄存器要求先关闭外设，避免运行中修改时序。

### 7.7 配置 `CR2.FREQ`

```c
I2C1->CR2 &= ~I2C_CR2_FREQ;
I2C1->CR2 |= 36U;
```

`FREQ` 不是目标 I2C 频率，而是 APB1 时钟的 MHz 值。这里写 36，因为 PCLK1=36MHz。

### 7.8 配置 `OAR1`

```c
I2C1->OAR1 = I2C_OAR1_ADDMODE;
```

本课 STM32 只做主机，不靠自身地址被别人访问。但 F103 I2C 硬件要求 OAR1 的相关保留/模式位正确设置，否则初始化可能异常。

### 7.9 配置 `CCR` 和 `TRISE`

```c
I2C1->CCR = 180U;
I2C1->TRISE = 37U;
```

这两句把 I2C1 配成 100kHz 标准模式。`CCR` 决定 SCL 周期，`TRISE` 描述允许的上升时间。

### 7.10 打开 ACK 和 PE

```c
I2C1->CR1 |= I2C_CR1_ACK;
I2C1->CR1 |= I2C_CR1_PE;
```

`ACK` 让主机接收数据时默认应答，`PE` 打开 I2C 外设。单字节读最后一个字节时，代码会临时关闭 ACK。

### 7.11 `i2c1_send_start()`

设置 `CR1.START` 后等待 `SR1.SB`。这一步对应总线上 SCL 高电平期间 SDA 拉低。

如果 `SB` 不置位，说明 I2C1 没有成功产生起始条件，常查 PE、BUSY、引脚模式和总线电平。

### 7.12 `i2c1_send_address()`

代码把 `0xA0` 或 `0xA1` 写入 `DR`，然后等待：

- `ADDR`：从机 ACK，地址确认成功
- `AF`：应答失败

这一步把协议地址和硬件状态联系起来。AT24C02 不在线时，通常等不到 `ADDR`。

### 7.13 `i2c1_clear_addr_flag()`

```c
temp = I2C1->SR1;
temp = I2C1->SR2;
```

这是 F103 I2C 的规定动作。`ADDR` 不是写 0 清除，而是读 `SR1` 后读 `SR2` 清除。

### 7.14 `at24c02_write_byte()`

写流程是：

```text
等 BUSY=0
START
地址+写 0xA0
清 ADDR
等 TXE
写内部地址
等 BTF
写数据
等 BTF
STOP
```

`TXE` 表示可以写下一个字节，`BTF` 表示字节已经传输完成。写 EEPROM 时等 `BTF` 能保证 STOP 不会过早出现。

### 7.15 `at24c02_read_byte()`

随机读流程分两段：

第一段告诉 EEPROM 要读哪个内部地址：

```text
START -> 0xA0 -> mem_addr
```

第二段用重复起始切换到读：

```text
RESTART -> 0xA1 -> 关 ACK -> 清 ADDR -> STOP -> 等 RXNE -> 读 DR
```

单字节读时必须用 NACK 告诉从机“这就是最后一个字节”。ACK/STOP 时序错，读操作容易卡住或多读。

### 7.16 主循环：写、等、读、比较

主循环在 `0xA5` 和 `0x3C` 之间切换写入值。这样能避免你一直读到 EEPROM 上电旧值却误以为写入成功。

读回一致则翻转 LED；失败则点亮 LED。这是本课现象层的最终判断。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 和系统时钟

HAL 版先调用 `HAL_Init()`，再用 `HAL_RCC_OscConfig()` 和 `HAL_RCC_ClockConfig()` 配置 72MHz。

这些结构体字段对应寄存器版的 HSE、PLL、APB1 二分频和 Flash latency。PCLK1 仍是 36MHz，这是 `HAL_I2C_Init()` 计算 I2C 时序的基础。

### 8.2 `i2c1_gpio_init()`

```c
gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
gpio.Mode = GPIO_MODE_AF_OD;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOB, &gpio);
```

`GPIO_MODE_AF_OD` 对应寄存器版 `CNF=11` 的复用开漏。`GPIO_SPEED_FREQ_HIGH` 对应输出速度能力，不是 I2C 的 SCL 频率。

### 8.3 `I2C_HandleTypeDef hi2c1`

`hi2c1` 是 HAL 管理 I2C1 的句柄。`hi2c1.Instance = I2C1` 绑定具体外设，`hi2c1.Init` 保存配置参数。

HAL API 通过这个句柄知道要操作哪个 I2C 外设、当前处于什么状态、超时和错误码如何记录。

### 8.4 `ClockSpeed`

```c
hi2c1.Init.ClockSpeed = 100000U;
```

目标是 100kHz 标准模式。HAL 会根据 PCLK1 计算 CCR，等价于寄存器版的 `CCR=180`。

### 8.5 `DutyCycle`

```c
hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
```

标准模式下占空比按普通模式处理。快速模式 400kHz 才更常讨论 2 或 16/9 的占空比差异。

### 8.6 `AddressingMode`

```c
hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
```

AT24C02 使用 7 位地址。本课不使用 10 位地址。

### 8.7 `OwnAddress1`

```c
hi2c1.Init.OwnAddress1 = 0U;
```

STM32 本课只做主机，不作为从机被别人寻址，所以自身地址不重要。HAL 初始化时仍会写 I2C 相关地址寄存器。

### 8.8 `NoStretchMode`

```c
hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
```

表示允许时钟拉伸。I2C 从机在需要更多处理时间时可以拉住 SCL。虽然 AT24C02 常见操作不一定明显用到，但工程上通常保持允许。

### 8.9 `HAL_I2C_Init()`

它封装寄存器版 `i2c1_init()` 的核心动作：

- 写 `CR2.FREQ`
- 计算并写 `CCR`
- 写 `TRISE`
- 配置地址模式
- 设置 ACK
- 设置 PE

### 8.10 `HAL_I2C_Mem_Write()`

HAL 写 EEPROM 的调用把“设备地址、内部地址、数据缓冲区”作为参数传入。内部完成 START、地址、ACK 检查、内部地址、数据和 STOP。

如果返回不是 `HAL_OK`，说明底层某一步超时、NACK 或错误，代码点亮 LED。

### 8.11 `HAL_I2C_Mem_Read()`

HAL 读 EEPROM 时，内部先发送内部地址，再重复起始切换到读方向。它对应寄存器版 `at24c02_read_byte()`。

单字节读的 ACK/NACK 和 STOP 时序由 HAL 处理，所以 HAL 版看起来短很多，但底层流程没有消失。

### 8.12 `HAL_Delay(10U)`

写 EEPROM 后等待 10ms，对应 AT24C02 内部写周期。这个延时不是 STM32 I2C 外设要求，而是 EEPROM 器件特性要求。

## 9. 两个版本真正应该怎么学

寄存器版重点看每个状态标志：

```text
SB -> ADDR -> TXE/BTF -> STOP
ADDR -> 清 SR1/SR2
RXNE -> 读 DR
AF -> 地址或数据无 ACK
```

HAL 版重点看 API 如何压缩流程：

```text
HAL_I2C_Init      -> CR2/CCR/TRISE/CR1
HAL_I2C_Mem_Write -> 写 EEPROM 完整事务
HAL_I2C_Mem_Read  -> 随机读完整事务
```

你不能只会 HAL 调用。I2C 是很容易卡状态的外设，理解寄存器标志能让你知道失败到底发生在地址、ACK、数据还是总线释放阶段。

## 10. 检验问题清单

### 10.1 为什么 I2C 要开漏输出？

**答**：因为 I2C 多个设备共享 SCL/SDA，总线高电平由上拉提供，任何设备都只能主动拉低。开漏可以避免多个设备输出相反电平造成冲突。

### 10.2 `0x50`、`0xA0`、`0xA1` 分别是什么？

**答**：`0x50` 是 AT24C02 的 7 位地址；`0xA0` 是左移后加写位的地址字节；`0xA1` 是左移后加读位的地址字节。

### 10.3 `CR2.FREQ=36` 表示 I2C 频率是 36MHz 吗？

**答**：不是。它表示 I2C1 所在 APB1 时钟是 36MHz，供 I2C 硬件计算时序。真正的 SCL 目标频率由 `CCR` 决定。

### 10.4 为什么 `CCR=180`？

**答**：标准模式下 `CCR=PCLK1/(2*Fscl)`，本课 PCLK1=36MHz，目标 SCL=100kHz，所以 `CCR=180`。

### 10.5 清 `ADDR` 为什么要读 `SR1` 再读 `SR2`？

**答**：这是 STM32F103 I2C 硬件规定的清除序列。`ADDR` 表示地址阶段完成，必须按顺序读取两个状态寄存器才能让硬件进入后续数据阶段。

### 10.6 EEPROM 写完为什么要等 10ms？

**答**：AT24C02 写入 EEPROM 单元需要内部编程时间。I2C 总线传输完成只代表数据送到了器件，不代表非易失存储已经写完。

### 10.7 `HAL_I2C_Mem_Read()` 为什么比普通接收复杂？

**答**：因为 EEPROM 读指定地址前，要先写入内部地址，再重复起始切换到读方向。它不是单纯从总线上直接收一个字节。

### 10.8 `AF` 置位一般说明什么？

**答**：说明应答失败，地址或数据发送后没有收到 ACK。常见原因是地址错、器件没供电、接线错、没上拉或 EEPROM 正在写周期中忙。

## 11. 工程实现步骤

### 11.1 需求分析

本课目标是验证 STM32 能可靠访问一个 I2C EEPROM。为了让结果可见，代码不只写，还读回比较，并用 PC13 表示成败。

### 11.2 硬件核查

确认 AT24C02 供电为 3.3V，SCL 接 PB6，SDA 接 PB7，GND 共地。检查模块是否自带上拉，没有则补 4.7k 左右上拉到 3.3V。

### 11.3 寄存器路线

先配置时钟和 PB6/PB7，再设置 I2C1 的 CR2、OAR1、CCR、TRISE、ACK、PE。之后按写事务和读事务逐步等待状态标志。

### 11.4 HAL 路线

先配置 RCC 和 GPIO，再填写 `hi2c1.Init`，调用 `HAL_I2C_Init()`。读写 EEPROM 使用 `HAL_I2C_Mem_Write()` 和 `HAL_I2C_Mem_Read()`。

### 11.5 工程思维

I2C 排错不要只看“函数返回失败”。要把失败定位到总线电平、地址 ACK、内部地址、数据阶段、STOP 释放哪一步。寄存器版的状态标志就是定位工具。

### 11.6 常见工程陷阱

最常见的是忘记上拉、地址左移规则搞错、PB6/PB7 接反、写完 EEPROM 立刻读、清 `ADDR` 顺序错误。每个错误的现象都可能是“卡住”，但卡住的位置不同。

## 12. 运行现象

正常情况下，程序会循环写入 `0xA5` 和 `0x3C`，每次写后读回比较。比较成功时 PC13 LED 翻转；如果写或读失败，LED 会被点亮作为错误提示。

如果 EEPROM 没接或地址错误，LED 通常会进入错误状态，寄存器版可能在等待某个标志时超时并返回失败。

## 13. 常见问题排查

### 13.1 LED 一直亮

说明写或读失败。先查 AT24C02 供电、GND、PB6/PB7 接线和上拉电阻，再查地址是否为 `0x50`。

### 13.2 程序卡在等待 `ADDR`

地址阶段没有收到 ACK。常见原因是地址字节错、设备不在线、SCL/SDA 没有上拉、PB6/PB7 接反。

### 13.3 总线一直 `BUSY`

可能是上一次通信没有 STOP，或者某个设备把 SDA 拉低。断电重启外设模块、检查上拉和接线，用示波器/逻辑分析仪看 SDA 是否能回到高电平。

### 13.4 读回总是旧值

通常是写后没有等待 EEPROM 内部写周期，或者写操作其实没有成功。确认 `delay_ms(10)` 或 `HAL_Delay(10)` 保留。

### 13.5 HAL 返回 `HAL_ERROR` 或超时

HAL 只是告诉你某一步失败。回到寄存器版思路拆开：先看设备是否 ACK，再看内部地址和数据阶段，再看 STOP 后总线是否释放。

## 14. 本课最核心的结论

1. I2C 的高电平来自上拉电阻，所以 PB6/PB7 必须使用开漏方式。
2. I2C1 挂在 APB1，本课 PCLK1=36MHz，`CR2/CCR/TRISE` 都围绕它计算。
3. AT24C02 的 7 位地址 `0x50` 在线上传输时要左移并加入 R/W 位。
4. `ADDR`、`TXE`、`BTF`、`RXNE` 是理解 I2C 硬件流程的关键状态。
5. 随机读 EEPROM 必须先写内部地址，再用重复起始切到读方向。
6. HAL I2C API 很方便，但底层仍然执行 START、地址、ACK、数据和 STOP 这套流程。

## 15. 建议你现在怎么读这节课

先把第 5 章脑图画出来，再读第 6 章名词。然后对照寄存器版源码，把每个等待标志对应到总线上的一步。最后读 HAL 版，写出 `HAL_I2C_Mem_Write()` 和 `HAL_I2C_Mem_Read()` 内部大概做了哪些寄存器动作。

## 16. 扩展练习

1. 把写入地址从 `0x00` 改成 `0x10`，再读回校验。
2. 连续写入多个地址，观察是否需要页写边界处理。
3. 用逻辑分析仪抓取 `0xA0`、内部地址、数据和 `0xA1`。
4. 故意去掉上拉或改错地址，观察失败发生在 `ADDR` 还是 `BUSY`。

## 17. 下一课预告

- 上一课：[25_uart_packet_protocol](../25_uart_packet_protocol/README.md)
- 下一课：[27_i2c_software_eeprom](../27_i2c_software_eeprom/README.md)
