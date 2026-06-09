#include "stm32f1xx.h"
#include "FreeRTOS.h"
#include "task.h"

static void system_clock_72mhz_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    RCC->CR |= RCC_CR_HSEON; while ((RCC->CR & RCC_CR_HSERDY) == 0U) {}
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL | RCC_CFGR_SW);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    RCC->CR |= RCC_CR_PLLON; while ((RCC->CR & RCC_CR_PLLRDY) == 0U) {}
    RCC->CFGR |= RCC_CFGR_SW_PLL; while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}
static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13); GPIOC->CRH |= GPIO_CRH_MODE13_1; GPIOC->BSRR = GPIO_BSRR_BS13;
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 | GPIO_CRL_MODE1 | GPIO_CRL_CNF1 | GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
    GPIOA->CRL |= GPIO_CRL_CNF0_1 | GPIO_CRL_MODE1_1 | GPIO_CRL_MODE2_1; GPIOA->BSRR = GPIO_BSRR_BS0;
}
static void led_toggle_pc13(void){ if((GPIOC->ODR & GPIO_ODR_ODR13)!=0U) GPIOC->BRR=GPIO_BRR_BR13; else GPIOC->BSRR=GPIO_BSRR_BS13; }
static void led_toggle_pa1(void){ if((GPIOA->ODR & GPIO_ODR_ODR1)!=0U) GPIOA->BRR=GPIO_BRR_BR1; else GPIOA->BSRR=GPIO_BSRR_BS1; }
static void led_toggle_pa2(void){ if((GPIOA->ODR & GPIO_ODR_ODR2)!=0U) GPIOA->BRR=GPIO_BRR_BR2; else GPIOA->BSRR=GPIO_BSRR_BS2; }
void vApplicationMallocFailedHook(void){ taskDISABLE_INTERRUPTS(); while(1){} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name){ (void)task; (void)task_name; taskDISABLE_INTERRUPTS(); while(1){} }
#include "semphr.h"
static SemaphoreHandle_t g_mutex;
static void low_task(void *arg){ (void)arg; while(1){ xSemaphoreTake(g_mutex,portMAX_DELAY); led_toggle_pa1(); vTaskDelay(pdMS_TO_TICKS(600)); xSemaphoreGive(g_mutex); vTaskDelay(pdMS_TO_TICKS(300)); } }
static void mid_task(void *arg){ (void)arg; while(1){ led_toggle_pa2(); vTaskDelay(pdMS_TO_TICKS(100)); } }
static void high_task(void *arg){ (void)arg; while(1){ vTaskDelay(pdMS_TO_TICKS(200)); xSemaphoreTake(g_mutex,portMAX_DELAY); led_toggle_pc13(); xSemaphoreGive(g_mutex); } }
int main(void){ system_clock_72mhz_init(); gpio_init(); g_mutex=xSemaphoreCreateMutex(); BaseType_t ok=xTaskCreate(low_task,"low",128,NULL,1,NULL); ok &= xTaskCreate(mid_task,"mid",128,NULL,2,NULL); ok &= xTaskCreate(high_task,"high",128,NULL,3,NULL); if(g_mutex==NULL||ok!=pdPASS){taskDISABLE_INTERRUPTS();while(1){}} vTaskStartScheduler(); while(1){} }
