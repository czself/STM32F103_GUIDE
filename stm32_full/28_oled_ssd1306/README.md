# 第 28 课：OLED SSD1306 显示屏

## 1. 本课到底在学什么

本课表面现象是：STM32 通过 I2C1 初始化 SSD1306 OLED，并在第 0 页写入 `0x55/0xAA` 交替测试图案，屏幕上出现一条横向纹理，PC13 LED 周期翻转。

真正要学的是：同样是 I2C 写字节，外部器件不同，字节含义完全不同。AT24C02 里 `0x00` 是 EEPROM 内部地址；SSD1306 里 `0x00` 可以是 control byte，表示后面跟的是命令。

本课链路是：

```text
PB6/PB7 I2C1
  -> 发送 OLED 地址 0x78
  -> 发送 control byte
  -> control=0x00 时写命令
  -> control=0x40 时写显存数据
  -> 初始化显示控制器
  -> 选择 page 和 column
  -> 写入 128 字节图案
  -> OLED 像素点亮
```

你要把 I2C 总线传输和 SSD1306 内部命令系统分开理解：I2C 负责把字节送到器件，SSD1306 决定这些字节是命令还是显示数据。

## 2. 本课学习目标

学完本课，你应该能回答：

1. SSD1306 和上一课 AT24C02 在 I2C 访问模型上有什么区别？
2. OLED 地址 `0x78` 和常见 7 位地址 `0x3C` 是什么关系？
3. `control byte` 为什么有 `0x00` 和 `0x40` 两种？
4. `oled_cmd()` 和 `oled_data()` 的底层 I2C 流程哪里相同、哪里不同？
5. `page` 为什么一页对应垂直 8 个像素？
6. `0xB0`、`0x00`、`0x10` 这几个命令为什么能选择写入位置？
7. `0x55` 和 `0xAA` 写到显存后为什么会形成条纹？
8. `HAL_I2C_Master_Transmit()` 对应寄存器版哪些动作？

## 3. 本课目录结构

```text
28_oled_ssd1306/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

`reg/` 直接操作 I2C1 寄存器发送 SSD1306 命令和数据。  
`hal/` 使用 `HAL_I2C_Master_Transmit()` 发送两字节消息。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill
- 下载器：ST-Link
- 显示模块：0.96 寸 I2C OLED，常见控制器 SSD1306，分辨率 128x64
- I2C 引脚：PB6 = SCL，PB7 = SDA
- LED：PC13

接线：

```text
OLED VCC -> 3.3V
OLED GND -> GND
OLED SCL -> PB6
OLED SDA -> PB7
```

常见 I2C OLED 模块 7 位地址为 `0x3C`，左移后写地址字节为 `0x78`。如果你的模块地址是 `0x3D`，左移后应改为 `0x7A`。

## 5. 先建立一个最基本的脑图

```text
72MHz 系统时钟
  -> APB1=36MHz
  -> I2C1=100kHz

PB6/PB7 复用开漏
  -> 连接 OLED SCL/SDA

oled_init()
  -> 发送一串 SSD1306 初始化命令
  -> 打开显示、设置寻址模式、扫描方向、对比度、电荷泵

写显示位置
  -> oled_cmd(0xB0) 选择 page 0
  -> oled_cmd(0x00) 设置列低 4 位
  -> oled_cmd(0x10) 设置列高 4 位

写显示数据
  -> oled_data(0x55 / 0xAA)
  -> SSD1306 把字节写进 GDDRAM
  -> OLED 点阵显示测试条纹
