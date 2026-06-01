# 第 27 课：FreeRTOS + UART

## 1. 本课到底在学什么

本课表面上是串口回显，真正学习的是“中断和任务如何配合”。

UART 字节到达是硬件中断事件；命令解析、回显、控制 LED 是业务逻辑。RTOS 中常见写法是：

```text
UART RX 中断
  -> 只读取字节
  -> xQueueSendFromISR 投递到队列
UART 任务
  -> xQueueReceive 等待字节
  -> 解析命令、回显、控制 LED
```

## 2. 学习目标

- 理解为什么中断里不做复杂业务
- 理解 `xQueueSendFromISR()` 和普通 `xQueueSend()` 的区别
- 理解 FreeRTOS 中断优先级限制
- 掌握 UART 接收中断 + 任务解析的基本框架

## 3. 目录结构

```text
27_freertos_uart/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- USART1_TX：PA9
- USART1_RX：PA10
- 串口参数：115200, 8N1
- PC13：板载 LED

## 5. 先建立脑图

```text
USART1 收到字节
  -> RXNE 置位
  -> USART1_IRQHandler
  -> 读取 DR 清 RXNE
  -> xQueueSendFromISR 投递字节
  -> UART 任务被唤醒
  -> 回显字节
  -> 如果是 t/T，翻转 LED
```

## 6. 核心名词解释

### 6.1 `RXNE`

Receive data register not empty，接收数据寄存器非空。它表示 UART 收到了一个新字节，读取 `DR` 后会清除。

### 6.2 `xQueueSendFromISR()`

这是中断上下文专用的队列发送 API。它不会像任务 API 那样阻塞，并且可以通过 `portYIELD_FROM_ISR()` 请求中断结束后立即切到被唤醒的高优先级任务。

### 6.3 FreeRTOS 中断优先级

会调用 FreeRTOS `FromISR` API 的中断，优先级不能高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。本课把 USART1 设置为优先级 6，符合配置要求。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/27_freertos_uart/reg/src/main.c)。

关键寄存器：

- `USART1->BRR`：波特率
- `USART1->CR1.RE/TE`：接收/发送使能
- `USART1->CR1.RXNEIE`：接收中断使能
- `USART1->SR.RXNE`：收到字节标志
- `USART1->DR`：接收/发送数据寄存器

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/27_freertos_uart/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `HAL_UART_Receive_IT()` | 开启接收中断 |
| `HAL_UART_RxCpltCallback()` | 收到 1 字节后的回调 |
| `HAL_UART_Transmit()` | 发送回显字节 |
| `USART1_IRQHandler()` | HAL 中断入口，内部处理 RXNE |

## 9. 两种写法对比

寄存器版能看见 `RXNE` 和 `DR`；HAL 版更接近实际工程，但回调里仍然只投递队列，不做复杂业务。

## 10. 运行现象

- 串口输入任意字符，会原样回显
- 输入 `t` 或 `T`，PC13 翻转

## 11. 常见问题排查

- 没有回显：查 PA9/PA10 接线和串口参数
- 收一个字节后不再接收：HAL 版要在回调里重新调用 `HAL_UART_Receive_IT()`
- 进 HardFault：检查 USART 中断优先级是否满足 FreeRTOS FromISR 要求
- 字节丢失：增大队列长度或提高 UART 任务优先级

## 12. 本课总结

RTOS 中 UART 的健康结构是：中断收得快，任务处理得清楚。不要把命令解析塞进中断里。

## 13. 扩展练习

- 输入 `1` 点亮 LED，输入 `0` 熄灭 LED
- 支持一行命令，以回车作为结束符
- 加入发送队列，让多个任务都能安全打印

## 14. 下一课预告

下一课把 ADC + DMA 接入 RTOS：DMA 中断通知任务处理采样缓冲区。
