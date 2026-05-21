# STM32 课程总索引

## 说明

这份文档用于汇总整套 STM32 教学课程的目录、先后顺序、依赖关系和每课最终 Demo 目标。

当前约束如下：

- 每个知识点一个目录
- 每课同时包含 `reg/` 和 `hal/`
- 每课必须有详细 `README.md`
- 每课都必须能落到一个真实可运行的 Demo
- 讲解方式统一遵循 [TEACHING_PROMPT.md](/home/myself/workspace/mcu/stm32/STM32F103C8T6/TEACHING_PROMPT.md)

## 当前进度

- `01_led`：已完成
- `02_gpio_key`：已完成
- 其余课程：课程骨架已规划，后续按同一标准逐课补全

## 阶段 1：基础核心能力

### 01_led

- 目标：GPIO 输出控制板载 LED
- Demo：`PC13` 周期闪烁
- 状态：已完成

### 02_gpio_key

- 目标：GPIO 输入读取按键状态
- Demo：按下 `PA0` 点亮 `PC13`
- 状态：已完成

### 03_clock_tree

- 目标：理解并配置 F103 时钟系统
- Demo：切换到 `HSE + PLL`，并通过不同延时现象或串口输出验证时钟频率
- 依赖：`01_led`

### 04_systick

- 目标：理解 Cortex-M3 SysTick 和毫秒节拍
- Demo：基于 SysTick 实现精确毫秒闪灯
- 依赖：`03_clock_tree`

### 05_timer_base

- 目标：掌握通用定时器的基本定时功能
- Demo：使用定时器中断周期翻转 LED
- 依赖：`03_clock_tree`

## 阶段 2：控制与波形输出

### 06_pwm_basic

- 目标：掌握 PWM 频率和占空比配置
- Demo：PWM 调节 LED 亮度
- 依赖：`05_timer_base`

### 07_pwm_advanced

- 目标：掌握动态更新 PWM
- Demo：呼吸灯
- 依赖：`06_pwm_basic`

### 08_exti

- 目标：掌握外部中断
- Demo：按键触发中断切换 LED 状态
- 依赖：`02_gpio_key`

### 09_input_capture

- 目标：掌握输入捕获
- Demo：测量输入方波频率或脉宽
- 依赖：`05_timer_base`

## 阶段 3：模拟量与 DMA

### 10_adc_polling

- 目标：掌握 ADC 单通道采样
- Demo：电位器采样并驱动 LED 节奏或串口输出
- 依赖：`03_clock_tree`

### 11_adc_interrupt

- 目标：掌握 ADC 中断方式采样
- Demo：EOC 中断读取 ADC 数据
- 依赖：`10_adc_polling`

### 12_dma_basic

- 目标：掌握 DMA 的基本搬运模型
- Demo：内存与外设之间的数据搬运基础实验
- 依赖：`03_clock_tree`

### 13_adc_dma

- 目标：掌握 ADC + DMA 联合使用
- Demo：连续采样并自动搬运数据
- 依赖：`10_adc_polling`, `12_dma_basic`

## 阶段 4：串行通信

### 14_uart_polling

- 目标：掌握 UART 基础收发
- Demo：串口回显或周期打印
- 依赖：`03_clock_tree`

### 15_uart_interrupt

- 目标：掌握 UART 中断接收
- Demo：串口接收中断控制 LED 或回显
- 依赖：`14_uart_polling`

### 16_uart_dma

- 目标：掌握 UART + DMA
- Demo：DMA 发送固定数据块
- 依赖：`14_uart_polling`, `12_dma_basic`

### 17_spi_basic

- 目标：掌握 SPI 主模式
- Demo：SPI 环回或驱动简单外设
- 依赖：`03_clock_tree`

### 18_i2c_basic

- 目标：掌握 I2C 基础时序和主机通信
- Demo：读写 EEPROM 或简单 I2C 模块
- 依赖：`03_clock_tree`

### 19_can_basic

- 目标：掌握 CAN 控制器基础收发
- Demo：发送和接收标准帧
- 依赖：外接 CAN 收发器

## 阶段 5：系统级能力

### 20_nvic_priority

- 目标：掌握中断优先级和分组
- Demo：两个中断源的优先级实验
- 依赖：`08_exti`, `05_timer_base`

### 21_watchdog_iwdg

- 目标：掌握独立看门狗
- Demo：喂狗与复位实验
- 依赖：`03_clock_tree`

### 22_watchdog_wwdg

- 目标：掌握窗口看门狗
- Demo：过早/过晚喂狗复位实验
- 依赖：`03_clock_tree`

### 23_rtc_basic

- 目标：掌握 RTC 和备份域基础
- Demo：简单走时实验
- 依赖：`03_clock_tree`

### 24_low_power_basic

- 目标：掌握基础低功耗模式
- Demo：进入睡眠并通过中断唤醒
- 依赖：`08_exti`

## 阶段 6：RTOS 入门

### 25_freertos_basic

- 目标：掌握 FreeRTOS 最小系统
- Demo：两个任务周期闪灯
- 建议板卡：F103 入门，F407 更舒适

### 26_freertos_queue

- 目标：掌握任务间通信
- Demo：一个任务发消息，另一个任务处理
- 依赖：`25_freertos_basic`

### 27_freertos_uart

- 目标：掌握 UART 与 RTOS 结合
- Demo：串口接收任务 + 队列
- 依赖：`15_uart_interrupt`, `25_freertos_basic`

### 28_freertos_adc_dma

- 目标：掌握 ADC/DMA 与 RTOS 结合
- Demo：采样任务处理 DMA 缓冲区
- 依赖：`13_adc_dma`, `25_freertos_basic`

### 29_small_system_project

- 目标：整合前面知识形成一个小系统
- Demo：按键、LED、UART、ADC、RTOS 多模块协同
- 依赖：前面多个核心课程

## 建议学习顺序

建议严格按课程编号学习，不要跳。

推荐主线：

1. `01_led`
2. `02_gpio_key`
3. `03_clock_tree`
4. `04_systick`
5. `05_timer_base`
6. `06_pwm_basic`
7. `08_exti`
8. `10_adc_polling`
9. `12_dma_basic`
10. `14_uart_polling`
11. 再逐步进入更复杂课题

## 说明

如果后续要继续批量生成课程内容，应遵循两条原则：

- 不牺牲讲解深度
- 不在单节课中超前引入过多未来内容
