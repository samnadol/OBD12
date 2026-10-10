/*
 * ring.h
 *
 * Lock-free single-producer / single-consumer ring buffer of fixed-size
 * elements. One side may run in an ISR and the other in the main loop without
 * any locking, as long as each side only uses its own functions:
 *   producer: ring_push(), ring_write()
 *   consumer: ring_pop(), ring_read(), ring_peek(), ring_skip(), ring_contig()
 */

#ifndef INC_UTIL_RING_H_
#define INC_UTIL_RING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
	uint8_t *storage;
	uint16_t elem_size;
	uint16_t mask;              // capacity - 1, capacity is a power of two
	volatile uint16_t head;     // written by the producer only
	volatile uint16_t tail;     // written by the consumer only
} ring_t;

// capacity must be a power of two and <= 32768
void ring_init(ring_t *r, void *storage, uint16_t elem_size, uint16_t capacity);

uint16_t ring_count(const ring_t *r);
uint16_t ring_space(const ring_t *r);

// element API
bool ring_push(ring_t *r, const void *elem);
bool ring_pop(ring_t *r, void *elem);

// byte API (elem_size == 1)
uint16_t ring_write(ring_t *r, const uint8_t *data, uint16_t len);      // returns bytes written
uint16_t ring_read(ring_t *r, uint8_t *data, uint16_t len);             // returns bytes read
uint16_t ring_peek(const ring_t *r, uint8_t *data, uint16_t len);       // like read, without consuming
uint16_t ring_contig(const ring_t *r, const uint8_t **data);           // longest readable run without wrap
void ring_skip(ring_t *r, uint16_t len);

// consumer-side reset (drops everything currently queued)
void ring_flush(ring_t *r);

#endif /* INC_UTIL_RING_H_ */
