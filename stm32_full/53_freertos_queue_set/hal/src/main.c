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
#include "queue.h"
static QueueHandle_t g_a,g_b; static QueueSetHandle_t g_set;
static void prod_a(void *arg){ uint8_t v=1; (void)arg; while(1){ xQueueSend(g_a,&v,0); vTaskDelay(pdMS_TO_TICKS(700)); } }
static void prod_b(void *arg){ uint8_t v=2; (void)arg; while(1){ xQueueSend(g_b,&v,0); vTaskDelay(pdMS_TO_TICKS(1100)); } }
static void select_task(void *arg){ QueueSetMemberHandle_t active; uint8_t v; (void)arg; while(1){ active=xQueueSelectFromSet(g_set,portMAX_DELAY); if(active==g_a){ xQueueReceive(g_a,&v,0); HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); } else if(active==g_b){ xQueueReceive(g_b,&v,0); HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_1); } } }
int main(void){ HAL_Init(); system_clock_72mhz_init(); gpio_init(); g_a=xQueueCreate(4,sizeof(uint8_t)); g_b=xQueueCreate(4,sizeof(uint8_t)); g_set=xQueueCreateSet(8); xQueueAddToSet(g_a,g_set); xQueueAddToSet(g_b,g_set); BaseType_t ok=xTaskCreate(prod_a,"pa",128,NULL,1,NULL); ok &= xTaskCreate(prod_b,"pb",128,NULL,1,NULL); ok &= xTaskCreate(select_task,"sel",160,NULL,2,NULL); if(g_a==NULL||g_b==NULL||g_set==NULL||ok!=pdPASS){taskDISABLE_INTERRUPTS();while(1){}} vTaskStartScheduler(); while(1){} }
