# 第 28 课：FreeRTOS + ADC DMA

## 1. 本课到底在学什么

本课表面上是 ADC 采样控制 LED，真正学习的是“DMA 自动搬运 + 任务处理数据”的结构。

裸机 ADC DMA 常见问题是：数据到了以后谁处理？在 RTOS 中可以这样组织：

```text
ADC 连续转换
  -> DMA 循环搬运到内存缓冲区
  -> DMA 半满/全满中断
  -> 通知 ADC 任务
  -> ADC 任务计算平均值并控制 LED
```

## 2. 学习目标

- 理解 DMA 中断不应该做大量计算
- 理解任务通知 `vTaskNotifyGiveFromISR()` / `ulTaskNotifyTake()`
- 理解 ADC 缓冲区半满/全满事件
- 能把采样链路和业务处理拆开

## 3. 目录结构

```text
28_freertos_adc_dma/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- PA0：ADC1_IN0，接电位器中间端
- PC13：板载 LED

## 5. 先建立脑图

```text
ADC1 采样 PA0
  -> DMA1_Channel1 把 ADC1->DR 搬到数组
  -> 半满 HTIF / 全满 TCIF 中断
  -> FromISR API 通知任务
  -> ADC 任务计算平均值
  -> 平均值大于 2048 点亮 LED，否则熄灭
```

## 6. 核心名词解释

### 6.1 任务通知

任务通知是 FreeRTOS 给每个任务内置的轻量级事件机制。它比队列更轻，但只能通知特定任务，适合 DMA 完成这类简单事件。

### 6.2 `vTaskNotifyGiveFromISR()`

中断里给任务发通知。DMA 中断不能直接长时间计算平均值，所以只通知 ADC 任务“缓冲区有新数据”。

### 6.3 `ulTaskNotifyTake()`

任务等待通知。没有通知时任务阻塞，有通知时醒来处理数据。

### 6.4 DMA 半满/全满

循环 DMA 缓冲区有两个常用事件：

- 半满：前半段数据更新完成
- 全满：后半段数据更新完成

本课为了简化，收到任意一种事件后都计算整个缓冲区平均值。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/28_freertos_adc_dma/reg/src/main.c)。

关键寄存器：

- `ADC1->CR2.DMA/CONT`：允许 DMA、连续转换
- `DMA1_Channel1->CPAR`：外设地址，指向 `ADC1->DR`
- `DMA1_Channel1->CMAR`：内存缓冲区地址
- `DMA_CCR_CIRC`：循环模式
- `DMA_CCR_HTIE/TCIE`：半满/全满中断

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/28_freertos_adc_dma/hal/src/main.c)。

| HAL 写法 | 底层含义 |
|---|---|
| `HAL_ADC_Start_DMA()` | 启动 ADC 并让 DMA 搬运结果 |
| `HAL_ADC_ConvHalfCpltCallback()` | DMA 半满回调 |
| `HAL_ADC_ConvCpltCallback()` | DMA 全满回调 |
| `__HAL_LINKDMA()` | 把 ADC 句柄和 DMA 句柄关联起来 |

## 9. 两种写法对比

寄存器版更清楚地暴露 ADC 和 DMA 的握手关系；HAL 版把 DMA 回调和 ADC 句柄关联起来，工程上更方便。

## 10. 运行现象

- PA0 电压较低时，PC13 熄灭
- PA0 电压超过约 1.65V 时，PC13 点亮
- 转动电位器，LED 状态随平均值变化

## 11. 常见问题排查

- ADC 值不变：检查 PA0 是否配置成模拟输入
- DMA 没中断：检查 `DMA1_Channel1_IRQn` 和 HTIE/TCIE
- 调用 FromISR 后异常：检查 DMA 中断优先级
- LED 抖动：增加缓冲区长度或做迟滞判断

## 12. 本课总结

ADC DMA 在 RTOS 中的主线是：硬件负责采样和搬运，中断负责通知，任务负责计算和决策。

## 13. 扩展练习

- 分别处理半缓冲区和后半缓冲区
- 把平均值通过 UART 任务打印
- 增加阈值迟滞，减少 LED 抖动

## 14. 下一课预告

下一课进入小系统项目，把按键、LED、UART、ADC 和 RTOS 任务组合起来。
