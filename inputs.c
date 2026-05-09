#include "gate.h"
#include "basic_io.h"
#include "tm4c123gh6pm.h"

/*
 * Input Task: 10 ms periodic poll of the seven physical buttons.
 *
 * For the four panel buttons (Driver/Security OPEN/CLOSE) we run a small per-button
 * state machine: debounce -> edge detect -> hold-timer. We emit:
 *     EVT_BTN_PRESSED  on confirmed press,
 *     EVT_BTN_RELEASED on confirmed release (with hold_ms).
 *
 * Limit buttons emit EVT_LIMIT_* on press only, plus give xSemLimit (TC-21).
 * Obstacle gives xSemObstacle on press only.
 *
 * Per-panel conflict: when both OPEN and CLOSE are simultaneously confirmed pressed
 * on the same panel, we emit EVT_PANEL_CONFLICT once until at least one is released.
 */

typedef struct {
    uint8_t  raw_prev;     /* previous raw sample, for the 2-sample debounce */
    uint8_t  stable;       /* current confirmed (debounced) state */
    TickType_t press_tick; /* tick of the last 0->1 transition */
} BtnState_t;

static inline uint8_t Read_PF4(void) { return (GPIO_PORTF_DATA_R & BTN_DRV_OPEN_PIN)  == 0; } /* active-low */
static inline uint8_t Read_PE0(void) { return (GPIO_PORTE_DATA_R & BTN_DRV_CLOSE_PIN) != 0; }
static inline uint8_t Read_PE1(void) { return (GPIO_PORTE_DATA_R & BTN_SEC_OPEN_PIN)  != 0; }
static inline uint8_t Read_PB0(void) { return (GPIO_PORTB_DATA_R & BTN_SEC_CLOSE_PIN) != 0; }
static inline uint8_t Read_PB1(void) { return (GPIO_PORTB_DATA_R & BTN_OPEN_LIM_PIN)  != 0; }
static inline uint8_t Read_PD0(void) { return (GPIO_PORTD_DATA_R & BTN_CLOSE_LIM_PIN) != 0; }
static inline uint8_t Read_PD1(void) { return (GPIO_PORTD_DATA_R & BTN_OBSTACLE_PIN)  != 0; }

static void process_panel_button(BtnState_t *s, uint8_t raw, ButtonId_t id, TickType_t now)
{
    GateEvent_t ev;
    if (raw == s->raw_prev && raw != s->stable) {
        /* Two consecutive matching samples ⇒ accept new stable level */
        s->stable = raw;
        if (raw) {
            s->press_tick = now;
            ev.kind = EVT_BTN_PRESSED;
            ev.id   = (uint8_t)id;
            ev.hold_ms = 0;
            xQueueSend(xQueueGateEvents, &ev, 0);
        } else {
            uint32_t held = (uint32_t)((now - s->press_tick) * portTICK_PERIOD_MS);
            ev.kind = EVT_BTN_RELEASED;
            ev.id   = (uint8_t)id;
            ev.hold_ms = (held > 0xFFFFu) ? 0xFFFFu : (uint16_t)held;
            xQueueSend(xQueueGateEvents, &ev, 0);
        }
    }
    s->raw_prev = raw;
}

/* Limit / obstacle: emit only on the rising edge (after debounce). */
static void process_edge_button(BtnState_t *s, uint8_t raw,
                                EventKind_t kind, SemaphoreHandle_t sem)
{
    if (raw == s->raw_prev && raw != s->stable) {
        s->stable = raw;
        if (raw) {
            if (kind != EVT_BTN_PRESSED) {  /* sentinel guard, always true here */
                GateEvent_t ev = { kind, 0, 0 };
                xQueueSend(xQueueGateEvents, &ev, 0);
            }
            if (sem != NULL) {
                xSemaphoreGive(sem);
            }
        }
    }
    s->raw_prev = raw;
}

void vInputTask(void *pv)
{
    (void)pv;
    BtnState_t panel[BTN_COUNT_PANEL] = {0};
    BtnState_t lim_open  = {0};
    BtnState_t lim_close = {0};
    BtnState_t obstacle  = {0};

    uint8_t driver_conflict_emitted   = 0;
    uint8_t security_conflict_emitted = 0;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(INPUT_PERIOD_MS);

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        process_panel_button(&panel[BTN_DRV_OPEN],  Read_PF4(), BTN_DRV_OPEN,  now);
        process_panel_button(&panel[BTN_DRV_CLOSE], Read_PE0(), BTN_DRV_CLOSE, now);
        process_panel_button(&panel[BTN_SEC_OPEN],  Read_PE1(), BTN_SEC_OPEN,  now);
        process_panel_button(&panel[BTN_SEC_CLOSE], Read_PB0(), BTN_SEC_CLOSE, now);

        if (Read_PB1() == lim_open.raw_prev  && Read_PB1() != lim_open.stable  && Read_PB1())
            vPrintString("[Input] Open Limit pressed\n");
        process_edge_button(&lim_open,  Read_PB1(), EVT_LIMIT_OPEN,   xSemLimit);

        if (Read_PD0() == lim_close.raw_prev && Read_PD0() != lim_close.stable && Read_PD0())
            vPrintString("[Input] Closed Limit pressed\n");
        process_edge_button(&lim_close, Read_PD0(), EVT_LIMIT_CLOSED, xSemLimit);

        /* Obstacle: only the semaphore — Safety Task is the consumer. */
        if (Read_PD1() == obstacle.raw_prev && Read_PD1() != obstacle.stable) {
            obstacle.stable = Read_PD1();
            if (obstacle.stable) {
                vPrintString("[Input] OBSTACLE pressed\n");
                xSemaphoreGive(xSemObstacle);
            }
        }
        obstacle.raw_prev = Read_PD1();

        /* Per-panel conflict detection (latched until one side releases). */
        uint8_t drv_both = panel[BTN_DRV_OPEN].stable && panel[BTN_DRV_CLOSE].stable;
        uint8_t sec_both = panel[BTN_SEC_OPEN].stable && panel[BTN_SEC_CLOSE].stable;

        if (drv_both && !driver_conflict_emitted) {
            vPrintString("[Input] Driver CONFLICT\n");
            GateEvent_t ev = { EVT_PANEL_CONFLICT, (uint8_t)PANEL_DRIVER, 0 };
            xQueueSend(xQueueGateEvents, &ev, 0);
            driver_conflict_emitted = 1;
        } else if (!drv_both) {
            driver_conflict_emitted = 0;
        }

        if (sec_both && !security_conflict_emitted) {
            vPrintString("[Input] Security CONFLICT\n");
            GateEvent_t ev = { EVT_PANEL_CONFLICT, (uint8_t)PANEL_SECURITY, 0 };
            xQueueSend(xQueueGateEvents, &ev, 0);
            security_conflict_emitted = 1;
        } else if (!sec_both) {
            security_conflict_emitted = 0;
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
