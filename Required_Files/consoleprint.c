#include <stdio.h>
#include "consoleprint.h"

/*
 * consoleprint() — routes a string to the Keil Debug (printf) Viewer via
 * printf, which is retargeted to ITM channel 0 by retarget.c (__write ->
 * ITM_SendChar). Output appears at:
 *   View -> Serial Windows -> Debug (printf) Viewer
 */
int consoleprint(char *cpstring)
{
    return printf("%s", cpstring);
}
