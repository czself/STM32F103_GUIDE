# 54_freertos_event_group - FreeRTOS 事件组

## 1. 本课到底在学什么

本课表面现象是：PA1 按 500ms 节奏翻转，PA2 按 900ms 节奏翻转，PC13 只有在 BIT_A 和 BIT_B 都被置位之后才翻转一次。PC13 不跟任何单个任务的周期完全一致，因为它等待的是“两个条件都到齐”。

真正要学的是 FreeRTOS Event Group，事件组。事件组用一个位图保存多个事件标志，一个任务可以等待其中任意 bit，也可以等待多个 bit 全部满足。本课 `task_a` 设置 BIT_A，`task_b` 设置 BIT_B，`wait_task` 用 `xEventGroupWaitBits()` 等 BIT_A 和 BIT_B 同时出现。

这节课接在队列集之后。队列集等“多个对象中哪个可读”，事件组等“一个 bit 集合中哪些条件已满足”。如果一个任务要等“传感器准备好 + 通信连接好 + 配置加载完成”，事件组比多个队列轮询更合适。

## 2. 本课学习目标

学完本课你应该能做到：

- 能解释 BIT_A 和 BIT_B 分别由哪个任务设置。
- 能解释 PA1、PA2、PC13 分别对应哪个任务或事件条件。
- 能说明 `xEventGroupCreate()` 创建的对象是什么。
- 能说明 `xEventGroupSetBits(g_events, BIT_A)` 和 `BIT_B` 的作用。
- 能拆开 `xEventGroupWaitBits(g_events, BIT_A|BIT_B, pdTRUE, pdTRUE, portMAX_DELAY)` 每个参数。
- 能解释 `pdTRUE` 清除 bit 和 `pdTRUE` 等待所有 bit 的区别。
- 能区分事件组和队列、队列集、信号量。
- 能根据 PC13 不翻、PA1/PA2 正常、事件 bit 没清除等现象排查。
- 能知道事件组适合状态组合，不适合传递大块数据。

## 3. 本课目录结构

```text
54_freertos_event_group/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

两个版本都包含 `event_groups.h`。事件组 API 属于 FreeRTOS，不属于 HAL。reg/hal 版本的事件组逻辑一致，差别只在 GPIO 翻转方式。

## 4. 实验硬件

- STM32F103C8T6 BluePill。
- ST-Link 下载器。
- PC13：wait_task 等到 BIT_A 和 BIT_B 都满足后翻转。
- PA1：task_a 每设置一次 BIT_A 后翻转。
- PA2：task_b 每设置一次 BIT_B 后翻转。
- PA0：初始化为上拉输入，当前未使用。
- 系统时钟：HSE 8MHz，PLL x9 到 72MHz。
- FreeRTOS 配置：事件组模块来自内核，当前工程已编译 `event_groups.c`。

本课没有中断设置事件组，也没有按键输入。所有 bit 都由任务周期性设置。

## 5. 先建立一个最基本的脑图

```text
复位启动
  -> 配 72MHz 时钟和 GPIO
  -> 创建事件组 g_events
  -> 创建 task_a，优先级 1
  -> 创建 task_b，优先级 1
  -> 创建 wait_task，优先级 2
  -> task_a 设置 BIT_A，翻转 PA1，延时 500ms
  -> task_b 设置 BIT_B，翻转 PA2，延时 900ms
  -> wait_task 等 BIT_A | BIT_B
  -> 两个 bit 都满足后 wait_task 返回
  -> 因 clearOnExit = pdTRUE，满足的 bit 被清除
  -> wait_task 翻转 PC13
  -> wait_task 再次等待下一轮 A+B 到齐
