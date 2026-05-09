#include "gate.h"
#include "tm4c123gh6pm.h"

/*
 * LED Control Task: receives the current GateState_t via task notifications and
 * drives PF1 (RED), PF2 (BLUE), PF3 (GREEN). While in REVERSING, it blinks the
 * BLUE LED at 5 Hz; for all other states the LEDs are static.
 */

static void apply_leds(GateState_t st, uint8_t blink_phase)
{
    uint32_t mask = 0;
    switch (st) {
        case GATE_OPENING:        mask = LED_GREEN; break;
        case GATE_CLOSING:        mask = LED_RED;   break;
        case GATE_STOPPED_MIDWAY: mask = LED_BLUE;  break;
        case GATE_REVERSING:
            mask = LED_GREEN | (blink_phase ? LED_BLUE : 0u);
            break;
        case GATE_IDLE_OPEN:
        case GATE_IDLE_CLOSED:
        default:                  mask = 0;         break;
    }
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | (mask & LED_MASK);
}

void vLEDControlTask(void *pv)
{
    (void)pv;
    GateState_t st = GATE_IDLE_CLOSED;
    uint8_t blink_phase = 0;

    apply_leds(st, blink_phase);

    for (;;) {
        uint32_t notified = 0;
        TickType_t wait = (st == GATE_REVERSING) ? pdMS_TO_TICKS(BLINK_HALF_MS)
                                                 : portMAX_DELAY;

        if (xTaskNotifyWait(0, 0xFFFFFFFFu, &notified, wait) == pdTRUE) {
            st = (GateState_t)notified;
            blink_phase = 0;
        } else {
            /* Timeout: only happens while REVERSING — toggle blink. */
            blink_phase ^= 1u;
        }
        apply_leds(st, blink_phase);
    }
}
