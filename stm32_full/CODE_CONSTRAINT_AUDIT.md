# STM32 教学代码约束审查清单

## 1. 审查目标

本文件用于检查 `00` 到 `62` 每一课的代码是否符合 `TEACHING_PROMPT.md` 中的“教学代码”标准。

本轮重点看代码本身，不直接大规模修改课程代码。检查范围包括：

- `reg/src/main.c`
- `hal/src/main.c`
- `reg/platformio.ini`
- `hal/platformio.ini`
- FreeRTOS 课程涉及的 `FreeRTOSConfig.h`

审查标准不是“能不能跑”这么窄，而是看代码是否能作为教学材料逐行阅读：

- 是否一行塞多个关键动作
- 是否把函数、任务函数、Hook、`main()` 压成一行
- 是否有足够教学注释解释“为什么、硬件/内核后果、前置条件、错了怎样”
- 寄存器版是否能看出时钟、GPIO、外设寄存器、标志位、启动顺序
- HAL 版是否能反推结构体字段和 API 对应的底层配置意图
- FreeRTOS 课程是否解释任务、调度器、队列、信号量、Hook、FromISR、heap/stack
- README 讲到的硬件、引脚、协议、运行现象是否和代码一致

## 2. 总体结论

所有课程目录均存在 `README.md`、`reg/src/main.c`、`hal/src/main.c`，结构完整。

但是代码质量分层明显：

- 前 1/3 的课程整体更接近教学代码，尤其 `01_led`、`02_gpio_key`、`03_clock_tree`、`05_systick`、`06_timer_base`、`11_input_capture`、`15_adc_polling`、`18_dma_basic`。
- 中段有一批课程代码可以表达 Demo，但为了赶进度出现明显压缩写法，尤其 `12`、`14`、`17`、`19`、`24`、`27`、`28`、`29`、`31`、`36`、`38`、`40`。
- `44` 到 `58` 的 FreeRTOS 课程是当前最高风险区域：大量文件只有 20 到 30 行，几乎没有教学注释，任务、Hook、信号量、队列、调度器 API 被压缩在一行里，不符合教学代码标准。
- `59`、`60`、`61` 比 `44` 到 `58` 明显好，但仍需要继续补充 RTOS 对象和 HAL/寄存器对应解释。
- `62_lcd_12864` 的 HAL 版代码较完整，reg 版是最小底层预览链路；这不是绝对错误，但必须在 README 和代码注释中明确二者教学边界。

## 3. 课程分级

### 3.1 合格或接近合格

这些课程基本能作为教学代码继续迭代，问题主要是少量注释补强或局部一行多语句：

| 课程 | 判断 |
| --- | --- |
| `01_led` | 教学注释充分，是当前代码颗粒度样板。 |
| `02_gpio_key` | reg/hal 都能体现 GPIO 输入输出链路，少量压缩语句可后续拆开。 |
| `03_clock_tree` | 时钟链路解释较完整。 |
| `05_systick` | SysTick 与 HAL tick 关系较清楚。 |
| `06_timer_base` | 定时器基础链路较完整，需拆少量多语句行。 |
| `08_pwm_basic` | PWM 配置和 HAL 字段对应较清楚。 |
| `10_exti` | EXTI/NVIC/回调链路较清楚。 |
| `11_input_capture` | 注释非常充分，接近样板。 |
| `15_adc_polling` | ADC 轮询链路解释充分。 |
| `16_adc_interrupt` | ADC 中断链路基本完整。 |
| `18_dma_basic` | DMA 搬运链路解释充分。 |
| `20_adc_dma` | ADC+DMA 基本可作为教学代码，局部函数仍需拆开。 |
| `21_uart_polling` | UART 轮询链路较完整。 |
| `22_uart_interrupt` | UART 中断链路较完整。 |
| `23_uart_dma` | UART DMA 链路较完整。 |
| `26_i2c_basic` | I2C 基础较完整。 |
| `30_spi_basic` | SPI 基础较完整。 |
| `32_can_basic` | CAN 基础较完整，但局部辅助函数过短。 |
| `33_nvic_priority` | NVIC 优先级解释充分。 |
| `34_watchdog_iwdg` | IWDG 代码教学性较好。 |
| `35_watchdog_wwdg` | WWDG 代码教学性较好。 |
| `37_rtc_basic` | RTC 代码基本可读。 |
| `39_low_power_sleep` | Sleep 低功耗链路基本可读。 |

### 3.2 基本合格但需要补教学注释

这些课能表达 Demo，但代码或 HAL 字段解释不足，建议后续按 README 顺序补注释：