```

这条链路里，PA1 和 PA2 是条件产生证据，PC13 是条件组合满足证据。PC13 的节奏会受到 500ms 和 900ms 两个周期共同影响。

要特别注意两个 `pdTRUE` 参数。第一个 `pdTRUE` 表示 wait 成功返回时清除 bit；第二个 `pdTRUE` 表示必须等待所有指定 bit 都置位，而不是任意一个置位。

## 6. 先认识本课里出现的核心名词

### 6.1 `Event Group` 是什么

Event Group 是事件组，属于 FreeRTOS 内核对象层。

它用 bit 位记录多个事件状态。一个任务可以设置 bit，另一个任务可以等待 bit 条件。本课 `g_events` 保存 BIT_A 和 BIT_B 两个条件。

### 6.2 `EventGroupHandle_t` 是什么

事件组句柄类型。

`static EventGroupHandle_t g_events;` 保存事件组对象引用。设置和等待 bit 都通过它。

### 6.3 `BIT_A` 是什么

`#define BIT_A (1U << 0)`，表示事件组第 0 位。

task_a 周期性设置它。PA1 翻转表示 task_a 已经设置过 BIT_A。

### 6.4 `BIT_B` 是什么

`#define BIT_B (1U << 1)`，表示事件组第 1 位。

task_b 周期性设置它。PA2 翻转表示 task_b 已经设置过 BIT_B。

### 6.5 `xEventGroupCreate()` 是什么

创建事件组的 API。

它从 FreeRTOS heap 分配事件组对象。失败时返回 NULL，源码会停住。

### 6.6 `xEventGroupSetBits()` 是什么

设置事件组 bit 的 API。

task_a 设置 BIT_A，task_b 设置 BIT_B。设置 bit 会唤醒等待条件满足的任务。

### 6.7 `xEventGroupWaitBits()` 是什么

等待事件组 bit 条件的 API。

本课等待 `BIT_A | BIT_B`。只有两个 bit 都满足时，wait_task 才返回并翻转 PC13。

### 6.8 `clearOnExit = pdTRUE` 是什么

`xEventGroupWaitBits()` 的第三个参数为 `pdTRUE`。

它表示等待成功返回时，自动清除本次等待的 bit。这样下一轮必须重新由 task_a/task_b 设置 bit。

### 6.9 `waitForAllBits = pdTRUE` 是什么

第四个参数为 `pdTRUE`，表示必须等所有指定 bit 都置位。

如果改成 `pdFALSE`，任意一个 bit 到来就会返回，PC13 会更频繁翻转，语义完全不同。

### 6.10 `portMAX_DELAY` 在事件组里是什么

表示 wait_task 一直等到条件满足。

等待期间不占 CPU。若 A 或 B 永远不来，wait_task 会一直阻塞。

### 6.11 `task_a` 是什么

task_a 是事件 A 生产任务。

它设置 BIT_A、翻转 PA1、延时 500ms。它不直接翻转 PC13。

### 6.12 `task_b` 是什么

task_b 是事件 B 生产任务。

它设置 BIT_B、翻转 PA2、延时 900ms。

### 6.13 `wait_task` 是什么

wait_task 是事件组合等待任务。

它优先级 2，高于 task_a/task_b。两个 bit 都满足后，它翻转 PC13。

### 6.14 事件组和队列有什么区别

队列传数据，事件组传 bit 状态。

本课 wait_task 不接收 `uint8_t` 数据，只关心 BIT_A 和 BIT_B 是否到齐。

### 6.15 事件组和队列集有什么区别

队列集等待多个对象中哪个可读，事件组等待同一个 bit 集合中的条件组合。

上一课要选择 `g_a` 或 `g_b`，所以用队列集；本课要等 A 和 B 都完成，所以用事件组。

### 6.16 `PC13 / PA1 / PA2` 是什么证据

PA1 证明 A 事件被设置，PA2 证明 B 事件被设置，PC13 证明 A+B 条件被 wait_task 消费。

三个引脚分别对应事件生产和事件组合消费。

### 6.17 事件 bit 会不会排队

事件组 bit 是状态位，不是消息队列。

BIT_A 被设置多次，在 wait_task 清除前仍然只是“BIT_A 为 1”。如果你需要统计 A 事件发生了多少次，事件组不合适，应使用队列或计数信号量。

### 6.18 为什么 wait_task 优先级更高

wait_task 优先级为 2，task_a/task_b 为 1。

两个 bit 都满足后，wait_task 能更快运行并清除 bit，进入下一轮等待。若 wait_task 优先级很低，bit 可能保持已满足状态更久，影响你观察事件组合节奏。

