# STM32F103 学习指南

面向 STM32F103C8T6（BluePill）的 29 课学习工程，每课包含中文说明、寄存器版 `reg/` 和 HAL 版 `hal/`，从 GPIO 学到 FreeRTOS 与综合项目。

本仓库是 [totrytakeoff/STM32F103_GUIDE](https://github.com/totrytakeoff/STM32F103_GUIDE) 的分支副本，默认开发分支为 `master`。

## 从这里开始

- [课程总索引](COURSE_INDEX.md)：29 课的顺序、依赖关系与实验目标。
- [学习计划](STUDY_PLAN.md)：按阶段安排学习任务。
- [教学说明](TEACHING_PROMPT.md)：课程讲解与练习方式。
- [第一课：LED](01_led/README.md)：从寄存器版与 HAL 版点灯开始。

| 课程范围 | 内容 |
| --- | --- |
| 01–05 | GPIO、时钟、SysTick、基础定时器 |
| 06–09 | PWM、外部中断、输入捕获 |
| 10–13 | ADC、DMA |
| 14–19 | UART、SPI、I2C、CAN |
| 20–24 | 中断优先级、看门狗、RTC、低功耗 |
| 25–29 | FreeRTOS、队列、外设协同与综合项目 |

## 编译一个实验

安装 PlatformIO Core 或 VS Code 的 PlatformIO 扩展，并准备 BluePill 和 ST-Link。当前 LED 工程使用 `ststm32` 平台、`genericSTM32F103C8` 板型和 `stm32cube` 框架。

在仓库根目录执行：

```bash
# 编译第一课寄存器版
pio run -d 01_led/reg

# 编译同一课的 HAL 版
pio run -d 01_led/hal

# 接好 ST-Link 后，将寄存器版烧录到开发板
pio run -d 01_led/reg -t upload
```

后续课程把 `01_led/reg` 替换成对应课程的 `reg` 或 `hal` 目录即可。具体接线、晶振和外设要求以每课 README 与 `platformio.ini` 为准。

## 仓库维护

课程源码、配置和文档纳入版本管理。PlatformIO 会重新生成 `.pio/` 中的构建缓存；`compile_commands.json` 也可由 `pio run -d 01_led/reg -t compiledb` 按需生成。这两类文件不应再次提交。

FreeRTOS 课程引用根目录的 [freertos](freertos) 配置与 [lib](lib) 源码，使用时应保留完整仓库目录关系。