```

这节课的重点不是画复杂 UI，而是第一次理解“显示屏控制器也有自己的寄存器和显存模型”。

## 6. 先认识本课里出现的核心名词

### 6.1 `SSD1306` 是什么

SSD1306 是常见小尺寸 OLED 模块里的显示控制器。

它属于外部器件层，不在 STM32 芯片内部。STM32 通过 I2C 把命令和数据发给它，SSD1306 再控制 OLED 面板的像素点亮。

如果 OLED 模块不是 SSD1306，初始化命令可能不同，屏幕可能无显示或显示异常。

### 6.2 `OLED_ADDR 0x78` 是什么

代码里：

```c
#define OLED_ADDR 0x78U
```

这是 SSD1306 常见 7 位地址 `0x3C` 左移一位后的 8 位地址格式：

```text
0x3C << 1 = 0x78
```

寄存器版直接把它写入 `DR` 作为地址字节；HAL 版 `HAL_I2C_Master_Transmit()` 也要求传入左移后的地址。

地址错时，寄存器版会卡在等待 `ADDR`，HAL 版可能超时返回错误，屏幕不会显示。

### 6.3 `control byte` 是什么

SSD1306 I2C 通信中，地址之后的第一个数据字节常用作 control byte，用来说明后面的字节类型。

本课用：

- `0x00`：后面是命令
- `0x40`：后面是显示数据

它属于 SSD1306 协议层，不是 STM32 I2C 外设寄存器。如果命令和数据的 control byte 搞反，初始化命令会被当成像素数据，或像素数据被当成命令，屏幕表现会乱。

### 6.4 `oled_cmd()` 是什么

`oled_cmd(c)` 调用：

```c
oled_write(0x00U, c);
```

它表示向 SSD1306 发送一个命令字节。命令会改变控制器状态，比如显示开关、寻址模式、扫描方向、页地址和列地址。

它属于 C 封装层，对应底层 I2C 事务：地址 `0x78`、control `0x00`、命令值。

### 6.5 `oled_data()` 是什么

`oled_data(d)` 调用：

```c
oled_write(0x40U, d);
```

它表示向 SSD1306 显存写一个数据字节。这个字节会落到当前 page/column 指向的位置。

它和 `oled_cmd()` 的 I2C 发送方式一样，区别只在 control byte。

### 6.6 `page` 是什么

SSD1306 的 128x64 显存常按 page 组织。每个 page 覆盖垂直方向 8 个像素，64 像素高度共有 8 页。

一个数据字节的 8 个 bit 对应当前列上的 8 个垂直像素。写 `0x55` 和 `0xAA` 会让相邻 bit 交替亮灭，所以能看到测试纹理。

### 6.7 `column` 是什么

column 是横向列地址，范围通常是 0 到 127。

本课设置：

```c
oled_cmd(0x00);
oled_cmd(0x10);
```

这两条分别设置列地址低 4 位和高 4 位，让后续数据从第 0 列开始写。

### 6.8 `GDDRAM` 是什么

GDDRAM 可以理解为 SSD1306 内部显存。

STM32 写入的显示数据先进入 GDDRAM，SSD1306 再根据扫描方式把显存内容显示到 OLED 面板。

它属于外部器件内部存储层。屏幕显示不只是 I2C 线有波形，还要求控制器已经初始化、地址指针正确、数据写入 GDDRAM。

GDDRAM 和 STM32 的 SRAM 没有直接关系。STM32 只能通过 I2C 命令和数据间接改变它，不能像访问 `GPIOC->ODR` 那样直接读写一个内存地址。`oled_data(0x55)` 的含义是“把一个显示字节送给 SSD1306”，至于它落到 GDDRAM 哪个位置，取决于 SSD1306 当前 page/column 指针。

这也是为什么显示驱动要分层：底层 I2C 只保证字节送达，中间层命令设置地址指针，上层绘图才谈画点、画线、显示字符。如果 page/column 没设对，I2C 完全正常也可能显示在错误位置。

### 6.9 `0xAE / 0xAF` 是什么

这两个是 SSD1306 显示开关命令：

- `0xAE`：Display OFF
- `0xAF`：Display ON

初始化序列通常先关闭显示，配置参数，再打开显示。若没有最后的 `0xAF`，屏幕可能保持黑屏。

### 6.10 `0x20` 是什么

`0x20` 是设置内存寻址模式的命令，后面跟一个参数。本课序列中 `0x20, 0x02` 表示使用页寻址模式。

页寻址模式下，需要先设置 page 和 column，再连续写数据。

### 6.11 `I2C1` 是什么

I2C1 是 STM32 内部 I2C 外设。本课仍使用 PB6/PB7 复用开漏，100kHz 标准模式。

它负责产生 START、发送地址、等待 ACK、发送 control byte 和 payload、产生 STOP。它不理解 SSD1306 命令含义。

### 6.12 `HAL_I2C_Master_Transmit()` 是什么

HAL 版用它向 OLED 发送两个字节：

```c
uint8_t buf[2] = {control, value};
HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 100);
```

它封装一次 I2C 主机发送事务：START、地址、发送缓冲区、STOP。SSD1306 的命令/数据语义由 `buf[0]` 的 control byte 决定。

### 6.13 `0x8D / 0x14` 是什么

初始化序列里的 `0x8D, 0x14` 是 SSD1306 电荷泵相关命令和参数。许多 0.96 寸 OLED 模块需要打开内部电荷泵，面板才有足够驱动电压显示。

它属于 SSD1306 控制器配置层，不是 I2C 外设配置。若 I2C 通信正常、地址 ACK 正常、但屏幕仍黑，初始化序列中的显示开关、电荷泵、扫描方向和对比度都要检查。

## 7. 寄存器版代码逐步讲解

### 7.1 时钟和 LED

系统时钟仍是 72MHz，APB1 是 36MHz。PC13 用作运行指示，OLED 写完测试图案后主循环周期翻转 LED。

### 7.2 `i2c1_init()` 打开 GPIOB 和 I2C1

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
```

