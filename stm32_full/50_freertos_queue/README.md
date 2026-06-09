# 50_freertos_queue - FreeRTOS 队列

## 1. 本课到底在学什么

本课表面现象是：PC13 会按大约 700ms 的节奏翻转。这个节奏不是 consumer 任务自己延时得来的，而是 producer 任务每 700ms 往队列里放一个 1 字节事件，consumer 任务收到事件后才翻转 PC13。

真正要学的是 FreeRTOS 队列如何把两个任务解耦。producer 只负责产生事件并发送到 `g_queue`，consumer 只负责在队列上阻塞等待并处理事件。两个任务不需要互相调用函数，也不需要共享一个容易竞争的全局变量。

这节课接在中断与临界区之后。上一课已经用队列把 ISR 事件交给任务，本课把队列单独拿出来，用两个普通任务讲清楚队列容量、元素大小、发送等待时间、接收阻塞、返回值和现象之间的关系。后面的 queue set、UART 任务、ADC DMA 任务都会继续用这个思路。

## 2. 本课学习目标

学完本课你应该能做到：

- 能解释 PC13 为什么跟着 producer 的 700ms 节奏翻转。
- 能说明 `xQueueCreate(4, sizeof(uint8_t))` 的队列长度和元素大小。
- 能解释 `QueueHandle_t g_queue` 保存的是什么。
- 能拆开 `xQueueSend(g_queue, &e, 0)` 三个参数的意义。
- 能解释发送等待时间为 0 时队列满会发生什么。
- 能说明 `xQueueReceive(g_queue, &e, portMAX_DELAY)` 为什么让 consumer 不占 CPU。
- 能区分队列“传值复制”和直接传指针共享的差别。
- 能知道 reg/hal 版的队列逻辑完全相同，差别只在 PC13 翻转方式。
- 能根据 PC13 不闪、只闪一次、节奏不对、进入 hook 等现象排查。
- 能说明队列和全局变量在任务解耦上的区别。

## 3. 本课目录结构

```text
50_freertos_queue/
├── README.md
├── reg/
│   ├── platformio.ini
│   └── src/main.c
└── hal/
    ├── platformio.ini
    └── src/main.c
```

两套工程都使用 `FreeRTOS.h`、`task.h` 和 `queue.h`。`platformio.ini` 通过 `-I../../freertos` 使用统一的 `FreeRTOSConfig.h`，通过 `lib_extra_dirs = ../../lib` 使用仓库内的 FreeRTOS-Kernel。队列 API 不属于 HAL，HAL 版也直接调用 FreeRTOS 原生队列 API。

## 4. 实验硬件

- 开发板：STM32F103C8T6 BluePill。
- 下载器：ST-Link。
- 系统时钟：HSE 8MHz，PLL x9，SYSCLK 72MHz。
- 主要观察点：PC13 板载 LED，由 consumer 收到队列事件后翻转。
- PA1、PA2：初始化为输出，但当前课程没有使用。
- PA0：初始化为上拉输入，但当前课程没有读取，也没有 EXTI。
- FreeRTOS tick：`configTICK_RATE_HZ = 1000`，所以 `pdMS_TO_TICKS(700)` 约等于 700 tick。
- FreeRTOS heap：`configTOTAL_HEAP_SIZE = 12 * 1024`，队列控制块、队列存储区和任务栈都从这里分配。

本课没有串口、没有按键触发、没有中断发送队列。若 PC13 不翻，先查任务和队列，不要去查 PA0 EXTI。

## 5. 先建立一个最基本的脑图

本课完整链路是：

```text
复位启动
  -> 配 72MHz 时钟
  -> 配 PC13/PA1/PA2 输出，PA0 上拉输入
  -> 创建队列 g_queue，长度 4，每个元素 1 字节
  -> 创建 producer，优先级 2
  -> 创建 consumer，优先级 1
  -> 启动调度器
  -> producer 定义 uint8_t e = 1
  -> producer 调用 xQueueSend(g_queue, &e, 0)
  -> producer vTaskDelay(700ms) 阻塞
  -> consumer 在 xQueueReceive(portMAX_DELAY) 上被唤醒
  -> consumer 收到 e 后翻转 PC13
  -> consumer 再次阻塞等待下一条队列消息
```

