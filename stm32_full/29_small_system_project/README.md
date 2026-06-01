# 第 29 课：小系统综合项目

## 1. 本课到底在学什么

本课不是引入一个全新外设，而是把前面学过的模块组合成一个小系统：

- LED：系统状态输出
- 按键：本地输入事件
- UART：上位机交互
- ADC：模拟量采样
- FreeRTOS：任务组织和事件分发

真正要学的是：多个模块不要都挤在一个 `while (1)` 里，而是按职责拆成任务，并用队列传递事件。

## 2. 学习目标

- 能把小系统拆成多个任务
- 理解输入事件、周期采样、串口命令、状态输出的分工
- 复习队列、中断入队、ADC 采样和 LED 控制
- 形成“先设计数据流，再写代码”的习惯

## 3. 目录结构

```text
29_small_system_project/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

## 4. 硬件环境

- PC13：板载 LED
- PB0：按键输入，按下接 GND
- PA0：ADC1_IN0，接电位器
- PA9/PA10：USART1，115200 8N1

## 5. 先建立脑图

```text
ADC 任务
  -> 周期读取 PA0
  -> 更新全局 ADC 值

按键任务
  -> 检测 PB0 按下沿
  -> 发送 EVENT_KEY 到事件队列

UART 中断
  -> 接收字节
  -> 放入 UART 队列

UART 任务
  -> 收到 t：发送 EVENT_UART_TOGGLE
  -> 收到 s：打印当前 ADC

控制任务
  -> 等待事件队列
  -> 翻转 LED，并打印事件来源

状态任务
  -> 每 2 秒打印一次 ADC
```

## 6. 核心名词解释

### 6.1 事件队列

事件队列是整个系统的“消息入口”。按键和 UART 命令都不直接改 LED，而是发事件给控制任务。

### 6.2 控制任务

控制任务统一处理 LED 状态变化。这样以后如果要增加蜂鸣器、屏幕提示、日志输出，不需要到处改按键任务和 UART 任务。

### 6.3 状态任务

状态任务是周期性任务，负责把系统内部状态通过 UART 输出。它和输入事件无关，所以独立成任务。

### 6.4 中断边界

USART1 中断只把字节放进队列；命令解析在 UART 任务里做。这是 RTOS 项目里非常重要的边界。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/29_small_system_project/reg/src/main.c)。

它手动配置：

- GPIO：PC13、PB0、PA0、PA9、PA10
- USART1：波特率、中断接收
- ADC1：软件触发单次采样
- FreeRTOS：创建事件队列、串口队列和 5 个任务

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/29_small_system_project/hal/src/main.c)。

| 模块 | HAL API | 底层含义 |
|---|---|---|
| GPIO | `HAL_GPIO_Init()` | 配置引脚模式 |
| UART | `HAL_UART_Receive_IT()` | 开启接收中断 |
| ADC | `HAL_ADC_Start()` | 启动一次转换 |
| 队列 | `xQueueSend()` | 发送系统事件 |
| 任务 | `xTaskCreate()` | 创建并交给调度器 |

## 9. 两种写法对比

寄存器版适合复习底层链路，HAL 版更接近项目工程。两者的系统结构完全一样：任务按职责拆分，事件通过队列流动。

## 10. 运行现象

- 串口上电打印 `system ready`
- 每 2 秒打印一次 `adc=xxxx`
- 按 PB0：LED 翻转，串口打印 `key`
- 串口输入 `t`：LED 翻转，串口打印 `toggle`
- 串口输入 `s`：立即打印当前 ADC 值

## 11. 常见问题排查

- 串口无输出：检查 PA9/PA10 和 115200 8N1
- 按键无反应：检查 PB0 是否接 GND、内部上拉是否开启
- ADC 一直为 0 或 4095：检查电位器接线
- 系统跑一会卡死：检查任务栈、heap、串口任务是否被长时间阻塞
- 中断相关 HardFault：检查 USART1 优先级是否满足 FreeRTOS FromISR 规则

## 12. 本课总结

小系统的关键不是外设越多越好，而是结构要清楚：输入产生事件，队列传递事件，控制任务统一处理，状态任务周期输出。

## 13. 扩展练习

- 把 ADC 阈值也接入事件队列
- 增加命令 `led on` / `led off`
- 把状态输出改成 JSON 风格
- 用 ADC DMA 替代本课的 ADC 轮询任务

## 14. 下一步建议

到这里，入门教程已经形成完整主线。后续可以继续扩展：

- FreeRTOS 软件定时器
- 互斥锁和临界区
- 低功耗 + RTOS Tickless
- 真实传感器驱动项目