GPIOB 让 PB6/PB7 可配置，I2C1 时钟让外设寄存器工作。

### 7.3 PB6/PB7 复用开漏

```c
GPIOB->CRL |= GPIO_CRL_MODE6 | GPIO_CRL_CNF6 |
              GPIO_CRL_MODE7 | GPIO_CRL_CNF7;
```

这把 PB6/PB7 配成 50MHz 复用开漏输出。硬件后果是 I2C1 接管这两根线，并符合 I2C 开漏电气特性。

### 7.4 I2C 软件复位和时序配置

```c
I2C1->CR1 = I2C_CR1_SWRST;
I2C1->CR1 = 0;
I2C1->CR2 = 36;
I2C1->CCR = 180;
I2C1->TRISE = 37;
I2C1->CR1 = I2C_CR1_PE;
```

`SWRST` 让 I2C1 回到干净状态。`CR2/CCR/TRISE` 设置 36MHz PCLK1 下的 100kHz I2C。最后 `PE` 打开外设。

### 7.5 `i2c1_start_addr()`

函数先设置 `START` 并等 `SB`，再写地址并等 `ADDR`，最后读 `SR1/SR2` 清除地址标志。

如果 OLED 地址错或没接，通常卡在等待 `ADDR`。

### 7.6 `i2c1_write()`

```c
while ((I2C1->SR1 & I2C_SR1_TXE) == 0U) {}
I2C1->DR = b;
```

等待发送数据寄存器为空，再写入一个字节。这个字节可能是 control byte，也可能是 SSD1306 命令或显示数据。

### 7.7 `oled_write()`

```c
i2c1_start_addr(OLED_ADDR);
i2c1_write(control);
i2c1_write(value);
while ((I2C1->SR1 & I2C_SR1_BTF) == 0U) {}
I2C1->CR1 |= I2C_CR1_STOP;
```

每次发送两个数据字节：control 和 value。等 `BTF` 表示传输完成，再发送 STOP。

这里每写一个命令或一个数据字节，就产生一次 I2C 事务。流程简单、容易教学，但刷新效率不高。真实 OLED 驱动通常会一次发送 control byte 后连续发送多个数据字节，减少 START/STOP 开销。