从现象层看，PC13 每收到一条消息翻转一次。从 RTOS 层看，队列是 producer 和 consumer 之间的中间对象：发送者不关心接收者此刻是否正在运行，接收者也不用轮询全局变量。

这条链路最容易错的地方有三个：队列创建失败、发送时队列满但等待时间为 0、接收任务因为没有消息一直阻塞。每个错误的现象都不同，后面排错会逐个拆。

## 6. 先认识本课里出现的核心名词

### 6.1 `Queue` 是什么

Queue 是 FreeRTOS 队列，中文叫队列，属于 RTOS 内核对象层。

它保存一组固定大小的元素，任务可以把数据复制进去，另一个任务再复制出来。队列控制的是任务之间的数据缓冲、阻塞等待和唤醒行为，不是 GPIO 硬件。

本课需要队列，是因为 producer 和 consumer 不直接互相调用。若队列创建失败，两个任务之间没有通信通道，PC13 不会按预期翻转。

### 6.2 `QueueHandle_t` 是什么

`QueueHandle_t` 是队列句柄类型，属于 FreeRTOS C API 层。

`static QueueHandle_t g_queue;` 保存 `xQueueCreate()` 返回的队列对象引用。后续 `xQueueSend()` 和 `xQueueReceive()` 都要靠这个句柄找到同一个队列。

如果句柄是 NULL，说明队列没有创建成功。继续使用 NULL 句柄会导致断言、异常或不可预期行为。

### 6.3 `xQueueCreate()` 是什么

`xQueueCreate()` 是创建队列 API，属于 FreeRTOS 对象创建层。

本课 `xQueueCreate(4, sizeof(uint8_t))` 创建长度 4、元素大小 1 字节的队列。长度 4 表示最多能缓存 4 个还没被 consumer 取走的事件；元素大小 1 表示每次发送复制 1 个 `uint8_t`。

它需要从 FreeRTOS heap 分配内存。heap 不足时返回 NULL，源码会进入关中断死循环。

### 6.4 `uint8_t e = 1` 是什么

`e` 是 producer 要发送的事件值，属于 C 语言数据层。

当前值只有 1，表示“来了一个事件”。consumer 收到后没有根据值分支，只要收到就翻转 PC13。这里队列主要演示事件通知和任务解耦，不演示复杂数据包。

如果后续要传不同命令，可以把 `uint8_t` 改成结构体，但队列元素大小也必须同步改变。

### 6.5 `xQueueSend()` 是什么

`xQueueSend()` 是任务上下文队列发送 API，属于 FreeRTOS 队列操作层。

源码里 `xQueueSend(g_queue, &e, 0)` 表示把 `e` 的值复制到队列尾部，若队列满则不等待，立即返回失败。第三个参数 0 是等待 tick 数，不是发送数据值。

本课 producer 没检查返回值，这是教学上要特别指出的风险：如果队列满，事件会被丢掉，PC13 翻转次数会少于发送次数。

### 6.6 `xQueueReceive()` 是什么

`xQueueReceive()` 是任务上下文队列接收 API。

consumer 调用 `xQueueReceive(g_queue, &e, portMAX_DELAY)`。队列为空时，consumer 进入阻塞态，不占 CPU；队列有数据时，内核把元素复制到 `e`，函数返回 `pdPASS`。

如果队列一直没有消息，consumer 会一直阻塞，PC13 不翻转，但系统不一定死机。

### 6.7 `portMAX_DELAY` 是什么

`portMAX_DELAY` 是 FreeRTOS 表示最长等待的特殊值，属于 RTOS 时间等待层。

在本课接收队列时，它表示 consumer 愿意一直等到消息到来。配合 `INCLUDE_vTaskSuspend = 1` 时，FreeRTOS 可以把这种等待当作无限等待处理。

如果改成有限等待，接收任务需要处理超时分支。当前源码没有超时分支，因为教学重点是阻塞等待消息。

### 6.8 `pdPASS` 是什么

`pdPASS` 是 FreeRTOS 成功返回值。

consumer 只有在 `xQueueReceive()` 返回 `pdPASS` 时才翻转 PC13。这个判断能避免超时或失败时错误处理旧数据。

如果你忽略返回值，在有限等待场景下可能处理未更新的变量。

### 6.9 `producer` 是什么

