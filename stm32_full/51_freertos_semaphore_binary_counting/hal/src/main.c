#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

static void system_clock_72mhz_init(void)
{
    RCC_OscInitTypeDef osc={0}; RCC_ClkInitTypeDef clk={0};
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE; osc.HSEState=RCC_HSE_ON; osc.HSEPredivValue=RCC_HSE_PREDIV_DIV1; osc.PLL.PLLState=RCC_PLL_ON; osc.PLL.PLLSource=RCC_PLLSOURCE_HSE; osc.PLL.PLLMUL=RCC_PLL_MUL9; HAL_RCC_OscConfig(&osc);
    clk.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2; clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider=RCC_SYSCLK_DIV1; clk.APB1CLKDivider=RCC_HCLK_DIV2; clk.APB2CLKDivider=RCC_HCLK_DIV1; HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_2);
}
static void gpio_init(void)
{
    GPIO_InitTypeDef gpio={0}; __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_13; gpio.Mode=GPIO_MODE_OUTPUT_PP; gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOC,&gpio); HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
    gpio.Pin=GPIO_PIN_1|GPIO_PIN_2; gpio.Mode=GPIO_MODE_OUTPUT_PP; gpio.Speed=GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOA,&gpio);
    gpio.Pin=GPIO_PIN_0; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_PULLUP; HAL_GPIO_Init(GPIOA,&gpio);
}
void vApplicationMallocFailedHook(void){ taskDISABLE_INTERRUPTS(); while(1){} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name){ (void)task; (void)task_name; taskDISABLE_INTERRUPTS(); while(1){} }
#include "semphr.h"
static SemaphoreHandle_t g_bin,g_count;
static void giver(void *arg){ (void)arg; while(1){ xSemaphoreGive(g_bin); vTaskDelay(pdMS_TO_TICKS(1000)); } }
static void taker(void *arg){ (void)arg; while(1){ if(xSemaphoreTake(g_bin,portMAX_DELAY)==pdTRUE){ HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); } } }
static void worker(void *arg){ (void)arg; while(1){ if(xSemaphoreTake(g_count,portMAX_DELAY)==pdTRUE){ HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1); vTaskDelay(pdMS_TO_TICKS(300)); xSemaphoreGive(g_count); } } }
int main(void){ HAL_Init(); system_clock_72mhz_init(); gpio_init(); g_bin=xSemaphoreCreateBinary(); g_count=xSemaphoreCreateCounting(2,2); BaseType_t ok=xTaskCreate(giver,"give",128,NULL,2,NULL); ok &= xTaskCreate(taker,"take",128,NULL,1,NULL); ok &= xTaskCreate(worker,"work",128,NULL,1,NULL); if(g_bin==NULL||g_count==NULL||ok!=pdPASS){taskDISABLE_INTERRUPTS();while(1){}} vTaskStartScheduler(); while(1){} }
