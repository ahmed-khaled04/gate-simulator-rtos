#include "gate.h"

/*
 * Gate Control Task: consumes events from xQueueGateEvents and runs the state
 * machine in PDF §7. Tap-vs-hold is decided here from EVT_BTN_RELEASED.hold_ms;
 * driver/security priority is enforced via current_owner.
 */

GateState_t g_gate_state = GATE_IDLE_CLOSED;

typedef enum { SUB_AUTO = 0, SUB_MANUAL } SubMode_t;
typedef enum { OWNER_NONE = 0, OWNER_DRIVER, OWNER_SECURITY } Owner_t;

static SubMode_t  s_sub_mode    = SUB_AUTO;
static Owner_t    s_owner       = OWNER_NONE;
static uint8_t    s_open_pressed[2]  = {0,0};   /* [PANEL_DRIVER], [PANEL_SECURITY] */
static uint8_t    s_close_pressed[2] = {0,0};

static void notify_led(GateState_t st)
{
    if (xTaskLEDControl != NULL) {
        xTaskNotify(xTaskLEDControl, (uint32_t)st, eSetValueWithOverwrite);
    }
}

static void set_state(GateState_t st)
{
    xSemaphoreTake(xMutexState, portMAX_DELAY);
    g_gate_state = st;
    xSemaphoreGive(xMutexState);
    notify_led(st);
}

GateState_t GateState_Read(void)
{
    GateState_t s;
    xSemaphoreTake(xMutexState, portMAX_DELAY);
    s = g_gate_state;
    xSemaphoreGive(xMutexState);
    return s;
}

static Owner_t owner_of(ButtonId_t b)
{
    return (b == BTN_DRV_OPEN || b == BTN_DRV_CLOSE) ? OWNER_DRIVER : OWNER_SECURITY;
}
static uint8_t is_open_button(ButtonId_t b)
{
    return (b == BTN_DRV_OPEN || b == BTN_SEC_OPEN);
}

static void start_opening(Owner_t who, SubMode_t mode)
{
    s_owner = who;
    s_sub_mode = mode;
    set_state(GATE_OPENING);
}

static void start_closing(Owner_t who, SubMode_t mode)
{
    s_owner = who;
    s_sub_mode = mode;
    set_state(GATE_CLOSING);
}

static void stop_midway(void)
{
    s_owner = OWNER_NONE;
    set_state(GATE_STOPPED_MIDWAY);
}

static void handle_press(ButtonId_t b)
{
    Owner_t who = owner_of(b);
    GateState_t cur = GateState_Read();

    /* Security preempts driver; driver cannot preempt active security command. */
    if (who == OWNER_DRIVER && s_owner == OWNER_SECURITY &&
        (cur == GATE_OPENING || cur == GATE_CLOSING)) {
        return;
    }

    if (is_open_button(b)) {
        if (cur == GATE_IDLE_CLOSED || cur == GATE_STOPPED_MIDWAY ||
            cur == GATE_CLOSING) {
            /* Treat press as start of motion; sub_mode tentatively MANUAL until
             * we see the release tick — release-handler will downgrade to AUTO
             * if hold_ms < threshold. */
            start_opening(who, SUB_MANUAL);
        } else if (cur == GATE_OPENING && who == OWNER_SECURITY && s_owner == OWNER_DRIVER) {
            /* Security takes ownership of the in-progress motion. */
            s_owner = OWNER_SECURITY;
            s_sub_mode = SUB_MANUAL;
        }
    } else { /* CLOSE button */
        if (cur == GATE_IDLE_OPEN || cur == GATE_STOPPED_MIDWAY ||
            cur == GATE_OPENING) {
            start_closing(who, SUB_MANUAL);
        } else if (cur == GATE_CLOSING && who == OWNER_SECURITY && s_owner == OWNER_DRIVER) {
            s_owner = OWNER_SECURITY;
            s_sub_mode = SUB_MANUAL;
        }
    }
}

static void handle_release(ButtonId_t b, uint16_t hold_ms)
{
    Owner_t who = owner_of(b);
    GateState_t cur = GateState_Read();

    if (cur != GATE_OPENING && cur != GATE_CLOSING) return;
    if (s_owner != who) return;   /* a different owner is running the show */

    /* Direction sanity: only the matching button's release affects motion. */
    if (cur == GATE_OPENING && !is_open_button(b)) return;
    if (cur == GATE_CLOSING &&  is_open_button(b)) return;

    if (hold_ms < HOLD_THRESH_MS) {
        /* Tap → AUTO mode: motion continues; release does NOT stop the gate. */
        s_sub_mode = SUB_AUTO;
    } else {
        /* Held → MANUAL: stop now. */
        stop_midway();
    }
}

static void handle_limit(EventKind_t ev)
{
    GateState_t cur = GateState_Read();
    if (ev == EVT_LIMIT_OPEN && cur == GATE_OPENING) {
        s_owner = OWNER_NONE;
        set_state(GATE_IDLE_OPEN);
    } else if (ev == EVT_LIMIT_CLOSED && cur == GATE_CLOSING) {
        s_owner = OWNER_NONE;
        set_state(GATE_IDLE_CLOSED);
    }
    /* Wrong limit pressed during motion → ignored (TC-12). */
}

static void handle_conflict(void)
{
    GateState_t cur = GateState_Read();
    if (cur == GATE_OPENING || cur == GATE_CLOSING || cur == GATE_STOPPED_MIDWAY) {
        stop_midway();
    } else if (cur == GATE_IDLE_OPEN || cur == GATE_IDLE_CLOSED) {
        /* Spec: conflicting input results in safe stop — already at rest. */
        stop_midway();
    }
}

void vGateControlTask(void *pv)
{
    (void)pv;
    GateEvent_t ev;

    /* Publish initial state to LED Control. */
    notify_led(g_gate_state);

    for (;;) {
        if (xQueueReceive(xQueueGateEvents, &ev, portMAX_DELAY) != pdTRUE) continue;

        /* Track raw press/release flags for cross-checks (e.g., conflict resolution). */
        if (ev.kind == EVT_BTN_PRESSED || ev.kind == EVT_BTN_RELEASED) {
            ButtonId_t b = (ButtonId_t)ev.id;
            uint8_t down = (ev.kind == EVT_BTN_PRESSED);
            uint8_t panel = (b == BTN_DRV_OPEN || b == BTN_DRV_CLOSE) ? 0 : 1;
            if (b == BTN_DRV_OPEN || b == BTN_SEC_OPEN)  s_open_pressed[panel]  = down;
            if (b == BTN_DRV_CLOSE|| b == BTN_SEC_CLOSE) s_close_pressed[panel] = down;
        }

        switch (ev.kind) {
            case EVT_BTN_PRESSED:
                /* Suppress press-handling if its panel is currently in conflict. */
                {
                    uint8_t panel = (ev.id == BTN_DRV_OPEN || ev.id == BTN_DRV_CLOSE) ? 0 : 1;
                    if (s_open_pressed[panel] && s_close_pressed[panel]) break;
                }
                handle_press((ButtonId_t)ev.id);
                break;

            case EVT_BTN_RELEASED:
                handle_release((ButtonId_t)ev.id, ev.hold_ms);
                break;

            case EVT_LIMIT_OPEN:
            case EVT_LIMIT_CLOSED:
                handle_limit(ev.kind);
                /* Also drain xSemLimit if set (avoid stale signal). */
                xSemaphoreTake(xSemLimit, 0);
                break;

            case EVT_PANEL_CONFLICT:
                handle_conflict();
                break;
        }
    }
}