producer 是生产者任务，属于应用任务层。

它优先级 2，每 700ms 尝试发送一次 `e=1`。发送后调用 `vTaskDelay(pdMS_TO_TICKS(700))` 进入阻塞态，让 consumer 和空闲任务有机会运行。

如果 producer 没创建成功，队列永远没有消息，consumer 会一直阻塞。

### 6.10 `consumer` 是什么

consumer 是消费者任务，属于应用任务层。

它优先级 1，循环等待队列消息。收到消息后翻转 PC13，然后立即回到队列等待。它的节奏由 producer 的发送节奏决定，而不是自己延时决定。

如果 consumer 没创建成功，队列可能积累消息直到满，PC13 不会翻转。

### 6.11 `队列满` 是什么

队列满表示队列里已经有 4 个未取走元素，再发送就没有空间。

本课发送等待时间为 0，所以满时 `xQueueSend()` 会立即失败，不会阻塞 producer。因为源码没有检查返回值，丢事件不会直接显示出来，只会表现为 consumer 少收到消息。

在真实工程中，发送端应检查返回值，或合理设置等待时间、队列长度和消费速度。

### 6.12 `队列空` 是什么

队列空表示没有可接收元素。

consumer 在空队列上用 `portMAX_DELAY` 等待，会进入阻塞态。队列空不是错误，它是生产者还没产生事件时的正常状态。

如果队列一直空，现象是 PC13 一直不翻转。要查 producer 是否运行、发送是否成功。

### 6.13 `任务解耦` 是什么

任务解耦是软件工程层概念。

producer 不直接调用 `led_toggle_pc13()`，consumer 也不关心 producer 的延时细节。队列把“产生事件”和“处理事件”分成两个任务，使任务职责更清晰。

如果用全局变量轮询，可能需要临界区、状态清除和轮询延时，逻辑更容易乱。

### 6.14 `PC13` 在本课做什么

PC13 是 GPIO 输出层的现象证据。

寄存器版 consumer 收到消息后调用 `led_toggle_pc13()`；HAL 版调用 `HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13)`。PC13 翻转说明 consumer 至少收到并处理了一条队列消息。

PC13 不翻不一定是 GPIO 错，也可能是队列没创建、producer 没发送、consumer 没接收。

### 6.15 `heap_4` 和队列有什么关系

FreeRTOS 动态队列对象从内核 heap 中分配。

`configTOTAL_HEAP_SIZE = 12 * 1024`，队列控制块、队列存储区、任务 TCB 和任务栈都消耗这块内存。对象越多、栈越大，越容易创建失败。

本课实现了 malloc failed hook，heap 不足时会停住方便调试。

### 6.16 `queue.h` 是什么

`queue.h` 是 FreeRTOS 队列 API 头文件，属于 C 接口层。

源码在任务和 hook 后包含它，提供 `QueueHandle_t`、`xQueueCreate()`、`xQueueSend()`、`xQueueReceive()` 等声明。没有它，队列代码不能正确编译。

## 7. 寄存器版代码逐步讲解

### 7.1 头文件和模块边界

寄存器版先包含 CMSIS 和 FreeRTOS 任务头文件，后面再包含 `queue.h`。

`stm32f1xx.h` 管 RCC/GPIO/FLASH，`task.h` 管任务创建和延时，`queue.h` 管队列。每个头文件对应一个层次，不要把队列当成 STM32 外设。

### 7.2 时钟初始化

`system_clock_72mhz_init()` 配置 Flash 等待周期、HSE、PLL x9、APB1 二分频，并切换 SYSCLK 到 PLL。

这保证 `configCPU_CLOCK_HZ = 72000000` 与实际一致，producer 的 700ms 延时才准确。

### 7.3 GPIO 初始化

`gpio_init()` 打开 GPIOC、GPIOA、AFIO 时钟，配置 PC13、PA1、PA2 输出，PA0 上拉输入。

本课只用 PC13 显示 consumer 处理事件。PA1/PA2/PA0 是模板化初始化遗留的可用资源，当前任务逻辑没有使用它们。

### 7.4 `led_toggle_pc13()` 的硬件后果

函数读取 `GPIOC->ODR` 判断当前 PC13 输出状态，然后写 `BRR` 或 `BSRR` 翻转。

