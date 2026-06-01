# 第 25 课：FreeRTOS 基础

## 1. 本课到底在学什么

本课表面上是“双任务闪灯”，真正学习的是：程序不再只有一个 `while (1)` 主循环，而是由 FreeRTOS 调度多个任务轮流运行。

前面的裸机程序通常是：

```text
main -> 初始化 -> while(1) 里自己安排所有事情
```

FreeRTOS 程序变成：

```text
main -> 初始化 -> 创建任务 -> 启动调度器
任务 A、任务 B、任务 C 由内核按优先级和 Tick 调度
```

## 2. 学习目标

- 理解任务 `Task` 是什么
- 理解 `Tick` 是 FreeRTOS 的时间基准
- 理解 `vTaskDelay()` 为什么不是普通忙等待
- 理解 `xTaskCreate()` 和 `vTaskStartScheduler()` 的作用
- 能区分裸机主循环和 RTOS 多任务结构

## 3. 目录结构

```text
25_freertos_basic/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

本阶段额外使用：

- [freertos/FreeRTOSConfig.h](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/freertos/FreeRTOSConfig.h)

`platformio.ini` 中通过 PlatformIO 原生配置引入 FreeRTOS：

```ini
board_build.stm32cube.custom_config_file = ../../freertos/FreeRTOSConfig.h
```

这行配置告诉 PlatformIO 的 STM32Cube 构建系统使用自定义的 `FreeRTOSConfig.h`，
构建系统会自动从 `framework-stm32cubef1` 包中编译 FreeRTOS 内核源码，
无需额外的构建脚本。

## 4. 硬件环境

- PC13：板载 LED
- PA1：可外接 LED 或示波器观察第二个任务

## 5. 先建立脑图

```text
初始化时钟和 GPIO
  -> xTaskCreate 创建 LED 任务
  -> xTaskCreate 创建 heartbeat 任务
  -> vTaskStartScheduler 启动调度器
  -> SysTick 周期进入 FreeRTOS Tick
  -> 内核根据延时到期和优先级切换任务
```

## 6. 核心名词解释

### 6.1 任务

任务就是一个长期运行的函数。它有自己的栈、优先级和状态。任务函数通常写成 `while (1)`，但它不应该一直霸占 CPU，而要在合适的时候阻塞或延时。

### 6.2 `xTaskCreate()`

它用来创建任务。参数包括任务函数、任务名、栈大小、参数、优先级和任务句柄。它只是“登记任务”，真正运行要等调度器启动。

### 6.3 `vTaskDelay()`

它让当前任务进入阻塞态一段时间。阻塞期间 CPU 可以运行别的任务，所以它不是浪费 CPU 的空循环。

### 6.4 `vTaskStartScheduler()`

它启动 FreeRTOS 调度器。调用后，程序主要由任务驱动，正常情况下不会再回到后面的 `while (1)`。

## 7. 寄存器版代码讲解

寄存器版在 [reg/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/25_freertos_basic/reg/src/main.c)。

寄存器版仍然手动配置 GPIO 和系统时钟；FreeRTOS 部分则使用官方 API。这样可以看清楚：RTOS 并不替代底层外设配置，它只是接管任务调度。

## 8. HAL版代码讲解

HAL 版在 [hal/src/main.c](/home/myself/workspace/mcu/stm32/STM32F103C8T6/STM32F103C8T6_GUIDE/25_freertos_basic/hal/src/main.c)。

HAL 负责时钟和 GPIO 初始化，FreeRTOS 负责运行两个任务：

- `led_task`：500ms 翻转 PC13
- `heartbeat_task`：1000ms 翻转 PA1

## 9. 两种写法对比

| 项目 | 寄存器版 | HAL版 |
|---|---|---|
| 外设初始化 | 手动写寄存器 | 调 HAL API |
| 任务创建 | FreeRTOS API | FreeRTOS API |
| 调度机制 | 相同 | 相同 |

## 10. 运行现象

- PC13 约 500ms 翻转一次
- PA1 约 1000ms 翻转一次
- 两个周期互不阻塞，说明任务在被调度器轮流运行

## 11. 常见问题排查

- 程序卡死：检查任务栈和 heap 是否足够
- LED 不闪：检查 `vTaskStartScheduler()` 是否执行
- 只运行一个任务：检查另一个任务是否没有 `vTaskDelay()`
- 编译找不到 FreeRTOS：检查 `platformio.ini` 中 `board_build.stm32cube.custom_config_file` 路径是否正确

## 12. 本课总结

FreeRTOS 最小链路是：配置硬件，创建任务，启动调度器，任务用阻塞/延时让出 CPU。

## 13. 扩展练习

- 修改两个任务优先级
- 删除 `vTaskDelay()` 观察另一个任务是否被影响
- 增加第三个任务周期翻转 PB0 外接 LED

## 14. 下一课预告

下一课学习队列。任务不应该随便共享变量，而应该用 RTOS 提供的通信机制传递事件。
