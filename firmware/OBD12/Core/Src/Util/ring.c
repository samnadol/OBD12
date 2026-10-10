/*
 * ring.c
 *
 * See ring.h. head and tail are free-running 16-bit counters; the index into
 * storage is (counter & mask). A memory barrier separates the data copy from
 * the counter update so the other side never sees a counter ahead of the data.
 */

#include "Util/ring.h"

#include <string.h>

#include "stm32g4xx.h"

void ring_init(ring_t *r, void *storage, uint16_t elem_size, uint16_t capacity)
{
	r->storage = storage;
	r->elem_size = elem_size;
	r->mask = capacity - 1;
	r->head = 0;
	r->tail = 0;
}

uint16_t ring_count(const ring_t *r)
{
	return (uint16_t) (r->head - r->tail);
}

uint16_t ring_space(const ring_t *r)
{
	return (uint16_t) (r->mask + 1) - ring_count(r);
}

bool ring_push(ring_t *r, const void *elem)
{
	if (ring_space(r) == 0)
		return false;

	memcpy(&r->storage[(r->head & r->mask) * r->elem_size], elem, r->elem_size);
	__DMB();
	r->head++;
	return true;
}

bool ring_pop(ring_t *r, void *elem)
{
	if (ring_count(r) == 0)
		return false;

	memcpy(elem, &r->storage[(r->tail & r->mask) * r->elem_size], r->elem_size);
	__DMB();
	r->tail++;
	return true;
}

uint16_t ring_write(ring_t *r, const uint8_t *data, uint16_t len)
{
	uint16_t space = ring_space(r);
	if (len > space)
		len = space;

	uint16_t idx = r->head & r->mask;
	uint16_t first = (uint16_t) (r->mask + 1) - idx;
	if (first > len)
		first = len;

	memcpy(&r->storage[idx], data, first);
	memcpy(&r->storage[0], data + first, len - first);
	__DMB();
	r->head += len;
	return len;
}

uint16_t ring_peek(const ring_t *r, uint8_t *data, uint16_t len)
{
	uint16_t count = ring_count(r);
	if (len > count)
		len = count;

	uint16_t idx = r->tail & r->mask;
	uint16_t first = (uint16_t) (r->mask + 1) - idx;
	if (first > len)
		first = len;

	memcpy(data, &r->storage[idx], first);
	memcpy(data + first, &r->storage[0], len - first);
	return len;
}

uint16_t ring_read(ring_t *r, uint8_t *data, uint16_t len)
{
	len = ring_peek(r, data, len);
	ring_skip(r, len);
	return len;
}

uint16_t ring_contig(const ring_t *r, const uint8_t **data)
{
	uint16_t count = ring_count(r);
	uint16_t idx = r->tail & r->mask;
	uint16_t first = (uint16_t) (r->mask + 1) - idx;

	*data = &r->storage[idx];
	return count < first ? count : first;
}

void ring_skip(ring_t *r, uint16_t len)
{
	uint16_t count = ring_count(r);
	if (len > count)
		len = count;

	__DMB();
	r->tail += len;
}

void ring_flush(ring_t *r)
{
	__DMB();
	r->tail = r->head;
}