这个函数只在 consumer 收到消息后调用，所以 PC13 翻转频率间接证明队列收发是否正常。

### 7.5 hook 函数

malloc failed hook 和 stack overflow hook 都关中断后死循环。

队列创建、任务创建都依赖 heap；任务执行依赖各自栈。如果程序停住，调试器要先看是否进入 hook。

### 7.6 创建队列

main 中先执行 `g_queue = xQueueCreate(4, sizeof(uint8_t));`。

这一步必须在任务使用队列前完成。若返回 NULL，后面任务即使创建成功也不能安全收发。

### 7.7 创建 producer 和 consumer

main 创建 producer，栈 128，优先级 2；创建 consumer，栈 128，优先级 1。

producer 优先级更高，但它每次发送后会延时 700ms，所以不会长期压住 consumer。consumer 被队列消息唤醒后能处理事件。

### 7.8 创建失败检查

`if(g_queue==NULL || ok!=pdPASS)` 统一检查队列和任务创建结果。

这比盲目启动调度器安全。若队列或任务对象不存在，系统停在明确位置，而不是运行到后面随机出错。

### 7.9 producer 的发送语句

`xQueueSend(g_queue, &e, 0)` 把 `e` 的内容复制到队列。

第三个参数 0 表示队列满时不等。由于源码丢弃返回值，读代码时要知道：这不是保证发送成功，只是尝试发送。

### 7.10 producer 的延时语句

`vTaskDelay(pdMS_TO_TICKS(700))` 让 producer 阻塞约 700ms。

这决定队列事件产生节奏。若改成 100ms，PC13 理论上会更快翻转；若删掉延时，队列可能很快满。

### 7.11 consumer 的接收语句

consumer 循环里定义 `uint8_t e`，再调用 `xQueueReceive()`。

接收成功后，队列元素被复制到 `e`。当前代码没有使用 `e` 的值分支，但保留变量能说明队列确实在传数据，而不只是发通知。

### 7.12 consumer 为什么不用延时

consumer 的等待由队列完成。

当队列空时，它阻塞；当消息来时，它运行。这比固定周期轮询更省 CPU，也更及时。

### 7.13 `vTaskStartScheduler()` 后的运行顺序

调度器启动后，producer 优先级 2，通常先运行并发送一条消息，然后阻塞。

consumer 因队列有消息而运行，翻转 PC13，再回到接收阻塞。之后整个系统按 producer 的 700ms 周期重复。

### 7.14 当前课程没有 FromISR

本课所有队列操作都在任务上下文，使用 `xQueueSend()` 和 `xQueueReceive()`。

没有中断发送队列，所以不使用 `xQueueSendFromISR()`。这和上一课不同，文档必须分清 API 场景。

## 8. HAL 版代码逐步讲解

### 8.1 `HAL_Init()` 的边界

HAL 版 main 先调用 `HAL_Init()`，再配置时钟和 GPIO。

它不创建 FreeRTOS 队列，也不改变队列语义。队列仍由 `xQueueCreate()` 创建。

### 8.2 HAL RCC 配置

`RCC_OscInitTypeDef` 配 HSE、PLL 源、PLL x9；`RCC_ClkInitTypeDef` 配 SYSCLK、HCLK、PCLK1、PCLK2。

这些字段对应寄存器版 RCC/FLASH 配置，保证 tick 时间基准正确。

### 8.3 HAL GPIO 配置

HAL 版用 `GPIO_InitTypeDef` 配 PC13 输出、PA1/PA2 输出、PA0 上拉输入。

PC13 初始写高电平。底层对应 F1 的 CRH/CRL 和 ODR 配置。

### 8.4 HAL 版队列创建

HAL 版同样 `g_queue = xQueueCreate(4, sizeof(uint8_t))`。

这是 FreeRTOS API，不属于 HAL。HAL 没有替代它的队列对象。

### 8.5 HAL 版 producer

producer 代码和寄存器版一致：发送 `uint8_t e=1`，再延时 700ms。

这说明任务间通信逻辑与硬件访问层无关。

### 8.6 HAL 版 consumer

consumer 接收队列消息后调用 `HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13)`。

这是 HAL 版和寄存器版最主要的差异：一个用 HAL 翻转，一个用寄存器函数翻转。队列接收行为相同。

### 8.7 HAL 版也要检查创建结果