`BTF` 表示 Byte Transfer Finished，说明当前字节传输完成且数据移位完成。等待它再发 STOP，可以避免最后一个字节还没真正发完就结束事务。若过早 STOP，OLED 可能收不完整 control/value。

### 7.8 `oled_cmd()` 与 `oled_data()`

`oled_cmd()` 使用 control `0x00`，`oled_data()` 使用 control `0x40`。这两个函数是理解 SSD1306 的分界线：同样走 I2C，语义由 control byte 改变。

### 7.9 `oled_init()` 初始化序列

初始化数组包含显示关闭、寻址模式、扫描方向、对比度、多路复用比、显示偏移、时钟分频、预充电、电荷泵、显示打开等命令。

这些值不是 STM32 寄存器，而是写给 SSD1306 控制器的命令。

### 7.10 设置 page 和 column

```c
oled_cmd(0xB0);
oled_cmd(0x00);
oled_cmd(0x10);
```

`0xB0` 选择 page 0。`0x00/0x10` 把列地址设为 0。后续 128 个数据字节就从 page 0 的第 0 列开始写。

### 7.11 写 128 字节测试图案

```c
for (uint8_t i = 0; i < 128; ++i)
    oled_data((i & 1U) ? 0xAAU : 0x55U);
```

每一列写一个字节，奇偶列交替。`0x55` 和 `0xAA` 的 bit 交错，所以显示成规则纹理。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 时钟和 GPIO

HAL 版同样配置 72MHz 时钟、PC13 输出、PB6/PB7 复用开漏。这部分对应寄存器版 RCC 和 GPIO 配置。

### 8.2 `I2C_HandleTypeDef hi2c1`

`hi2c1.Instance = I2C1` 绑定 I2C1，`ClockSpeed=100000` 设置 100kHz，其他字段配置 7 位地址、普通主机模式和允许时钟拉伸。

### 8.3 `HAL_I2C_Init()`

该函数根据 `hi2c1.Init` 写 I2C1 的 CR2、CCR、TRISE、CR1 等寄存器。它对应寄存器版 `i2c1_init()`。

### 8.4 `oled_write()`

```c
uint8_t buf[2] = {control, value};
HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 100);
```

HAL 将两个字节作为连续 payload 发送给 OLED。底层仍然是 START、地址 ACK、写两个字节、STOP。

### 8.5 `oled_cmd()` 和 `oled_data()`

HAL 版的命令/数据区分仍由 control byte 完成。`HAL_I2C_Master_Transmit()` 不知道 `0x00` 和 `0x40` 的 SSD1306 含义。

### 8.6 `HAL_Delay(50)`

上电后等待 OLED 模块稳定，再发送初始化命令。若上电立即配置，某些模块可能还没准备好响应。

### 8.7 初始化数组

HAL 版初始化数组和寄存器版一致。你可以逐个命令对照，确认两份代码写给 SSD1306 的内容相同。

### 8.8 写图案循环

HAL 版同样先设置 page/column，再写 128 字节 `0x55/0xAA`。显示结果应和寄存器版一致。

不过 HAL 版当前 `oled_write()` 每次只发送两个字节 `{control, value}`，所以 128 列数据会调用 128 次 `HAL_I2C_Master_Transmit()`。这对测试图案没问题，但对整屏刷新会比较慢。

后续做字体库时，建议把一页的 128 个数据放进缓冲区，一次发送 `0x40` 加 128 字节数据。这样 I2C 总线效率更高，也更接近真实 OLED 驱动写法。

### 8.9 HAL 返回值

当前 HAL 源码没有检查 `HAL_I2C_Master_Transmit()` 返回值。教学时要知道这个 API 可能返回 `HAL_OK`、`HAL_ERROR`、`HAL_BUSY` 或 `HAL_TIMEOUT`。

