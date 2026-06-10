#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * 第 51 课：FreeRTOS 二值信号量和计数信号量（寄存器版）。
 *
 * 本文件故意把硬件层和 RTOS 层都摊开写：
 * - 硬件层：直接操作 RCC/GPIO 寄存器，控制 PC13 和 PA1。
 * - RTOS 层：创建二值信号量和计数信号量，让任务之间通过内核对象同步。
 *
 * 运行现象：
 * - giver_task 每 1 秒 give 一次二值信号量。
 * - taker_task 一直阻塞等待二值信号量，拿到后翻转 PC13。
 * - worker_task 占用计数信号量中的 1 个资源，翻转 PA1，300ms 后归还资源。
 */

static SemaphoreHandle_t g_binary_sem;
static SemaphoreHandle_t g_counting_sem;

static void system_clock_72mhz_init(void);
static void gpio_init(void);
static void led_toggle_pc13(void);
static void led_toggle_pa1(void);
static void stop_for_debug(void);
static void giver_task(void *argument);
static void taker_task(void *argument);
static void worker_task(void *argument);

static void system_clock_72mhz_init(void)
{
    /*
     * 72MHz 下 CPU 从 Flash 取指需要 2 个等待周期，并开启预取能让取指更稳定。
     * 如果先切到 PLL 高速再配置 Flash 等待周期，系统可能跑飞或出现随机异常。
     */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* 打开外部 8MHz HSE。PLL 要用 HSE 作为输入，所以必须先等 HSERDY 置位。 */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0U) {
    }

    /*
     * 先清掉即将配置的时钟字段，避免旧的分频或 PLL 倍频残留。
     * APB1 最高只能 36MHz，所以后面必须把 PPRE1 配成 /2。
     */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 |
                   RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL |
                   RCC_CFGR_SW);

    /* HSE 8MHz x9 = 72MHz；AHB/APB2 不分频，APB1 二分频到 36MHz。 */
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    /* 打开 PLL，并等待 PLLRDY。没有 ready 就切换 SYSCLK，会得到不稳定时钟。 */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {
    }

    /* 把系统时钟切到 PLL，并等待 SWS 确认切换完成。 */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

static void gpio_init(void)
{
    /* GPIOA/GPIOC 都挂在 APB2 总线上；不开时钟，后续 CRL/CRH 写入不会真正驱动引脚。 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;

    /* PC13 属于 8~15 号引脚，所以要配置 GPIOC->CRH 中的 MODE13/CNF13。 */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);

    /* MODE13=10 表示 2MHz 输出，CNF13=00 表示通用推挽输出。 */
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    /* BluePill 的 PC13 LED 常见为低电平点亮，初始化先输出高电平让它熄灭。 */
    GPIOC->BSRR = GPIO_BSRR_BS13;

    /* PA0 是输入按键预留位，PA1 是计数信号量观察点；PA2 只做后续课程预留。 */
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 | GPIO_CRL_CNF1 |
                    GPIO_CRL_MODE2 | GPIO_CRL_CNF2);

    /* PA0 配成上拉输入：MODE0=00，CNF0=10。上拉电平由 ODR0=1 决定。 */
    GPIOA->CRL |= GPIO_CRL_CNF0_1;
    GPIOA->BSRR = GPIO_BSRR_BS0;

    /* PA1/PA2 配成 2MHz 通用推挽输出；本课只使用 PA1。 */
    GPIOA->CRL |= GPIO_CRL_MODE1_1 | GPIO_CRL_MODE2_1;
}

static void led_toggle_pc13(void)
{
    /* 先读 ODR 判断 PC13 当前输出状态，再选择 BRR 或 BSRR 改变输出电平。 */
    if ((GPIOC->ODR & GPIO_ODR_ODR13) != 0U) {
        GPIOC->BRR = GPIO_BRR_BR13;
    } else {
        GPIOC->BSRR = GPIO_BSRR_BS13;
    }
}

static void led_toggle_pa1(void)
{
    /* PA1 是 worker_task 的观察点，翻转一次表示 worker 拿到并归还了一次计数资源。 */
    if ((GPIOA->ODR & GPIO_ODR_ODR1) != 0U) {
        GPIOA->BRR = GPIO_BRR_BR1;
    } else {
        GPIOA->BSRR = GPIO_BSRR_BS1;
    }
}

static void stop_for_debug(void)
{
    /* 初始化或 RTOS 对象创建失败后停在这里，方便调试器直接看到错误现场。 */
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationMallocFailedHook(void)
{
    /* FreeRTOS heap 分配失败时由内核调用，常见原因是 configTOTAL_HEAP_SIZE 不够。 */
    stop_for_debug();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    /* 任务栈溢出时由内核调用；参数可用于调试时定位是哪一个任务栈不够。 */
    (void)task;
    (void)task_name;
    stop_for_debug();
}

static void giver_task(void *argument)
{
    (void)argument;

    while (1) {
        /* give 二值信号量：如果 taker_task 正在阻塞等待，它会被唤醒。 */
        xSemaphoreGive(g_binary_sem);

        /* 让 giver_task 阻塞 1000ms，把 CPU 交给其他就绪任务。 */
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static void taker_task(void *argument)
{
    (void)argument;

    while (1) {
        /* portMAX_DELAY 表示一直等，直到二值信号量可用。 */
        if (xSemaphoreTake(g_binary_sem, portMAX_DELAY) == pdTRUE) {
            led_toggle_pc13();
        }
    }
}

static void worker_task(void *argument)
{
    (void)argument;

    while (1) {
        /*
         * 计数信号量表示“可同时使用的资源数量”。
         * 本课最大计数和初始计数都是 2，表示一开始有 2 份资源可用。
         */
        if (xSemaphoreTake(g_counting_sem, portMAX_DELAY) == pdTRUE) {
            led_toggle_pa1();

            /* 模拟 worker_task 占用资源 300ms。 */
            vTaskDelay(pdMS_TO_TICKS(300U));

            /* 用完资源后必须归还，否则计数会逐渐耗尽，后续任务会一直阻塞。 */
            xSemaphoreGive(g_counting_sem);
        }
    }
}

int main(void)
{
    system_clock_72mhz_init();
    gpio_init();

    /* 二值信号量只有 0/1 两种状态，适合表达“事件发生一次”。 */
    g_binary_sem = xSemaphoreCreateBinary();

    /* 计数信号量最大计数 2、初始计数 2，适合表达“有 2 份同类资源”。 */
    g_counting_sem = xSemaphoreCreateCounting(2U, 2U);

    /*
     * 分开保存每个任务创建结果，学生能直接定位哪个任务没有创建成功。
     * 不把三个返回值混在一个变量里，因为那样会让排错变得不直观。
     */
    BaseType_t giver_ok = xTaskCreate(giver_task, "give", 128U, NULL, 2U, NULL);
    BaseType_t taker_ok = xTaskCreate(taker_task, "take", 128U, NULL, 1U, NULL);
    BaseType_t worker_ok = xTaskCreate(worker_task, "work", 128U, NULL, 1U, NULL);

    if ((g_binary_sem == NULL) ||
        (g_counting_sem == NULL) ||
        (giver_ok != pdPASS) ||
        (taker_ok != pdPASS) ||
        (worker_ok != pdPASS)) {
        stop_for_debug();
    }

    /* 调度器启动后，任务开始按优先级和阻塞状态运行；正常情况下不会再返回。 */
    vTaskStartScheduler();

    stop_for_debug();
}