## 7. 寄存器版代码逐步讲解

### 7.1 头文件分工

`event_groups.h` 提供事件组 API。

GPIO、时钟仍由 CMSIS 寄存器配置。事件组不是 STM32 外设。

### 7.2 时钟和 GPIO 初始化

系统时钟配置到 72MHz，GPIOC/GPIOA 打开时钟，PC13/PA1/PA2 输出，PA0 上拉输入。

本课使用 PC13、PA1、PA2。

### 7.3 定义 bit

`BIT_A = 1U << 0`，`BIT_B = 1U << 1`。

用位移定义能清楚看出每个事件占用哪一位。

### 7.4 创建事件组

`g_events = xEventGroupCreate();`

事件组创建失败时返回 NULL。main 会统一检查。

### 7.5 task_a 设置 BIT_A

task_a 调用 `xEventGroupSetBits(g_events, BIT_A)`。

设置后翻转 PA1，再延时 500ms。PA1 是 A 事件生产证据。

### 7.6 task_b 设置 BIT_B

task_b 调用 `xEventGroupSetBits(g_events, BIT_B)`。

设置后翻转 PA2，再延时 900ms。PA2 是 B 事件生产证据。

### 7.7 wait_task 等两个 bit

wait_task 调用：

```c
xEventGroupWaitBits(g_events, BIT_A | BIT_B, pdTRUE, pdTRUE, portMAX_DELAY);
```

它要求 A 和 B 都到齐才返回。

### 7.8 自动清除 bit

第三个参数 `pdTRUE` 让 wait_task 返回时清除 BIT_A 和 BIT_B。

这使下一轮 PC13 翻转必须等待 A/B 重新设置。

### 7.9 等待所有 bit

第四个参数 `pdTRUE` 表示等待所有 bit。

如果 A 先到而 B 没到，wait_task 继续阻塞。反过来也一样。

### 7.10 wait_task 翻转 PC13

等待成功后翻转 PC13。

PC13 表示 A+B 组合条件被消费，不是单个事件到来。

### 7.11 创建三个任务

task_a/task_b 优先级 1，wait_task 优先级 2。

wait_task 条件满足后能及时运行。

### 7.12 当前没有 FromISR

本课没有在中断中设置 bit。

若后续 ISR 设置事件组，应使用 `xEventGroupSetBitsFromISR()`，并注意 daemon task 和中断优先级限制。

### 7.13 返回值虽然没保存但很有用

`xEventGroupWaitBits()` 会返回等待时的事件位状态。

当前源码没有保存返回值，因为只要 A+B 到齐就翻转 PC13。真实工程里常会保存返回值，判断到底哪些 bit 触发、是否超时、是否有额外状态位。

### 7.14 500ms 和 900ms 为什么会形成组合节奏

task_a 每 500ms 设置 A，task_b 每 900ms 设置 B。wait_task 要等两个 bit 都到齐。

如果 A 先到，A bit 会保持为 1 等 B；B 到来后 wait_task 返回并清除 A/B。所以下一次 PC13 翻转取决于清除之后 A 和 B 重新到齐的时间，而不是简单等于 500ms 或 900ms。

### 7.15 clearOnExit 对下一轮的影响

第三个参数为 `pdTRUE` 时，wait_task 成功返回会清除 BIT_A 和 BIT_B。

这让每次 PC13 翻转都代表一轮新的 A+B 条件。如果不清除，PC13 可能在下一轮 wait 时立刻再次满足，失去“重新等待两个事件”的含义。

## 8. HAL 版代码逐步讲解

### 8.1 HAL 初始化

HAL 版用 HAL 初始化时钟和 GPIO，事件组逻辑不变。

### 8.2 HAL GPIO 对应关系

PA1 对应 task_a，PA2 对应 task_b，PC13 对应 wait_task。

HAL 翻转最终仍改变底层输出寄存器。

### 8.3 HAL 版事件组创建

`xEventGroupCreate()` 和寄存器版一致。

事件组不是 HAL 对象。

### 8.4 HAL 版 task_a/task_b

两个任务设置 bit 后分别用 HAL 翻转 PA1/PA2。

