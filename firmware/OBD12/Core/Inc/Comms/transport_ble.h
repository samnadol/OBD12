/*
 * transport_ble.h
 *
 * Host protocol over the BLE Nordic UART service: RX characteristic writes go
 * into the comms RX ring, the comms TX ring is sent as TX notifications in
 * chunks of at most (ATT MTU - 3) bytes. All functions run in main-loop
 * context (BLE events are dispatched from hci_user_evt_proc()).
 */

#ifndef INC_COMMS_TRANSPORT_BLE_H_
#define INC_COMMS_TRANSPORT_BLE_H_

#include <stdbool.h>
#include <stdint.h>

// largest notification / write payload that fits the HCI transport buffers
// (HCI_MAX_PAYLOAD_SIZE / HCI_READ_PACKET_SIZE in bluenrg_conf.h)
#define BLE_TX_CHUNK_MAX 120
#define BLE_RX_WRITE_MAX 100

void transport_ble_on_connect(void);
void transport_ble_on_disconnect(void);
void transport_ble_on_mtu(uint16_t att_mtu);
void transport_ble_on_tx_subscription(bool enabled);
void transport_ble_on_rx(const uint8_t *data, uint16_t len);
void transport_ble_on_tx_pool_available(void);
void transport_ble_on_encryption(bool encrypted);

// sends pending TX data; call from the main loop
void transport_ble_process(void);

#endif /* INC_COMMS_TRANSPORT_BLE_H_ */
