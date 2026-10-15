/*
 * log.c
 *
 * printf() / PRINT_DBG() output (including the BlueNRG middleware's) is
 * collected into lines and sent as EVT_LOG frames to transports subscribed to
 * TOPIC_LOG, so debug output never corrupts the protocol byte stream.
 *
 * Output from interrupt handlers is dropped.
 */

#include <string.h>

#include "stm32g4xx_hal.h"
#include "Comms/comms.h"

#define LOG_LINE_MAX 120

static char line[LOG_LINE_MAX];
static uint16_t line_len;

static void flush_line(void)
{
	// strip the line ending
	while (line_len > 0 && (line[line_len - 1] == '\r' || line[line_len - 1] == '\n'))
		line_len--;

	if (line_len > 0)
		comms_event(TOPIC_LOG, EVT_LOG, line, line_len);

	line_len = 0;
}

// overrides the weak _write in syscalls.c
int _write(int file, char *ptr, int len)
{
	(void) file;

	if (__get_IPSR() != 0)
		return len;

	for (int i = 0; i < len; i++)
	{
		char c = ptr[i];
		if (c == '\n')
		{
			flush_line();
			continue;
		}

		line[line_len++] = c;
		if (line_len == LOG_LINE_MAX)
			flush_line();
	}

	return len;
}
