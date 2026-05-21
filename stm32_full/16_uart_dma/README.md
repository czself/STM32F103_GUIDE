# 第 16 课：UART + DMA

## 1. 本课要学什么

前两课你已经把 UART 的两种基本软件处理方式跑通了：

- `14_uart_polling` — CPU 轮询 TXE/RXNE
- `15_uart_interrupt` — 串口收到数据后，通过中断通知 CPU

这一课我们继续把串口主线补完整，进入第三种非常常用的方式：**UART + DMA 发送**。

这节课真正要学的是：

1. 为什么"发送一整段字符串"很适合交给 DMA
2. `USART1_TX` 在 F103 上为什么对应 `DMA1_Channel4`
3. `USART1->CR3.DMAT` 是干什么的
4. DMA 方向从"外设→内存"变成"内存→外设"时，配置有哪些变化
5. 为什么 UART + DMA 发送经常还要配合 DMA 传输完成中断

### UART 三种方式对比

| 方式 | 发送过程 | CPU 负担 |
|------|---------|---------|
| 轮询（14） | CPU 逐字节 wait TXE + 写 DR | 全程占用 |
| 中断（15） | 未涉及发送中断（发送用轮询） | 发送时占用 |
| **DMA（本课）** | **DMA 自动搬整段数据** | **启动一次就解放** |

---

## 2. 本课最终 Demo

### 2.1 硬件连接

使用 USART1，只需 TX 引脚：

- `PA9` → USB 转串口模块 `RX`
- `GND` → USB 转串口模块 `GND`

### 2.2 串口参数：115200 8N1

### 2.3 运行现象

1. 串口先打印启动说明（轮询发送）
2. 之后每隔约 1 秒，通过 DMA 发送一条固定字符串
3. 每发送完成一次，板载 LED 翻转一次

---

## 3. 为什么 UART 发送特别适合 DMA

轮询方式发一整段字符串，CPU 需要不断重复：

```
等 TXE → 写 1 字节 → 再等 TXE → 再写下一字节 → ...
```

这件事的特点是：**重复、规则固定、数据源在内存、目标固定**。

DMA 就是为这种场景设计的。

本课的核心思路：

- CPU 只需告诉 DMA：数据从哪来、发到哪去、发多少
- 之后 DMA 自动把整段数据一个字节一个字节喂给串口

---

## 4. 本课的数据流

```
内存字符串 → DMA1_Channel4 → USART1->DR → PA9(TX) → 串口助手
```

---

## 5. 为什么是 DMA1_Channel4

在 STM32F103 中，DMA 通道和外设之间的映射是固定的：

- `USART1_TX` → `DMA1_Channel4`
- `USART1_RX` → `DMA1_Channel5`
- `ADC1` → `DMA1_Channel1`

这是硬件设计决定的，必须查参考手册确认。

---

## 6. 本课新寄存器：USART1->CR3.DMAT

前面 UART 课主要接触 CR1、BRR、SR、DR。

本课新增一个关键寄存器：**CR3**

DMAT（DMA Mode for Transmission）位于 CR3 bit 7：

- 0 = 禁止 DMA 发送（默认）
- 1 = 允许 DMA 发送

如果不设 DMAT = 1，即使 DMA 通道配置好了，USART 也不会响应 DMA 请求。

---

## 7. 与 ADC+DMA 的配置对比

| 配置项 | ADC+DMA（12/13 课） | UART+DMA（本课） |
|--------|-------------------|-----------------|
| 方向 | 外设 → 内存 | **内存 → 外设** |
| DIR | 0 | **1** |
| 通道 | DMA1_Channel1 | **DMA1_Channel4** |
| 外设地址 | ADC1->DR（数据源） | USART1->DR（数据目标） |
| 数据宽度 | 16 位（半字） | **8 位（字节）** |
| 循环模式 | CIRC=1（持续采样） | **CIRC=0（发完即停）** |
| 完成中断 | 可选 | **需要（通知 CPU 发完了）** |

---

## 8. 本课寄存器版核心流程

### 8.1 初始化阶段

1. 系统时钟 72MHz
2. SysTick 配置（1ms 定时，供 delay_ms）
3. PC13 LED
4. PA9 为 USART1_TX（复用推挽输出）
5. USART1：BRR + TE + UE（不设 RE，本课不接收）
6. DMA1_Channel4：CPAR = &USART1->DR，CMAR/CNDTR 在启动时设置

### 8.2 发起一次 DMA 发送（usart1_dma_send）

1. 确认当前不忙
2. 等待 TC（上一次物理传输完成）
3. 关 DMA 通道 → 写 CMAR + CNDTR
4. 清 DMA 中断标志
5. 开 USART1->CR3.DMAT
6. 使能 DMA 通道

### 8.3 DMA 传输完成中断

1. 检查 TCIF4
2. 清中断标志
3. 关 DMA 通道
4. 关 DMAT
5. 置完成标志

---

## 9. 为什么 DAC+DMA 用 CIRC=1，而 UART+DMA 用 CIRC=0？

**ADC 是连续产生数据的外设**，适合"持续搬运"——CIRC=1。

**UART 发送是离散事件**，发完一段就停了，等 CPU 再启动下一次——CIRC=0。

DMA 不是一律都开循环模式，要看外设数据流是不是连续流式的。

---

## 10. HAL 版新增 API

### 10.1 `HAL_UART_Transmit_DMA()`

一键启动 UART DMA 发送，内部完成：

1. 通过 hdmatx 找到 DMA 句柄
2. 配置 DMA 源地址、目标地址、长度
3. 开 DMAT
4. 启动 DMA

### 10.2 `HAL_UART_TxCpltCallback()`

DMA 发送完成回调。本课在其中置完成标志。

### 10.3 为什么 HAL 版要同时开两个 IRQ

- `DMA1_Channel4_IRQn`：DMA 搬运完成中断
- `USART1_IRQn`：USART TC（传输完成）事件中断

HAL 需要两者配合完成完整的发送收尾流程。

---

## 11. 本课最容易出错的地方

### 11.1 DMA 通道用错

必须用 `DMA1_Channel4`，不是 Channel1。

### 11.2 方向配反

本课是 `内存 → 外设`（DIR=1），不是 ADC 的 `外设 → 内存`（DIR=0）。

### 11.3 忘了开 DMAT

USART1->CR3.DMAT = 1 这步不能少，否则 DMA 通道使能了也没用。

### 11.4 忘了开 MINC

发送字符串时需要内存地址自增，否则 DMA 一直发第一个字节。

### 11.5 发送还没结束又再次启动

本课用 g_uart_dma_busy 状态变量防止重入。

### 11.6 发送了字符串的 '\0'

`sizeof(g_dma_message) - 1` 去掉结束符，只发送有效字符。

---

## 12. 本课你真正应该掌握什么

1. UART 发送长数据块时，DMA 可以替 CPU 做重复搬运
2. UART DMA 发送的本质：内存缓冲区 → DMA → USARTx->DR
3. DMAT 是 UART 侧打开 DMA 发送链路的关键位
4. DMA1_Channel4 是 USART1_TX 在 F103 上的固定映射
5. 发送完成后需要传输完成中断来收尾和触发后续动作
6. UART 三条主线已完整：轮询 → 中断 → DMA

---

## 13. 下一课会学什么

下一课建议进入 `17_spi_basic`。

到这里，UART 这条主线已经完整了：

1. **轮询（14）** — CPU 主动检查
2. **中断（15）** — 收到后通知 CPU
3. **DMA（16）** — 自动搬运

后面我们就切到另一条非常重要的通信总线：**SPI**。