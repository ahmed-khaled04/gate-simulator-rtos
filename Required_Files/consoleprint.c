//*****************************************************************************
//   +--+
//   | ++----+
//   +-++    |
//     |     |
//   +-+--+  |
//   | +--+--+
//   +----+    Copyright (c) 2009 Code Red Technologies Ltd.
//
// consoleprint.c - provides a "print string to console" function that uses
//                  the CodeRed semihosting debug channel functionality.
//
//*****************************************************************************

#include <stdio.h>
#include <string.h>
#include "rt_sys.h"
#include "consoleprint.h"

#if (defined(__NEWLIB__))
#define LIBSTUB_SYS_WRITE _swiwrite
#else // __REDLIB__
#define LIBSTUB_SYS_WRITE __write
#endif
int LIBSTUB_SYS_WRITE (int, char *, int);

int consoleprint(char *cpstring)
{
    int slen, res;
    slen = strlen(cpstring);
    res  = LIBSTUB_SYS_WRITE(0, cpstring, slen);
    return res;
}