HAL 版同样检查 `g_queue == NULL || ok != pdPASS`。

因为队列和任务从 FreeRTOS heap 分配，HAL 不会自动解决内存不足。

### 8.8 HAL 版不使用 `HAL_Delay()`

producer 使用 `vTaskDelay()`，consumer 使用队列阻塞。

RTOS 任务里的等待应由 FreeRTOS 管理，才能让任务进入阻塞态并让出 CPU。`HAL_Delay()` 不是本课的等待模型。

### 8.9 HAL 版调试仍可看寄存器

即使用 HAL 翻转 PC13，底层仍改变 GPIOC 输出寄存器。

可以观察 GPIOC ODR13，也可以在 `HAL_GPIO_TogglePin()` 行打断点，确认 consumer 是否收到消息。

### 8.10 HAL 和队列返回值

HAL 版 producer 同样没有检查 `xQueueSend()` 返回值。

这不是 HAL 的问题，而是应用层选择。真实工程里建议记录发送失败次数，尤其队列长度较短或 producer 频率较高时。

## 9. 两个版本真正应该怎么学

寄存器版让你看清 PC13 翻转的底层动作，HAL 版让你看清 GPIO 初始化和翻转的工程写法。两个版本的 FreeRTOS 队列路径完全一致：创建队列、producer 发送、consumer 阻塞接收、收到后处理。

学习这课时，把 GPIO 当成现象，把队列当成主角。PC13 翻转只是证明队列消息被消费；真正要理解的是数据如何从 producer 的局部变量复制到队列，再复制到 consumer 的局部变量。

## 10. 检验问题清单

### 10.1 本课队列长度是多少？

**答**：长度是 4，表示最多缓存 4 个未被接收的 `uint8_t` 元素。

### 10.2 `sizeof(uint8_t)` 控制什么？

**答**：控制每个队列元素复制的字节数。本课每条消息 1 字节。

### 10.3 `xQueueSend(..., 0)` 的 0 表示什么？

**答**：表示队列满时不等待，立即返回失败。它不是发送的数据值。

### 10.4 producer 为什么要延时 700ms？

**答**：它决定事件产生周期，也让 producer 阻塞，把 CPU 交给 consumer 或空闲任务。

### 10.5 consumer 为什么不需要自己延时？

**答**：它在队列空时阻塞等待，队列消息到来时才运行，天然由队列驱动。

### 10.6 PC13 翻转说明什么？

**答**：说明 consumer 至少成功接收并处理了一条队列消息。

### 10.7 队列满时当前源码会怎样？

**答**：`xQueueSend()` 会失败并立即返回，但源码没检查返回值，所以事件会静默丢失。

### 10.8 本课能在 ISR 里用这些 API 吗？

**答**：本课没有 ISR。任务上下文用 `xQueueSend()`；中断上下文应使用 `xQueueSendFromISR()`。

### 10.9 HAL 版有没有不同的队列 API？

**答**：没有。HAL 只封装 STM32 外设，队列仍使用 FreeRTOS API。

### 10.10 队列和全局变量相比有什么好处？

**答**：队列自带缓冲、复制、阻塞和唤醒机制，可以让生产者和消费者解耦，减少轮询和共享变量竞争。

## 11. 工程实现步骤

### 11.1 需求分析

目标是用一个 producer 和一个 consumer 演示 FreeRTOS 队列的任务间通信。

### 11.2 硬件核查

确认 PC13 LED 可用，HSE 时钟正常，ST-Link 可下载。PA0 不参与本课触发，不需要按键。

### 11.3 寄存器路线

配置时钟和 GPIO，创建队列和两个任务。producer 用 `xQueueSend()`，consumer 用 `xQueueReceive()`，收到后寄存器翻转 PC13。

### 11.4 HAL 路线

用 HAL 配置时钟和 GPIO，队列和任务代码保持 FreeRTOS 原生 API。consumer 用 HAL 翻转 PC13。

### 11.5 工程思维

队列适合传递小消息或事件。要根据生产速度、消费速度和可接受延迟选择队列长度，并检查发送/接收返回值。

### 11.6 常见工程陷阱

不检查队列创建结果、不检查发送失败、队列元素大小和实际数据不匹配、把 ISR API 和任务 API 混用、用队列传局部指针但对象生命周期不够，都是常见问题。

