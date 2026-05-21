# 第 15 课：UART 中断接收

## 1. 本课要学什么

上一课 `14_uart_polling` 已经让你掌握了 UART 最基础的收发方法：

- 发送时轮询 `TXE`
- 接收时轮询 `RXNE`

这种方法很直观，但有一个明显问题：

- CPU 得一直主动去问串口："你收到数据了吗？"

这一课我们要把这个思路升级成：

- 平时 CPU 不用一直盯着 `RXNE`
- 当 USART1 真收到一个字节时
- 外设主动向 CPU 发起中断请求
- CPU 才进入中断函数处理

这就是：**UART 中断接收**

### 轮询 vs 中断

| 对比 | 轮询版（14） | 中断版（本课） |
|------|------------|--------------|
| CPU 如何获知数据到达 | 不断检查 RXNE | USART 主动通知 |
| 主循环空闲时 | 还在检查 RXNE | 可以做其他任务 |
| 响应实时性 | 取决于主循环调度速度 | 中断触发后立即响应 |
| 代码结构 | 收/发都在主循环 | 中断存数据，主循环做处理 |

---

## 2. 本课最终 Demo

### 2.1 硬件连接

使用 USART1：

- `PA9` → USART1_TX
- `PA10` → USART1_RX

接线：PA9 接模块 RX，PA10 接模块 TX，GND 接 GND。

### 2.2 串口参数

- `115200 8N1`

### 2.3 运行现象

与上一课完全相同（串口回显 + LED 控制），但背后的 CPU 行为完全不同：

- 主循环不再主动检查 RXNE
- 收到数据后自动进中断处理

---

## 3. 本课核心概念

### 3.1 RXNEIE（RXNE Interrupt Enable）

这是 USART 控制寄存器 CR1 中的 bit 5。

- 上一课（轮询）：RXNEIE = 0，RXNE 只作为状态位
- 本课（中断）：RXNEIE = 1，RXNE 置位时触发中断请求

简单说就是：串口收到数据后，"要不要通知 CPU" 由这位决定。

### 3.2 中断使能的两层配置

每层都不能少：

```
第 1 层（外设侧）：CR1.RXNEIE = 1 → USART 可以产生中断请求
第 2 层（NVIC 侧）：NVIC_EnableIRQ(USART1_IRQn) → CPU 可以接收中断
```

缺任何一层 → 中断不会被执行。

### 3.3 USART1_IRQHandler

这是 USART1 的中断服务函数，在中断向量表中定义。

当中断发生时，CPU 跳转到这里。

---

## 4. 为什么中断里要尽快读 DR

收到字节后：

1. RXNE 置位
2. 因 RXNEIE=1，触发中断
3. CPU 进入 USART1_IRQHandler
4. **必须尽快读 DR 取出数据**

原因：

- 读 DR 后 RXNE 自动清除，为接收下一字节做准备
- 如果不读，下一次数据到达可能会覆盖前一次数据

### 4.1 中断设计原则：快进快出

好的中断设计：

- 中断里只做"存数据 + 置标志"
- 主循环做"解析命令 + 控制 LED + 回显"

为什么？

- 中断里做复杂逻辑会拖慢系统实时性
- 如果中断处理时间 > 数据到达间隔，可能丢数据
- 代码更难维护、更难调试

---

## 5. 本课数据流总图

接收方向（中断通知）：

```
串口助手 → USB转串口 → PA10 → USART1 接收移位
  → RXNE 置位 → RXNEIE=1 → 中断请求 → NVIC 放行
  → CPU 进入 USART1_IRQHandler
  → 读 DR → g_rx_byte → g_rx_ready = 1
  → 退出中断
  → 主循环检测到 g_rx_ready → 处理命令
```

发送方向（仍用轮询，因为发送是主动的）：

```
主循环处理结果 → usart1_send_byte() → TX → 串口助手
```

---

## 6. 寄存器版核心配置变化

### 6.1 配置流程对比

| 步骤 | 轮询版（14） | 中断版（本课） |
|------|------------|--------------|
| 时钟、GPIO | 相同 | 相同 |
| BRR | 相同 | 相同 |
| CR1 | TE \| RE | **TE \| RE \| RXNEIE** |
| UE | 相同 | 相同 |
| NVIC | **不配置** | **配置 USART1_IRQn** |