| 课程 | 主要问题 |
| --- | --- |
| `00_environment_intro` | reg/hal 都较短，注释偏少，作为环境课可以接受但仍应解释最小工程链路。 |
| `04_core_architecture_memory_map` | hal 注释过少，未充分解释 HAL 观察方式与底层地址映射关系。 |
| `07_timer_output_compare` | reg/hal 注释极少，TIM2 输出比较配置没有逐项解释。 |
| `09_pwm_advanced` | HAL 版注释偏少，TIM1 高级特性需要更多字段到寄存器映射。 |
| `13_timer_encoder` | reg/hal 注释极少，编码器模式关键字段没有教学解释。 |
| `25_uart_packet_protocol` | 协议代码可跑，但帧结构、状态机、错误处理说明不足。 |
| `41_debug_toolchain` | 代码短，调试工具链相关变量/观察点注释不足。 |
| `42_fsmc_sram` | FSMC 寄存器/时序解释不足。 |
| `43_tft_lcd_fsmc` | TFT/FSMC 边界解释不足。 |
| `59_freertos_uart` | 相比 44-58 已好很多，但 UART ISR 到 RTOS 对象的边界还应逐项补。 |
| `60_freertos_adc_dma` | 总体可读，仍需在代码中补 `vTaskNotifyGiveFromISR`、`ulTaskNotifyTake`、DMA 半满/全满的内核后果。 |
| `61_small_system_project` | 系统设计说明不错，但 HAL 版代码注释密度偏低，多队列/多任务边界应继续拆。 |
| `62_lcd_12864` | HAL 版完整，reg 版只是预览链路；需补充 reg/hal 教学目标不完全相同的代码级说明。 |

### 3.3 需重构代码可读性

这些课程存在明显一行多语句、函数压缩成一行、HAL 字段堆叠等问题，已经影响初学者阅读：

| 课程 | 文件 | 具体问题 | 优先级 |
| --- | --- | --- | --- |
| `12_timer_pwm_input` | `hal/src/main.c` | `__HAL_RCC...`、GPIO 字段、TIM 输入捕获字段、主循环多处压缩在一行；HAL 字段无法逐项反推底层寄存器。 | 高 |
| `14_timer_advanced_tim1` | `reg/src/main.c`, `hal/src/main.c` | 文件很短但包含 TIM1 高级功能，配置压缩且注释极少。 | 高 |
| `17_adc_multichannel_scan` | `reg/src/main.c`, `hal/src/main.c` | 多通道 ADC 配置压缩，缺少 SQR/SMPR/通道顺序解释。 | 高 |
| `19_dma_memory_uart_cases` | `reg/src/main.c`, `hal/src/main.c` | DMA/UART 案例大量长行，代码像草稿，不适合逐行教学。 | 高 |
| `24_uart_printf_redirect` | `reg/src/main.c`, `hal/src/main.c` | `fputc()` 和 `main()` 压成一行，重定向机制没有解释。 | 高 |
| `27_i2c_software_eeprom` | `reg/src/main.c`, `hal/src/main.c` | 多个 I2C/EEPROM 操作函数一行写完，ACK/等待/地址流程缺注释。 | 高 |
| `28_oled_ssd1306` | `reg/src/main.c`, `hal/src/main.c` | SSD1306 初始化、命令序列、I2C 发送函数压缩严重。 | 高 |
| `29_i2c_mpu6050` | `reg/src/main.c`, `hal/src/main.c` | `main()`、I2C 初始化、寄存器读写压缩严重，MPU6050 唤醒和 WHO_AM_I 没有教学拆解。 | 高 |
| `31_spi_w25q64` | `reg/src/main.c`, `hal/src/main.c` | W25Q64 命令、片选、JEDEC ID 读取压缩在长行，SPI Flash 教学链路不清。 | 高 |
| `36_bkp_backup_register` | `reg/src/main.c`, `hal/src/main.c` | BKP/PWR/RTC 域访问逻辑压缩，备份域写保护含义没有代码注释。 | 中 |
| `38_flash_internal` | `reg/src/main.c`, `hal/src/main.c` | Flash 解锁、擦除、写入、锁定流程压缩，风险较高。 | 高 |
| `40_low_power_stop_standby` | `reg/src/main.c`, `hal/src/main.c` | STOP/STANDBY 入口、唤醒标志、时钟恢复流程压缩，低功耗课不适合这样写。 | 高 |

### 3.4 FreeRTOS 高风险重构区

`44` 到 `58` 的 FreeRTOS 课程需要整体重构代码可读性。共同问题如下：

- `reg/src/main.c` 和 `hal/src/main.c` 普遍只有 20 到 30 行。
- 几乎没有教学注释。
- 时钟、GPIO、任务创建、调度器启动、Hook、队列/信号量/事件组/通知 API 大量写在同一行。
- `vApplicationMallocFailedHook()`、`vApplicationStackOverflowHook()` 裸写，没有解释 heap 失败、栈溢出、关中断和死循环等待调试器的原因。
- `main()` 经常压成一行，学生看不清初始化顺序、对象创建顺序、错误检查顺序。
- RTOS 对象创建后缺少对象含义说明，例如二值信号量、计数信号量、互斥量、队列集、事件组、软件定时器、任务通知。

逐课清单：

