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
#include "queue.h"
static QueueHandle_t g_queue; static volatile uint32_t g_shared;
static void event_task(void *arg){ uint8_t e; (void)arg; while(1){ if(xQueueReceive(g_queue,&e,portMAX_DELAY)==pdPASS){ taskENTER_CRITICAL(); g_shared++; taskEXIT_CRITICAL(); led_toggle_pc13(); } } }
void EXTI0_IRQHandler(void){ BaseType_t w=pdFALSE; if((EXTI->PR & EXTI_PR_PR0)!=0U){ EXTI->PR=EXTI_PR_PR0; uint8_t e=1U; xQueueSendFromISR(g_queue,&e,&w); } portYIELD_FROM_ISR(w); }
int main(void){ system_clock_72mhz_init(); gpio_init(); AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI0; EXTI->IMR |= EXTI_IMR_MR0; EXTI->FTSR |= EXTI_FTSR_TR0; NVIC_SetPriority(EXTI0_IRQn,6); NVIC_EnableIRQ(EXTI0_IRQn); g_queue=xQueueCreate(4,sizeof(uint8_t)); if(g_queue==NULL || xTaskCreate(event_task,"evt",160,NULL,2,NULL)!=pdPASS){taskDISABLE_INTERRUPTS();while(1){}} vTaskStartScheduler(); while(1){} }
