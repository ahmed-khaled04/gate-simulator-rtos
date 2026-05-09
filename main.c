/*
 * Smart Parking Garage Gate System (CSE411 / CSE323 — Spring 2026)
 * Target: TM4C123GH6PM LaunchPad, FreeRTOS (CMSIS-FreeRTOS pack), native API.
 *
 * Pin map (see gate.h for masks):
 *   PF1=RED  PF2=BLUE  PF3=GREEN
 *   PF4=Driver OPEN (active-low)
 *   PE0=Driver CLOSE  PE1=Security OPEN
 *   PB0=Security CLOSE  PB1=Open Limit
 *   PD0=Closed Limit  PD1=Obstacle
 */

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "basic_io.h"
#include "tm4c123gh6pm.h"
#include "gate.h"

QueueHandle_t      xQueueGateEvents = NULL;
SemaphoreHandle_t  xSemObstacle     = NULL;
SemaphoreHandle_t  xSemLimit        = NULL;
SemaphoreHandle_t  xMutexState      = NULL;
TaskHandle_t       xTaskLEDControl  = NULL;

/* Clock-gate masks for Ports B, D, E, F */
#define RCGCGPIO_B   (1U << 1)
#define RCGCGPIO_D   (1U << 3)
#define RCGCGPIO_E   (1U << 4)
#define RCGCGPIO_F   (1U << 5)
#define RCGCGPIO_ALL (RCGCGPIO_B | RCGCGPIO_D | RCGCGPIO_E | RCGCGPIO_F)

static void GPIO_Init(void)
{
    SYSCTL_RCGCGPIO_R |= RCGCGPIO_ALL;
    while ((SYSCTL_PRGPIO_R & RCGCGPIO_ALL) != RCGCGPIO_ALL) { }

    /* Port F: RGB (PF1-3), Driver OPEN (PF4), Driver CLOSE (PF0) — all pull-up, active-low.
     * PF0 is NMI-protected and requires lock/unlock before configuration. */
    GPIO_PORTF_LOCK_R  = 0x4C4F434BU;   /* unlock */
    GPIO_PORTF_CR_R   |= BTN_DRV_CLOSE_PIN;
    GPIO_PORTF_AMSEL_R &= ~(BTN_DRV_OPEN_PIN | BTN_DRV_CLOSE_PIN | LED_MASK);
    GPIO_PORTF_PCTL_R  &= ~0x000FFFFFU;
    GPIO_PORTF_AFSEL_R &= ~(BTN_DRV_OPEN_PIN | BTN_DRV_CLOSE_PIN | LED_MASK);
    GPIO_PORTF_DIR_R   |=  LED_MASK;
    GPIO_PORTF_DIR_R   &= ~(BTN_DRV_OPEN_PIN | BTN_DRV_CLOSE_PIN);
    GPIO_PORTF_PUR_R   |=  (BTN_DRV_OPEN_PIN | BTN_DRV_CLOSE_PIN);
    GPIO_PORTF_DEN_R   |=  (BTN_DRV_OPEN_PIN | BTN_DRV_CLOSE_PIN | LED_MASK);
    GPIO_PORTF_DATA_R  &= ~LED_MASK;

    /* Port E: PE1 (Security OPEN) only — pull-down, active-high. */
    GPIO_PORTE_AMSEL_R &= ~BTN_SEC_OPEN_PIN;
    GPIO_PORTE_PCTL_R  &= ~0x000000F0U;
    GPIO_PORTE_AFSEL_R &= ~BTN_SEC_OPEN_PIN;
    GPIO_PORTE_DIR_R   &= ~BTN_SEC_OPEN_PIN;
    GPIO_PORTE_PDR_R   |=  BTN_SEC_OPEN_PIN;
    GPIO_PORTE_DEN_R   |=  BTN_SEC_OPEN_PIN;

    /* Port B: PB0 (Security CLOSE), PB1 (Open Limit), pull-down. */
    GPIO_PORTB_AMSEL_R &= ~(BTN_SEC_CLOSE_PIN | BTN_OPEN_LIM_PIN);
    GPIO_PORTB_PCTL_R  &= ~0x000000FFU;
    GPIO_PORTB_AFSEL_R &= ~(BTN_SEC_CLOSE_PIN | BTN_OPEN_LIM_PIN);
    GPIO_PORTB_DIR_R   &= ~(BTN_SEC_CLOSE_PIN | BTN_OPEN_LIM_PIN);
    GPIO_PORTB_PDR_R   |=  (BTN_SEC_CLOSE_PIN | BTN_OPEN_LIM_PIN);
    GPIO_PORTB_DEN_R   |=  (BTN_SEC_CLOSE_PIN | BTN_OPEN_LIM_PIN);

    /* Port D: PD0 (Closed Limit), PD1 (Obstacle), pull-down. */
    GPIO_PORTD_AMSEL_R &= ~(BTN_CLOSE_LIM_PIN | BTN_OBSTACLE_PIN);
    GPIO_PORTD_PCTL_R  &= ~0x000000FFU;
    GPIO_PORTD_AFSEL_R &= ~(BTN_CLOSE_LIM_PIN | BTN_OBSTACLE_PIN);
    GPIO_PORTD_DIR_R   &= ~(BTN_CLOSE_LIM_PIN | BTN_OBSTACLE_PIN);
    GPIO_PORTD_PDR_R   |=  (BTN_CLOSE_LIM_PIN | BTN_OBSTACLE_PIN);
    GPIO_PORTD_DEN_R   |=  (BTN_CLOSE_LIM_PIN | BTN_OBSTACLE_PIN);
}

int main(void)
{
    GPIO_Init();

    vPrintString("=== Smart Parking Gate System ===\n");
    vPrintString("CSE411/CSE323 Spring 2026\n");
    vPrintString("Creating tasks...\n");

    xQueueGateEvents = xQueueCreate(16, sizeof(GateEvent_t));
    xSemObstacle     = xSemaphoreCreateBinary();
    xSemLimit        = xSemaphoreCreateBinary();
    xMutexState      = xSemaphoreCreateMutex();

    configASSERT(xQueueGateEvents != NULL);
    configASSERT(xSemObstacle     != NULL);
    configASSERT(xSemLimit        != NULL);
    configASSERT(xMutexState      != NULL);

    xTaskCreate(vLEDControlTask,  "LED",    128, NULL, 2, &xTaskLEDControl);
    xTaskCreate(vGateControlTask, "Gate",   384, NULL, 2, NULL);
    xTaskCreate(vInputTask,       "Input",  256, NULL, 3, NULL);
    xTaskCreate(vSafetyTask,      "Safety", 256, NULL, 4, NULL);

    vPrintString("Starting scheduler...\n");
    vTaskStartScheduler();

    for (;;) { }   /* unreached */
}

/* FreeRTOS hooks (configCHECK_FOR_STACK_OVERFLOW=2, configUSE_MALLOC_FAILED_HOOK
 * may be set in FreeRTOSConfig.h). Provide weak fallbacks so the kernel links
 * even if the application doesn't override them elsewhere. */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    for (;;) { }
}
void vApplicationMallocFailedHook(void)
{
    for (;;) { }
}