## 12. 运行现象

正常情况下，PC13 大约每 700ms 翻转一次。producer 每 700ms 发送一条 `uint8_t` 消息，consumer 收到后翻转 LED。

调试器中可以观察 `g_queue` 是否非 NULL，也可以在 producer 的 `xQueueSend()` 和 consumer 的 `xQueueReceive()` 返回后打断点。若 consumer 断点周期命中，说明队列通信正常。

## 13. 常见问题排查

### 13.1 PC13 完全不闪

先查 `g_queue` 是否为 NULL、两个 `xTaskCreate()` 是否返回 `pdPASS`、是否进入 malloc failed 或 stack overflow hook。再查 PC13 GPIO 配置。

### 13.2 producer 在跑但 consumer 不响应

检查 `xQueueSend()` 返回值是否成功，队列句柄是否一致，consumer 是否阻塞在同一个 `g_queue` 上。

### 13.3 consumer 一直阻塞

说明队列没有消息到来。查 producer 是否创建成功、是否执行到发送语句、发送是否失败。

### 13.4 PC13 节奏不是 700ms

检查 `pdMS_TO_TICKS(700)`、`configTICK_RATE_HZ`、系统时钟 72MHz 是否正确。

### 13.5 队列创建失败

检查 FreeRTOS heap、任务栈大小和是否创建了过多对象。队列创建失败会让 `g_queue == NULL`。

### 13.6 事件丢失

如果 producer 发送太快、consumer 处理太慢，长度 4 的队列可能满。当前发送等待时间为 0 且未检查返回值，丢事件不会自动报警。

### 13.7 HAL 版 PC13 不翻

先确认 consumer 是否收到消息。若收到但不翻，再查 `HAL_GPIO_Init()`、`HAL_GPIO_TogglePin()` 和 PC13 硬件。

### 13.8 误用 FromISR API

本课在任务上下文，不需要 FromISR API。若后续搬到中断里发送，必须改用 `xQueueSendFromISR()` 并处理 yield。

### 13.9 想证明队列传的是复制值

可以在 producer 发送后修改局部变量 `e`，再观察 consumer 收到的值。队列发送会按元素大小复制数据，不是保存 producer 局部变量地址。本课元素只有 1 字节，这个特性不明显；换成结构体后更值得验证。

## 14. 本课最核心的结论

1. 队列是 FreeRTOS 内核对象，用来在任务之间复制数据。
2. `xQueueCreate(4, sizeof(uint8_t))` 决定容量和单条消息大小。
3. producer 发送消息，consumer 阻塞接收，两个任务通过队列解耦。
4. `xQueueSend(..., 0)` 队列满时立即失败，工程中应检查返回值。
5. `xQueueReceive(..., portMAX_DELAY)` 让任务无消息时阻塞，不浪费 CPU。
6. PC13 翻转是消息被消费的现象证据。
7. reg/hal 差异只在 GPIO 翻转方式，不在队列语义。
8. 队列创建失败、任务创建失败、发送失败和 GPIO 错误要分层排查。

## 15. 建议你现在怎么读这节课

第一遍只看第 5 章，把 producer、队列、consumer、PC13 四个节点连起来。第二遍看 `xQueueCreate()` 的两个参数，确认队列到底能存什么。第三遍看 producer 的发送等待时间 0，理解它为什么可能丢消息。最后对比 reg/hal 的 PC13 翻转方式。

调试时最值得看的不是 LED，而是两个断点：producer 发送后、consumer 接收成功后。两个断点都周期命中，队列链路就通了。

## 16. 扩展练习

1. 把队列长度从 4 改成 1，观察快速发送时更容易满。
2. 把 producer 延时改成 100ms，再在 consumer 里加处理延时，观察丢事件风险。
3. 检查 `xQueueSend()` 返回值，统计发送失败次数。
4. 把 `uint8_t` 改成结构体，传递命令 ID 和计数值。
5. 把接收等待从 `portMAX_DELAY` 改成 1000ms，增加超时分支。

## 17. 下一课预告

- 上一课：[49_freertos_interrupt_critical](../49_freertos_interrupt_critical/README.md)
- 下一课：[51_freertos_semaphore_binary_counting](../51_freertos_semaphore_binary_counting/README.md)