如果 OLED 地址错、SCL/SDA 接错、没有 ACK，HAL 通常不会让屏幕显示，但如果你忽略返回值，主循环仍然会翻转 LED，看起来像“程序正常但屏不亮”。工程上应让 `oled_write()` 返回状态，或在失败时点亮错误 LED、断点查看 `hi2c1.ErrorCode`。

### 8.10 OLED 初始化延时

HAL 版在 `i2c1_init()` 后调用 `HAL_Delay(50)`，寄存器版也用 `delay_cycles()` 等待一段时间。这段等待是给 OLED 模块上电稳定、电荷泵准备和控制器复位留时间。

有些 OLED 模块上电较慢，如果刚上电就发送初始化命令，可能地址阶段无 ACK，或者命令被忽略。黑屏但偶尔复位后能显示时，要把上电延时列入排查项。

## 9. 两个版本真正应该怎么学

寄存器版重点看 I2C1 如何发送地址和字节；HAL 版重点看 `HAL_I2C_Master_Transmit()` 如何把一段缓冲区发出去。

但本课真正的新东西在 SSD1306 协议：control byte、命令、显存 page、column。I2C 只是运输层，显示效果由 OLED 控制器解释这些字节后产生。

## 10. 检验问题清单

### 10.1 `0x78` 是 OLED 的 7 位地址吗？

**答**：不是。常见 SSD1306 7 位地址是 `0x3C`，左移一位后是 `0x78`。代码和 HAL API 使用的是左移后的格式。

### 10.2 `control byte=0x00` 表示什么？

**答**：表示后面的字节是 SSD1306 命令，会改变控制器配置或地址指针。

### 10.3 `control byte=0x40` 表示什么？

**答**：表示后面的字节是显示数据，会写入 SSD1306 的 GDDRAM。

### 10.4 为什么要先 `oled_init()`？

**答**：上电后的 SSD1306 需要配置显示开关、寻址模式、扫描方向、电荷泵等参数。没有初始化，显存写入也可能看不到正确显示。

### 10.5 为什么 page 一页是 8 像素高？

**答**：SSD1306 的一个显示数据字节有 8 个 bit，通常对应同一列的 8 个垂直像素，所以 64 像素高度被分成 8 页。

### 10.6 `0x55/0xAA` 为什么适合做测试图案？

**答**：它们的二进制位交替为 01010101 和 10101010，写入显存后容易在屏幕上形成可见纹理，便于判断数据写入是否生效。

### 10.7 HAL 版为什么仍要理解 control byte？

**答**：HAL 只负责发 I2C 数据，不理解 SSD1306 协议。命令和数据的区分仍由你放进缓冲区的 control byte 决定。

### 10.8 OLED 黑屏时先查 I2C 还是先查显示命令？

**答**：先查供电、地址、SCL/SDA、ACK 等 I2C 基础链路；确认 I2C 有响应后，再查初始化命令、control byte、page/column 和数据。

## 11. 工程实现步骤

### 11.1 需求分析

本课目标是让 OLED 出现最简单的可见图案，不做字体库和复杂绘图。这样可以先验证 I2C 通信和 SSD1306 初始化。

### 11.2 硬件核查

确认 OLED 供电、GND、PB6/PB7 接线和地址。若模块有地址焊盘，确认它是 `0x3C` 还是 `0x3D`。

### 11.3 寄存器路线

配置 I2C1 为 100kHz，写 `oled_write()` 发送 control/value 两字节，再用 `oled_cmd()` 和 `oled_data()` 区分命令与数据。

### 11.4 HAL 路线

配置 `hi2c1`，用 `HAL_I2C_Master_Transmit()` 发送 `{control, value}` 缓冲区。其余 SSD1306 命令序列与寄存器版一致。

### 11.5 工程思维

显示驱动要分层：总线发送层、命令封装层、显存绘图层。当前代码只做到总线发送和最小命令封装，后续字体显示应建立在这两层之上。

