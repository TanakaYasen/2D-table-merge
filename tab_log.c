#include "tab_log.h"

#include <stdarg.h>
#include <stdio.h>

void		tab_log(TabLogLevel level, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}