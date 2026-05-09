#include "gate.h"
#include "basic_io.h"

/*
 * Safety Task: highest priority. Waits on xSemObstacle. When the obstacle button
 * is pressed AND the gate is currently CLOSING, it forces a 500 ms REVERSE then
 * settles in STOPPED_MIDWAY. Obstacle while OPENING is ignored (TC-09).
 */

void vSafetyTask(void *pv)
{
    (void)pv;
    vPrintString("Safety Task Started\n");
    for (;;) {
        if (xSemaphoreTake(xSemObstacle, portMAX_DELAY) != pdTRUE) continue;

        xSemaphoreTake(xMutexState, portMAX_DELAY);
        if (g_gate_state != GATE_CLOSING) {
            xSemaphoreGive(xMutexState);
            vPrintString("[Safety] Obstacle ignored\n");
            continue;
        }
        g_gate_state = GATE_REVERSING;
        xSemaphoreGive(xMutexState);
        xTaskNotify(xTaskLEDControl, (uint32_t)GATE_REVERSING, eSetValueWithOverwrite);
        vPrintString("[Safety] OBSTACLE! Reversing...\n");

        vTaskDelay(pdMS_TO_TICKS(REVERSE_MS));

        xSemaphoreTake(xMutexState, portMAX_DELAY);
        g_gate_state = GATE_STOPPED_MIDWAY;
        xSemaphoreGive(xMutexState);
        xTaskNotify(xTaskLEDControl, (uint32_t)GATE_STOPPED_MIDWAY, eSetValueWithOverwrite);
        vPrintString("[Safety] Reverse done\n");
    }
}
