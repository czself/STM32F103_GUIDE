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
#include "event_groups.h"
#define BIT_A (1U<<0)
#define BIT_B (1U<<1)
static EventGroupHandle_t g_events;
static void task_a(void *arg){ (void)arg; while(1){ xEventGroupSetBits(g_events,BIT_A); led_toggle_pa1(); vTaskDelay(pdMS_TO_TICKS(500)); } }
static void task_b(void *arg){ (void)arg; while(1){ xEventGroupSetBits(g_events,BIT_B); led_toggle_pa2(); vTaskDelay(pdMS_TO_TICKS(900)); } }
static void wait_task(void *arg){ (void)arg; while(1){ xEventGroupWaitBits(g_events,BIT_A|BIT_B,pdTRUE,pdTRUE,portMAX_DELAY); led_toggle_pc13(); } }
int main(void){ system_clock_72mhz_init(); gpio_init(); g_events=xEventGroupCreate(); BaseType_t ok=xTaskCreate(task_a,"a",128,NULL,1,NULL); ok &= xTaskCreate(task_b,"b",128,NULL,1,NULL); ok &= xTaskCreate(wait_task,"wait",160,NULL,2,NULL); if(g_events==NULL||ok!=pdPASS){taskDISABLE_INTERRUPTS();while(1){}} vTaskStartScheduler(); while(1){} }
