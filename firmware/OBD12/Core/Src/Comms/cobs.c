/*
 * cobs.c
 *
 * Mirrored by software/obd12/framing.py; keep the two in sync.
 */

#include "Comms/cobs.h"

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
	size_t write = 1;
	size_t code_idx = 0;
	uint8_t code = 1;

	for (size_t read = 0; read < len; read++)
	{
		if (in[read] == 0)
		{
			out[code_idx] = code;
			code = 1;
			code_idx = write++;
			continue;
		}

		out[write++] = in[read];
		code++;

		// a full block (254 data bytes) has no implicit zero after it
		if (code == 0xFF && read + 1 < len)
		{
			out[code_idx] = code;
			code = 1;
			code_idx = write++;
		}
	}

	out[code_idx] = code;
	return write;
}

size_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out)
{
	size_t read = 0;
	size_t write = 0;

	while (read < len)
	{
		uint8_t code = in[read];
		if (code == 0 || read + code > len)
			return 0;
		read++;

		for (uint8_t i = 1; i < code; i++)
		{
			if (in[read] == 0)
				return 0;
			out[write++] = in[read++];
		}

		if (code != 0xFF && read != len)
			out[write++] = 0;
	}

	return write;
}