总线发送层只负责把字节可靠送到 OLED；命令封装层负责 `oled_cmd()`、`oled_data()`、设置 page/column；显存绘图层才负责字符、图片、坐标和缓冲区。不要把字体点阵直接散落在 I2C 发送函数里，否则后续维护会很痛苦。

本课只写一页测试图案，是为了把“OLED 控制器能被初始化”和“GDDRAM 能被写入”先验证清楚。确认这两点后，再做字体、清屏、局部刷新，问题会少很多。

### 11.6 常见工程陷阱

地址格式、control byte、初始化延时、page/column 设置最容易出错。不要把 OLED 黑屏直接归因于屏坏，先用逻辑分析仪确认是否有地址 ACK。

## 12. 运行现象

OLED 上应出现第 0 页的测试纹理，也就是屏幕顶部 8 像素高度附近出现 `0x55/0xAA` 交替图案。PC13 LED 周期翻转，表示程序主循环运行。

如果 OLED 无显示但 PC13 翻转，说明 MCU 没卡死，重点排查 OLED 供电、地址、I2C 接线和初始化命令。

## 13. 常见问题排查

### 13.1 OLED 完全黑屏

先查 VCC/GND、SCL/SDA、地址 `0x78` 是否匹配模块。若模块是 `0x3D`，代码应改成 `0x7A`。

如果地址 ACK 正常，再查初始化命令是否完整，尤其 `0xAE` 后是否最终发送了 `0xAF` 打开显示，电荷泵 `0x8D/0x14` 是否存在，control byte 是否用 `0x00` 发送命令。I2C 通了但 OLED 黑屏，很多时候不是总线错，而是控制器没有被正确配置到显示状态。

### 13.2 I2C 地址无 ACK

检查上拉电阻、PB6/PB7 是否接反、模块供电是否为 3.3V 或兼容 3.3V。

### 13.3 屏幕亮但图案位置不对

检查 `0xB0` page 设置和 `0x00/0x10` column 设置。page/column 指针错会让数据写到别的位置。

### 13.4 图案乱码

检查 control byte。命令必须用 `0x00`，显示数据必须用 `0x40`。

### 13.5 HAL 版无显示但寄存器版正常

对比 `hi2c1.Init.ClockSpeed`、PB6/PB7 模式、OLED 地址和 `HAL_I2C_Master_Transmit()` 返回值。HAL 版如果忽略返回值，错误不容易被看见。

## 14. 本课最核心的结论

1. I2C 只是把字节送到 OLED，SSD1306 决定这些字节是命令还是显存数据。
2. `0x78` 是常见 OLED 地址 `0x3C` 左移后的格式。
3. control byte 是 SSD1306 I2C 通信的分界点，`0x00` 为命令，`0x40` 为数据。
4. page 模式下，一个数据字节对应垂直 8 个像素。
5. 初始化命令决定 OLED 是否开启、如何寻址、如何扫描和如何驱动面板。
6. HAL 发送 API 简化了 I2C 事务，但不替你理解 OLED 控制器协议。

## 15. 建议你现在怎么读这节课

先把 `oled_write(control, value)` 理解透，再看 `oled_cmd()` 和 `oled_data()`。然后把初始化数组按命令/参数分组，最后观察 `0xB0/0x00/0x10` 和 128 字节图案如何落到屏幕顶部。

## 16. 扩展练习

1. 把 page 从 `0xB0` 改成 `0xB1`，观察图案下移 8 像素。
2. 把 `0x55/0xAA` 改成全 `0xFF` 或全 `0x00`，观察一页全亮或全灭。
3. 改写 `oled_write()`，一次发送多个显示数据字节，提高刷新效率。
4. 增加一个最小 6x8 字符显示函数。

## 17. 下一课预告

- 上一课：[27_i2c_software_eeprom](../27_i2c_software_eeprom/README.md)
- 下一课：[29_i2c_mpu6050](../29_i2c_mpu6050/README.md)
