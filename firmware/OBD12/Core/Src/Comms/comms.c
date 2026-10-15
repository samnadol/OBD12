/*
 * comms.c
 *
 * Main-loop context only, except comms_rx() for USB (UART interrupt) and the
 * consumer side of the USB TX ring (UART TX interrupt).
 */

#include "Comms/comms.h"

#include <string.h>

#include "stm32g4xx_hal.h"
#include "Comms/cobs.h"
#include "Comms/crc16.h"

#define RX_RING_SIZE 512
#define TX_RING_SIZE 1024

// how long a sender in the main loop may wait for TX ring space (USB drains
// itself from its interrupt; 1 kB at 115200 baud takes ~90 ms)
#define TX_WAIT_MS 50

typedef struct
{
	ring_t rx;
	ring_t tx;
	uint8_t rx_storage[RX_RING_SIZE];
	uint8_t tx_storage[TX_RING_SIZE];

	// bytes of the frame being received, still COBS-encoded
	uint8_t frame[PROTO_MAX_ENCODED];
	uint16_t frame_len;
	bool frame_overflow;

	bool link_up;
	bool authorized;
	uint8_t topics;
	uint8_t event_seq;

	void (*kick)(void);
} transport_t;

static transport_t transports[TRANSPORT_COUNT];

void comms_init(void)
{
	for (int i = 0; i < TRANSPORT_COUNT; i++)
	{
		transport_t *tr = &transports[i];
		memset(tr, 0, sizeof(*tr));
		ring_init(&tr->rx, tr->rx_storage, 1, RX_RING_SIZE);
		ring_init(&tr->tx, tr->tx_storage, 1, TX_RING_SIZE);
	}

	transports[TRANSPORT_USB].link_up = true;
	transports[TRANSPORT_USB].authorized = true; // physically connected
	transports[TRANSPORT_USB].topics = TOPIC_LOG | TOPIC_CAN_STATUS;
	transports[TRANSPORT_BLE].topics = TOPIC_CAN_STATUS;
}

void comms_rx(transport_id_t t, const uint8_t *data, uint16_t len)
{
	ring_write(&transports[t].rx, data, len);
}

ring_t *comms_tx_ring(transport_id_t t)
{
	return &transports[t].tx;
}

void comms_set_link(transport_id_t t, bool up)
{
	transport_t *tr = &transports[t];
	if (tr->link_up == up)
		return;

	tr->link_up = up;
	if (!up)
	{
		ring_flush(&tr->tx);
		ring_flush(&tr->rx);
		tr->frame_len = 0;
		tr->frame_overflow = false;
	}
}

bool comms_link_up(transport_id_t t)
{
	return transports[t].link_up;
}

void comms_set_authorized(transport_id_t t, bool authorized)
{
	transports[t].authorized = authorized;
}

bool comms_authorized(transport_id_t t)
{
	return transports[t].authorized;
}

void comms_set_tx_kick(transport_id_t t, void (*kick)(void))
{
	transports[t].kick = kick;
}

uint8_t comms_topics(transport_id_t t)
{
	return transports[t].topics;
}

void comms_set_topics(transport_id_t t, uint8_t topics)
{
	transports[t].topics = topics;
}

bool comms_any_subscribed(uint8_t topic)
{
	for (int i = 0; i < TRANSPORT_COUNT; i++)
		if (transports[i].link_up && (transports[i].topics & topic))
			return true;
	return false;
}

static void handle_frame(transport_id_t t, transport_t *tr)
{
	uint8_t decoded[PROTO_MAX_ENCODED];
	size_t len = cobs_decode(tr->frame, tr->frame_len, decoded);

	if (len < PROTO_HEADER_LEN + PROTO_CRC_LEN)
		return; // empty, malformed or truncated: drop, the host will time out

	uint16_t received_crc = decoded[len - 2] | ((uint16_t) decoded[len - 1] << 8);
	if (crc16(decoded, len - PROTO_CRC_LEN) != received_crc)
		return;

	if (decoded[0] != PT_REQ)
		return;

	commands_handle(t, decoded[1], decoded[2], decoded + PROTO_HEADER_LEN, len - PROTO_HEADER_LEN - PROTO_CRC_LEN);
}

void comms_process(void)
{
	for (int t = 0; t < TRANSPORT_COUNT; t++)
	{
		transport_t *tr = &transports[t];
		uint8_t byte;

		while (ring_read(&tr->rx, &byte, 1))
		{
			if (byte == 0x00)
			{
				if (!tr->frame_overflow && tr->frame_len > 0)
					handle_frame((transport_id_t) t, tr);

				tr->frame_len = 0;
				tr->frame_overflow = false;
				continue;
			}

			if (tr->frame_len < sizeof(tr->frame))
				tr->frame[tr->frame_len++] = byte;
			else
				tr->frame_overflow = true; // discard until the next delimiter
		}
	}
}

static bool wait_for_space(transport_t *tr, uint16_t needed)
{
	if (ring_space(&tr->tx) >= needed)
		return true;

	// only wait where the ring can drain by itself: thread mode, interrupts on
	if (tr != &transports[TRANSPORT_USB] || __get_IPSR() != 0 || __get_PRIMASK() != 0)
		return false;

	uint32_t start = HAL_GetTick();
	while (ring_space(&tr->tx) < needed)
	{
		if (HAL_GetTick() - start > TX_WAIT_MS)
			return false;
		if (tr->kick)
			tr->kick();
	}
	return true;
}

bool comms_send(transport_id_t t, uint8_t type, uint8_t seq, uint8_t id, const void *payload, uint16_t len)
{
	transport_t *tr = &transports[t];
	if (!tr->link_up || len > PROTO_MAX_PAYLOAD)
		return false;

	uint8_t frame[PROTO_MAX_FRAME];
	frame[0] = type;
	frame[1] = seq;
	frame[2] = id;
	if (len)
		memcpy(frame + PROTO_HEADER_LEN, payload, len);

	uint16_t crc = crc16(frame, PROTO_HEADER_LEN + len);
	frame[PROTO_HEADER_LEN + len] = (uint8_t) (crc & 0xFF);
	frame[PROTO_HEADER_LEN + len + 1] = (uint8_t) (crc >> 8);

	uint8_t encoded[PROTO_MAX_ENCODED + 1];
	size_t encoded_len = cobs_encode(frame, PROTO_HEADER_LEN + len + PROTO_CRC_LEN, encoded);
	encoded[encoded_len++] = 0x00;

	// frames are queued whole or not at all
	if (!wait_for_space(tr, encoded_len))
		return false;

	ring_write(&tr->tx, encoded, encoded_len);
	if (tr->kick)
		tr->kick();
	return true;
}

bool comms_reply(transport_id_t t, uint8_t seq, uint8_t id, const void *payload, uint16_t len)
{
	return comms_send(t, PT_RSP, seq, id, payload, len);
}

bool comms_error(transport_id_t t, uint8_t seq, uint8_t id, uint8_t code)
{
	return comms_send(t, PT_ERR, seq, id, &code, 1);
}

void comms_event(uint8_t topic, uint8_t id, const void *payload, uint16_t len)
{
	for (int t = 0; t < TRANSPORT_COUNT; t++)
	{
		transport_t *tr = &transports[t];
		if (!tr->link_up || !(tr->topics & topic))
			continue;

		comms_send((transport_id_t) t, PT_EVT, tr->event_seq++, id, payload, len);
	}
}
