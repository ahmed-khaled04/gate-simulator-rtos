#ifndef GATE_H
#define GATE_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "basic_io.h"

/* Tunables */
#define HOLD_THRESH_MS      400u
#define REVERSE_MS          500u
#define DEBOUNCE_TICKS      2u
#define INPUT_PERIOD_MS     10u
#define BLINK_HALF_MS       100u

/* RGB LED pin masks (Port F) */
#define LED_RED             (1u << 1)
#define LED_BLUE            (1u << 2)
#define LED_GREEN           (1u << 3)
#define LED_MASK            (LED_RED | LED_BLUE | LED_GREEN)

/* Button pin masks on their respective ports */
#define BTN_DRV_OPEN_PIN    (1u << 4)   /* PF4, active-low  */
#define BTN_DRV_CLOSE_PIN   (1u << 0)   /* PE0, active-high */
#define BTN_SEC_OPEN_PIN    (1u << 1)   /* PE1, active-high */
#define BTN_SEC_CLOSE_PIN   (1u << 0)   /* PB0, active-high */
#define BTN_OPEN_LIM_PIN    (1u << 1)   /* PB1, active-high */
#define BTN_CLOSE_LIM_PIN   (1u << 0)   /* PD0, active-high */
#define BTN_OBSTACLE_PIN    (1u << 1)   /* PD1, active-high */

typedef enum {
    BTN_DRV_OPEN = 0,
    BTN_DRV_CLOSE,
    BTN_SEC_OPEN,
    BTN_SEC_CLOSE,
    BTN_COUNT_PANEL          /* sentinel: only the four panel buttons have hold-timing */
} ButtonId_t;

typedef enum {
    PANEL_DRIVER = 0,
    PANEL_SECURITY
} PanelId_t;

typedef enum {
    GATE_IDLE_CLOSED = 0,
    GATE_IDLE_OPEN,
    GATE_OPENING,
    GATE_CLOSING,
    GATE_STOPPED_MIDWAY,
    GATE_REVERSING
} GateState_t;

typedef enum {
    EVT_BTN_PRESSED = 0,
    EVT_BTN_RELEASED,
    EVT_LIMIT_OPEN,
    EVT_LIMIT_CLOSED,
    EVT_PANEL_CONFLICT
} EventKind_t;

typedef struct {
    EventKind_t kind;
    uint8_t     id;          /* ButtonId_t for BTN_*; PanelId_t for PANEL_CONFLICT; unused otherwise */
    uint16_t    hold_ms;     /* set on EVT_BTN_RELEASED */
} GateEvent_t;

/* IPC handles, created in main() before vTaskStartScheduler() */
extern QueueHandle_t      xQueueGateEvents;
extern SemaphoreHandle_t  xSemObstacle;
extern SemaphoreHandle_t  xSemLimit;
extern SemaphoreHandle_t  xMutexState;
extern TaskHandle_t       xTaskLEDControl;

/* Task entry points */
void vInputTask(void *pv);
void vGateControlTask(void *pv);
void vSafetyTask(void *pv);
void vLEDControlTask(void *pv);

/* Shared state — read/write only with xMutexState held */
extern GateState_t g_gate_state;

/* Helper: read state under mutex */
GateState_t GateState_Read(void);

#endif /* GATE_H */