延时仍使用 `vTaskDelay()`。

### 8.5 HAL 版 wait_task

等待参数完全一样：清除 bit、等待所有 bit、无限等待。

收到组合事件后用 HAL 翻转 PC13。

### 8.6 HAL 版不使用 HAL_Delay

任务等待使用 FreeRTOS 延时或事件组阻塞。

HAL_Delay 不是本课事件等待模型。

### 8.7 HAL 版也要检查创建结果

事件组和任务创建失败都要停住。

HAL 初始化成功不代表 FreeRTOS 对象成功。

### 8.8 HAL 版调试方法

可以观察 PA1/PA2/PC13，也可以在 `xEventGroupWaitBits()` 返回后打断点。

### 8.9 HAL 版也能观察事件组状态

调试器中可以观察 `g_events` 是否非 NULL，并在 `xEventGroupSetBits()` 后看 wait_task 是否被唤醒。

事件组内部结构不一定适合直接手工解读，但断点命中顺序很有价值：A 设置、B 设置、wait 返回、PC13 翻转。

### 8.10 HAL 版不要用 GPIO 状态替代事件状态

PA1/PA2 翻转只是 task_a/task_b 执行过的证据，不等于事件组内部 bit 当前一定仍为 1。

因为 wait_task 返回时会清除 bit，所以你可能看到 PA1/PA2 已翻转，但事件组 bit 已经被清掉。判断 RTOS 状态要看事件组 API 路径，不只看 GPIO。

## 9. 两个版本真正应该怎么学

两个版本的事件组语义完全一样。reg/hal 只改变 GPIO 操作。

学习重点是 bit 条件：A 设置 bit0，B 设置 bit1，wait 等 bit0 和 bit1 都为 1，然后清除并翻转 PC13。

## 10. 检验问题清单

### 10.1 BIT_A 是哪一位？

**答**：第 0 位，即 `1U << 0`。

### 10.2 BIT_B 是哪一位？

**答**：第 1 位，即 `1U << 1`。

### 10.3 PC13 为什么不跟 PA1 同步？

**答**：PC13 等 A 和 B 都到齐，不是只等 A。

### 10.4 第三个 `pdTRUE` 表示什么？

**答**：等待成功返回时清除指定 bit。

### 10.5 第四个 `pdTRUE` 表示什么？

**答**：必须等待所有指定 bit 都置位。

### 10.6 如果第四个参数改成 `pdFALSE` 会怎样？

**答**：任意一个 bit 到来就返回，PC13 会更频繁翻转。

### 10.7 事件组能传递数据吗？

**答**：不适合传递数据。它主要传递 bit 状态，传数据应使用队列。

### 10.8 为什么返回后要清 bit？

**答**：清除后下一轮必须重新等待 A 和 B，避免旧事件重复触发。

### 10.9 本课有没有 ISR 设置 bit？

**答**：没有，所有 bit 都由任务设置。

### 10.10 事件组和队列集最大区别是什么？

**答**：事件组等 bit 条件组合，队列集等多个对象中哪个可读。

## 11. 工程实现步骤

### 11.1 需求分析

演示两个独立任务设置事件 bit，第三个任务等待两个条件都满足。

### 11.2 硬件核查

确认 PA1、PA2、PC13 可观察，PA0 不参与。

### 11.3 寄存器路线

配置 GPIO，创建事件组和三个任务，A/B 设置 bit，wait 等所有 bit。

### 11.4 HAL 路线

用 HAL 配置硬件，事件组逻辑不变。

### 11.5 工程思维

事件组适合等待多个状态条件组合。要明确是否清 bit、等待任意还是全部。

### 11.6 常见工程陷阱

把事件组当队列传数据、忘记清 bit、把 wait-all 写成 wait-any、bit 定义重复、创建失败不检查，都是常见问题。

## 12. 运行现象

PA1 每 500ms 左右翻转，PA2 每 900ms 左右翻转。PC13 只有在 A 和 B 都置位后才翻转，然后对应 bit 被清除，进入下一轮等待。

调试器可观察 `g_events` 是否非 NULL，也可在 `xEventGroupWaitBits()` 返回后打断点。

