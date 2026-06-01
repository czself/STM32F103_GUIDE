# 第 26 课：FreeRTOS 队列

## 1. 本课到底在学什么

本课表面上是“按键控制 LED”，真正学习的是任务之间如何安全传递事件。

裸机里你可能会让按键代码直接改 LED。但在 RTOS 中，更好的结构是：

```text
按键任务只负责检测按键
  -> 通过队列发送一个事件
LED 任务只负责等待事件并控制 LED
```

这样两个任务职责清楚，互相不直接依赖。

## 2. 学习目标

- 理解队列 `Queue` 是任务间通信工具
- 理解 `xQueueCreate()`、`xQueueSend()`、`xQueueReceive()`
- 理解阻塞等待队列为什么能节省 CPU
- 能把“按键事件”和“LED 动作”拆成两个任务

## 3. 目录结构

```text
26_freertos_queue/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- PA0：按键输入，按下接 GND
- PC13：板载 LED

## 5. 先建立脑图

```text
创建队列
  -> 创建按键任务
  -> 创建 LED 任务

按键任务
  -> 周期扫描 PA0
  -> 检测到按下沿
  -> xQueueSend 发送事件

LED 任务
  -> xQueueReceive 阻塞等待
  -> 收到事件后翻转 PC13
```

## 6. 核心名词解释

### 6.1 队列

队列是 FreeRTOS 提供的先进先出缓冲区。发送方把数据放进去，接收方从里面取出来。它解决的是任务间“什么时候发生了什么”的传递问题。

### 6.2 `xQueueCreate()`

创建队列。参数是队列长度和每个元素的大小。本课创建 4 个 `uint8_t` 元素的队列。

### 6.3 `xQueueSend()`

把一个事件写入队列。如果队列满，可以选择等待或立即返回。本课按键事件很短，所以不等待。

### 6.4 `xQueueReceive()`

从队列取数据。本课 LED 任务使用 `portMAX_DELAY`，表示没有事件时一直阻塞，不浪费 CPU。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/26_freertos_queue/reg/src/main.c)。

寄存器部分配置 PA0 上拉输入和 PC13 输出；FreeRTOS 部分创建队列和两个任务。按键任务只发送事件，不直接控制 LED。

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/26_freertos_queue/hal/src/main.c)。

HAL 版的 GPIO 初始化更短，但队列 API 完全相同。这说明 RTOS 通信机制不依赖你用寄存器还是 HAL 配外设。

## 9. 两种写法对比

| 项目 | 寄存器版 | HAL版 |
|---|---|---|
| 按键读取 | `GPIOA->IDR` | `HAL_GPIO_ReadPin()` |
| LED 翻转 | `ODR/BSRR/BRR` | `HAL_GPIO_TogglePin()` |
| 队列通信 | FreeRTOS API | FreeRTOS API |

## 10. 运行现象

每按下一次 PA0，PC13 状态翻转一次。长按不会疯狂翻转，因为代码检测的是“按下沿”。

## 11. 常见问题排查

- 按键无效：检查 PA0 上拉和接线
- 按一次触发多次：增加消抖时间
- LED 任务没反应：检查队列是否创建成功、任务是否启动
- 队列满：说明发送太快或接收任务优先级/阻塞逻辑有问题

## 12. 本课总结

队列让任务之间通过消息协作，而不是互相直接改变量。RTOS 程序的结构感从这里开始变强。

## 13. 扩展练习

- 队列里发送不同命令：单闪、双闪、常亮
- 把 PA0 改成 EXTI 中断，再用 `xQueueSendFromISR()`
- 观察接收任务阻塞时 CPU 是否仍能运行其他任务

## 14. 下一课预告

下一课把 UART 中断接收接入 FreeRTOS：中断负责收字节，任务负责解析。