### 6.2 中断函数写法

```c
void USART1_IRQHandler(void)
{
    if ((USART1->SR & USART_SR_RXNE) != 0U) {
        g_rx_byte = (uint8_t)USART1->DR;  // 读数据 + 自动清标志
        g_rx_ready = 1U;                   // 通知主循环
    }
}
```

**为什么要在中断里检查 SR？**

USART1 可能由多个事件触发中断（RXNE、TC、TXE、IDLE 等）。需要先确认是"收到数据"引起的中断，才做对应的处理。

---

## 7. HAL 版核心变化

### 7.1 新增 API：`HAL_UART_Receive_IT()`

```c
HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1);
```

**这不是"接收"，而是"启动中断接收流程"：**

1. 设置句柄内部状态（缓冲区地址、长度等）
2. 使能 CR1.RXNEIE
3. 立即返回（不等数据）

一旦有数据到达，HAL 内部自动处理中断，然后回调 `HAL_UART_RxCpltCallback()`。

### 7.2 HAL 中断链路

```
硬件收到字节 → RXNE 置位
  → USART1_IRQHandler() 中调用 HAL_UART_IRQHandler(&huart1)
  → HAL 检测 RXNE → 读 DR 写入 g_rx_irq_byte
  → 清除 RXNE 标志
  → 调用 HAL_UART_RxCpltCallback(&huart1)
  → 用户在回调中取数据、置标志
```

### 7.3 回调中要"重启接收"

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        g_rx_byte = g_rx_irq_byte;
        g_rx_ready = 1U;

        // ★ 关键：再次启动下一轮接收
        HAL_UART_Receive_IT(&huart1, &g_rx_irq_byte, 1);
    }
}
```

为什么需要再次调用 `HAL_UART_Receive_IT()`？

- `HAL_UART_Receive_IT(&h, &buf, 1)` 的意思是"用中断方式收 1 个字节"
- 收完 1 个字节后，本次中断接收任务完成
- 如果不再次调用，后续数据不会再进入中断接收流程

---

## 8. 寄存器版 vs HAL 版对照

| 功能 | 寄存器版 | HAL 版 |
|------|---------|--------|
| 开 RXNE 中断 | `CR1 \|= RXNEIE` | `HAL_UART_Receive_IT()` 内部完成 |
| 中断入口 | `USART1_IRQHandler()` | 在其中调用 `HAL_UART_IRQHandler()` |
| 读数据 | `g_rx_byte = DR` | HAL 自动读，然后进回调 |
| 业务处理 | 写在中断函数中 | 写在 `HAL_UART_RxCpltCallback` 中 |
| 重启接收 | 自动（读 DR 清 RXNE） | 需要再次 `HAL_UART_Receive_IT()` |
| NVIC 配置 | `NVIC_EnableIRQ()` | `HAL_NVIC_EnableIRQ()` |

---

## 9. 本课最容易出错的地方

### 9.1 只开了 RXNEIE，没开 NVIC

后果：USART 能产生中断请求，但 CPU 不响应。

### 9.2 只开了 NVIC，没开 RXNEIE

后果：NVIC 允许 USART1 中断，但 USART 不产生请求。

### 9.3 进中断后没读 DR

后果：RXNE 不被清除，新数据无法接收。

### 9.4 HAL 版忘了再次调用 `HAL_UART_Receive_IT()`

后果：只能收到第一个字节，后面像"死了一样"。

### 9.5 中断里做太多事

后果：响应变慢，可能丢数据。

---

## 10. 本课你真正应该掌握什么

1. `RXNEIE` 是"允许 RXNE 触发中断"的开关
2. UART 中断同样需要外设层 + NVIC 层两边都打开
3. 中断里最关键的动作是及时读 `DR`
4. 更好的结构是"中断存数据，主循环做业务"
5. HAL 版中断接收是一个 "开启 → 完成 → 回调 → 再开启" 的循环

---

## 11. 下一课会学什么

下一课建议继续做 `16_uart_dma`。

这样你就会把 UART 的三种典型收发方式串起来：

1. **轮询**（14）— CPU 主动检查
2. **中断**（本课 15）— 收到后通知 CPU
3. **DMA**（16）— 自动搬运，CPU 完全解放