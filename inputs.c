#include "gate.h"
#include "basic_io.h"
#include "tm4c123gh6pm.h"

typedef struct {
    uint8_t    raw_prev;
    uint8_t    stable;
    uint8_t    just_changed;   /* set when stable flipped this cycle */
    TickType_t press_tick;
} BtnState_t;

static inline uint8_t Read_PF4(void) { return (GPIO_PORTF_DATA_R & BTN_DRV_OPEN_PIN)  == 0; }
static inline uint8_t Read_PF0(void) { return (GPIO_PORTF_DATA_R & BTN_DRV_CLOSE_PIN) == 0; }
static inline uint8_t Read_PE1(void) { return (GPIO_PORTE_DATA_R & BTN_SEC_OPEN_PIN)  != 0; }
static inline uint8_t Read_PB0(void) { return (GPIO_PORTB_DATA_R & BTN_SEC_CLOSE_PIN) != 0; }
static inline uint8_t Read_PB1(void) { return (GPIO_PORTB_DATA_R & BTN_OPEN_LIM_PIN)  != 0; }
static inline uint8_t Read_PD0(void) { return (GPIO_PORTD_DATA_R & BTN_CLOSE_LIM_PIN) != 0; }
static inline uint8_t Read_PD1(void) { return (GPIO_PORTD_DATA_R & BTN_OBSTACLE_PIN)  != 0; }

/* Step 1: debounce only — update stable state, do NOT send events yet. */
static void debounce_btn(BtnState_t *s, uint8_t raw, TickType_t now)
{
    s->just_changed = 0;
    if (raw == s->raw_prev && raw != s->stable) {
        s->stable      = raw;
        s->just_changed = 1;
        if (raw) s->press_tick = now;
    }
    s->raw_prev = raw;
}

/* Step 2: emit — only called if the panel is NOT in conflict. */
static void emit_btn_event(BtnState_t *s, ButtonId_t id, TickType_t now)
{
    if (!s->just_changed) return;
    GateEvent_t ev;
    if (s->stable) {
        ev.kind    = EVT_BTN_PRESSED;
        ev.id      = (uint8_t)id;
        ev.hold_ms = 0;
    } else {
        uint32_t held = (uint32_t)((now - s->press_tick) * portTICK_PERIOD_MS);
        ev.kind    = EVT_BTN_RELEASED;
        ev.id      = (uint8_t)id;
        ev.hold_ms = (held > 0xFFFFu) ? 0xFFFFu : (uint16_t)held;
    }
    xQueueSend(xQueueGateEvents, &ev, 0);
}

/* Limit / obstacle: edge-only, unchanged. */
static void process_edge_button(BtnState_t *s, uint8_t raw,
                                EventKind_t kind, SemaphoreHandle_t sem)
{
    if (raw == s->raw_prev && raw != s->stable) {
        s->stable = raw;
        if (raw) {
            GateEvent_t ev = { kind, 0, 0 };
            xQueueSend(xQueueGateEvents, &ev, 0);
            if (sem != NULL) xSemaphoreGive(sem);
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

    uint8_t drv_conflict_sent = 0;
    uint8_t sec_conflict_sent = 0;

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        /* ── 1. Debounce all panel buttons (no events yet) ── */
        debounce_btn(&panel[BTN_DRV_OPEN],  Read_PF4(), now);
        debounce_btn(&panel[BTN_DRV_CLOSE], Read_PF0(), now);
        debounce_btn(&panel[BTN_SEC_OPEN],  Read_PE1(), now);
        debounce_btn(&panel[BTN_SEC_CLOSE], Read_PB0(), now);

        /* ── 2. Check conflicts BEFORE emitting any press events ── */
        uint8_t drv_both = panel[BTN_DRV_OPEN].stable && panel[BTN_DRV_CLOSE].stable;
        uint8_t sec_both = panel[BTN_SEC_OPEN].stable && panel[BTN_SEC_CLOSE].stable;

        if (drv_both) {
            if (!drv_conflict_sent) {
                vPrintString("[Input] Driver CONFLICT\n");
                GateEvent_t ev = { EVT_PANEL_CONFLICT, (uint8_t)PANEL_DRIVER, 0 };
                xQueueSend(xQueueGateEvents, &ev, 0);
                drv_conflict_sent = 1;
            }
        } else {
            drv_conflict_sent = 0;
            /* ── 3. Emit individual events only when no conflict ── */
            emit_btn_event(&panel[BTN_DRV_OPEN],  BTN_DRV_OPEN,  now);
            emit_btn_event(&panel[BTN_DRV_CLOSE], BTN_DRV_CLOSE, now);
        }

        if (sec_both) {
            if (!sec_conflict_sent) {
                vPrintString("[Input] Security CONFLICT\n");
                GateEvent_t ev = { EVT_PANEL_CONFLICT, (uint8_t)PANEL_SECURITY, 0 };
                xQueueSend(xQueueGateEvents, &ev, 0);
                sec_conflict_sent = 1;
            }
        } else {
            sec_conflict_sent = 0;
            emit_btn_event(&panel[BTN_SEC_OPEN],  BTN_SEC_OPEN,  now);
            emit_btn_event(&panel[BTN_SEC_CLOSE], BTN_SEC_CLOSE, now);
        }

        /* ── Limit buttons ── */
        {
            uint8_t raw = Read_PB1();
            if (raw == lim_open.raw_prev && raw != lim_open.stable && raw)
                vPrintString("[Input] Open Limit pressed\n");
            process_edge_button(&lim_open, raw, EVT_LIMIT_OPEN, xSemLimit);
        }
        {
            uint8_t raw = Read_PD0();
            if (raw == lim_close.raw_prev && raw != lim_close.stable && raw)
                vPrintString("[Input] Closed Limit pressed\n");
            process_edge_button(&lim_close, raw, EVT_LIMIT_CLOSED, xSemLimit);
        }

        /* ── Obstacle ── */
        {
            uint8_t raw = Read_PD1();
            if (raw == obstacle.raw_prev && raw != obstacle.stable) {
                obstacle.stable = raw;
                if (raw) {
                    vPrintString("[Input] OBSTACLE pressed\n");
                    xSemaphoreGive(xSemObstacle);
                }
            }
            obstacle.raw_prev = raw;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(INPUT_PERIOD_MS));
    }
}
