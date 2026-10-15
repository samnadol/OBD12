/*
 * comms.h
 *
 * Transport-agnostic host protocol backend. Each transport (USB UART, BLE
 * Nordic UART) feeds raw bytes in with comms_rx() and drains comms_tx_ring();
 * framing, CRC checking and dispatch are shared. See protocol.h.
 */

#ifndef INC_COMMS_COMMS_H_
#define INC_COMMS_COMMS_H_

#include <stdbool.h>
#include <stdint.h>

#include "Comms/protocol.h"
#include "Util/ring.h"

typedef enum
{
	TRANSPORT_USB = 0,
	TRANSPORT_BLE = 1,
	TRANSPORT_COUNT
} transport_id_t;

void comms_init(void);
void comms_process(void);

// --- transport driver side ---

// queue received bytes; for USB this runs in the UART interrupt (single producer)
void comms_rx(transport_id_t t, const uint8_t *data, uint16_t len);

ring_t *comms_tx_ring(transport_id_t t);

// the link can carry data (USB: always; BLE: connected and TX notifications enabled)
void comms_set_link(transport_id_t t, bool up);
bool comms_link_up(transport_id_t t);

// the link may run commands that change the vehicle state
void comms_set_authorized(transport_id_t t, bool authorized);
bool comms_authorized(transport_id_t t);

// called after data was queued on t, to start transmitting (may be NULL)
void comms_set_tx_kick(transport_id_t t, void (*kick)(void));

// --- application side ---

// handles one request (implemented in App/commands.c)
void commands_handle(transport_id_t t, uint8_t seq, uint8_t id, const uint8_t *payload, uint16_t len);

bool comms_send(transport_id_t t, uint8_t type, uint8_t seq, uint8_t id, const void *payload, uint16_t len);
bool comms_reply(transport_id_t t, uint8_t seq, uint8_t id, const void *payload, uint16_t len);
bool comms_error(transport_id_t t, uint8_t seq, uint8_t id, uint8_t code);

// sends an event to every transport whose link is up and that subscribed to topic
void comms_event(uint8_t topic, uint8_t id, const void *payload, uint16_t len);

uint8_t comms_topics(transport_id_t t);
void comms_set_topics(transport_id_t t, uint8_t topics);
bool comms_any_subscribed(uint8_t topic);

#endif /* INC_COMMS_COMMS_H_ */