| 课程 | 主要问题 | 优先级 |
| --- | --- | --- |
| `44_freertos_intro_porting` | 最基础移植课却几乎无注释，任务/调度器/Hook 没拆。 | 高 |
| `45_freertos_task_create_delete` | 任务创建/删除逻辑压缩，`xTaskCreate` 参数和删除后果没讲。 | 高 |
| `46_freertos_task_suspend_resume` | suspend/resume API 没有解释任务状态变化。 | 高 |
| `47_freertos_scheduler_time_slice` | 时间片调度实验缺少同优先级任务切换说明。 | 高 |
| `48_freertos_time_management` | `vTaskDelay`、tick、阻塞态没有代码级解释。 | 高 |
| `49_freertos_interrupt_critical` | 中断/临界区/FromISR 边界没有拆清楚。 | 高 |
| `50_freertos_queue` | 队列创建、发送、接收、阻塞等待压缩严重。 | 高 |
| `51_freertos_semaphore_binary_counting` | 二值/计数信号量裸写，正是用户指出的一行糊完问题。 | 高 |
| `52_freertos_mutex_priority_inversion` | 互斥量与优先级继承没有代码级说明。 | 高 |
| `53_freertos_queue_set` | 队列集对象和成员队列关系没有代码级解释。 | 高 |
| `54_freertos_event_group` | 事件位、等待条件、清位策略没拆。 | 高 |
| `55_freertos_task_notification` | 任务通知轻量性的代码体现不足。 | 高 |
| `56_freertos_software_timer` | 软件定时器回调由谁执行、定时器服务任务是什么未说明。 | 高 |
| `57_freertos_memory_management` | heap/stack 内存管理课本身没有代码级注释，尤其不合格。 | 高 |
| `58_freertos_low_power_tickless` | tickless idle 课程代码极短，低功耗与 RTOS tick 停止/恢复没有拆。 | 高 |

### 3.5 代码行为或文档边界需核对

| 课程 | 问题 | 建议 |
| --- | --- | --- |
| `62_lcd_12864` | `reg` 版只把前 64 字节走 SPI 预览链路，`hal` 版才完整实现 ST7920 文本/位图刷新。 | 保留可以，但 README 和代码注释必须明确 reg 版不是完整刷新；否则学生会误以为两版功能等价。 |
| `58_freertos_low_power_tickless` | `platformio.ini` 使用本课局部 `freertos/FreeRTOSConfig.h`，其他 FreeRTOS 课使用公共 `../../freertos`。 | 这是 tickless 课程的合理差异，但 README 和审查清单中要说明它是有意使用局部配置。 |
| `44` 到 `57` | `platformio.ini` 基本都指向公共 `../../freertos` 和 `../../lib/FreeRTOS-Kernel/...`。 | 路径看起来一致，主要问题不是配置路径，而是代码教学性。 |

## 4. 按课程的最终分类

| 分类 | 课程 |
| --- | --- |
| 合格或接近合格 | `01`, `02`, `03`, `05`, `06`, `08`, `10`, `11`, `15`, `16`, `18`, `20`, `21`, `22`, `23`, `26`, `30`, `32`, `33`, `34`, `35`, `37`, `39` |
| 基本合格但需补注释 | `00`, `04`, `07`, `09`, `13`, `25`, `41`, `42`, `43`, `59`, `60`, `61`, `62` |
| 需重构可读性 | `12`, `14`, `17`, `19`, `24`, `27`, `28`, `29`, `31`, `36`, `38`, `40` |
| FreeRTOS 高风险重构区 | `44`, `45`, `46`, `47`, `48`, `49`, `50`, `51`, `52`, `53`, `54`, `55`, `56`, `57`, `58` |

## 5. 建议修复顺序

1. 先修 `44` 到 `58`。这些课程问题最集中，也最容易被学生直接看懵。
2. 再修 `24`、`27`、`28`、`29`、`31`。这些外设课压缩严重，且 I2C/SPI/Flash 类知识本来就需要按时序讲。
3. 再修 `12`、`14`、`17`、`19`、`36`、`38`、`40`。这些涉及定时器、ADC、DMA、备份域、Flash、低功耗，配置错误后现象复杂，教学注释必须补足。
4. 最后补强 `07`、`09`、`13`、`25`、`41`、`42`、`43`、`59`、`60`、`61`、`62`。

## 6. 单课修复模板

后续修每一课时，建议按下面顺序，不要只做格式化：

1. 先读 `reg/src/main.c`、`hal/src/main.c`、两个 `platformio.ini` 和相关 `FreeRTOSConfig.h`。
2. 把一行多语句拆开。
3. 把一行函数、任务函数、Hook、`main()` 拆开。
4. 在关键寄存器操作前补教学注释：这一步改哪个寄存器/bit、为什么这样写、错了什么现象。
5. 在 HAL 结构体字段前补教学注释：字段表达什么硬件意图、对应哪类寄存器配置。
6. FreeRTOS 课程补 RTOS 对象注释：对象是什么、谁创建、谁发送、谁接收、阻塞/唤醒关系是什么。
7. 对照 README，修正引脚、外设、通道、DMA、现象描述不一致的问题。
8. 最后运行对应 `pio run` 验证 reg/hal 两版至少能编译。