## 13. 常见问题排查

### 13.1 PA1 不翻

查 task_a 是否创建成功，BIT_A 设置是否执行，PA1 GPIO 是否正确。

### 13.2 PA2 不翻

查 task_b 是否创建成功，BIT_B 设置是否执行，PA2 GPIO 是否正确。

### 13.3 PC13 不翻但 PA1/PA2 正常

查 wait_task 是否创建成功，等待参数是否仍是 `BIT_A | BIT_B`、wait-all 是否为 `pdTRUE`。

### 13.4 PC13 翻得太频繁

检查第四个参数是否被改成 `pdFALSE`，或者第三个参数是否不清 bit 导致旧事件重复满足。

### 13.5 事件组创建失败

检查 heap 和对象数量。`g_events == NULL` 时系统会停住。

### 13.6 进入 stack overflow hook

检查三个任务栈。添加打印或复杂逻辑后要增大栈。

### 13.7 bit 定义冲突

确认 BIT_A 和 BIT_B 是不同位。如果两个宏使用同一位，wait-all 语义会被破坏。

### 13.8 HAL 版 GPIO 不动

先确认对应任务是否运行和 bit 是否设置，再查 HAL GPIO 初始化。

### 13.9 PA1/PA2 都翻但 PC13 很少翻

这可能是正常现象，因为 PC13 等待 A 和 B 都到齐，且返回后会清除 bit。若 A/B 周期差异大，组合事件频率不等于任一单独事件频率。

### 13.10 PC13 连续快速翻转

检查 clearOnExit 是否为 `pdTRUE`。如果不清 bit，wait_task 可能反复看到旧 bit 已满足，从而快速返回。

### 13.11 需要统计次数却用了事件组

如果你关心 A 发生 10 次、B 发生 3 次，事件组不能保存次数。它只保存 bit 状态。应改用队列、计数信号量或任务通知计数模式。

### 13.12 wait_task 一直阻塞

分别在 task_a 和 task_b 的 `xEventGroupSetBits()` 后打断点，确认两个 bit 都确实被设置。如果只有一个任务运行，wait-all 条件永远不满足，PC13 就不会翻转。

### 13.13 事件 bit 没按预期清除

检查 `xEventGroupWaitBits()` 第三个参数是否仍是 `pdTRUE`。如果改成 `pdFALSE`，BIT_A/B 会保留，下一轮 wait 可能立刻满足。调试时要把“任务执行过”和“bit 当前仍为 1”区分开。

如果需要保留 bit 给多个任务观察，就不能简单照搬本课的自动清除策略，必须重新设计谁负责清除。

## 14. 本课最核心的结论

1. 事件组用 bit 表示多个事件状态。
2. `xEventGroupSetBits()` 设置条件，`xEventGroupWaitBits()` 等待条件。
3. `pdTRUE` 清 bit 和 `pdTRUE` 等所有 bit 是两个不同参数。
4. PC13 表示 A+B 都满足，不表示单个事件。
5. 事件组不适合传递大块数据。
6. 事件组和队列集语义不同。
7. reg/hal 差异在 GPIO，不在事件组机制。
8. bit 定义、清除策略和等待策略必须写清楚。

## 15. 建议你现在怎么读这节课

先把 BIT_A、BIT_B 画成两个灯：PA1 亮代表 A 来过，PA2 亮代表 B 来过。PC13 是“两个都来过”的结果灯。然后再看 `xEventGroupWaitBits()` 四个关键参数，尤其两个 `pdTRUE`。

## 16. 扩展练习

1. 把 wait-all 改成 wait-any，观察 PC13 变快。
2. 把 clearOnExit 改成 `pdFALSE`，观察旧 bit 是否重复满足。
3. 增加 BIT_C 和第三个任务，等待三个条件。
4. 改变 task_a/task_b 周期，观察 PC13 节奏。
5. 在 wait_task 中记录返回的 bit 值。

## 17. 下一课预告

- 上一课：[53_freertos_queue_set](../53_freertos_queue_set/README.md)
- 下一课：[55_freertos_task_notification](../55_freertos_task_notification/README.md)
