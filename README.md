# OBD12

OBD-II dongle that bridges a vehicle's CAN bus (ISO 15765-4, 500 kbit/s) to Bluetooth LE and USB-C serial.

- `pcb/`: KiCad 9 schematic and layout
- `firmware/OBD12/`: STM32G431 firmware (STM32CubeIDE or PlatformIO)
- `firmware/BLUENRG-M2SP_DTM_SPI.hex`: image for the BLE module
- `software/`: Python host library and CLI
- `docs/protocol.md`: host protocol

## Hardware

- STM32G431KBT6, 32 MHz HSE
- TCAN337 CAN transceiver on FDCAN1. No termination fitted (R16/R17/C18 are DNP for bench use).
- BlueNRG-M2SP over SPI1
- FT230X USB UART on USART1, 115200 8N1
- Powered from OBD pin 16 (12 V vehicles only) or USB-C. LM2842 buck and SPX3819 LDO, OR'd with LM66100 ideal diodes, USB has priority.
- SWD on Tag-Connect TC2030-NL: J1 for the STM32, J5 for the BlueNRG. The board must be powered while programming.

LEDs: PWR (3.3 V), S1 (on idle, blinks with a BLE connection), S2 (CAN bus healthy and ECUs responding), RX/TX (USB).

## Setup

1. Flash `BLUENRG-M2SP_DTM_SPI.hex` to the BLE module through J5.
2. Flash the STM32 firmware through J1.
3. Program the FT230X EEPROM with FT_PROG so its serial number starts with `OBD12_`. The host library finds the board by this serial.

## Firmware

- **CubeIDE:** import `firmware/OBD12` as an existing project.
- **PlatformIO:** from `firmware/OBD12`, run `pio run`, or `pio run -t upload` to flash with an ST-Link. Keep its include paths in sync with `.cproject`.

The firmware is a main loop: interrupts only fill ring buffers or set flags, and a 3 s watchdog resets the MCU if the loop stalls. USB and BLE (Nordic UART service) carry the same framed protocol.

BLE pairing uses a passkey derived from the MCU UID; `info` over USB shows it. Over BLE, reset, clear bonds and non-read-only OBD requests (e.g. clearing DTCs) need a paired link.

## Software

```
cd software
pip install -r requirements.txt
python -m obd12 info          # USB, auto-detected
python -m obd12 --ble live    # BLE
```

Commands: `info`, `power`, `can-stats`, `vin`, `obd`, `live`, `log`, `ping`, `reset`, `clear-bonds`.

## Known issues

- No output capacitor directly on the USB LDO output (`USB_REG_OUT`).
- The power priority logic depends on LM66100 `ST` polarity. Check that 3.3 V stays up with USB and 12 V both connected.
- The ADC reads the regulator outputs without a divider, so the readings sit near full scale. They only work as presence detection.
- Check that C23 on the 12 V rail is rated for at least 25 V.
- R10/R11 (60 Ω) drive the FT230X LEDs harder than the CBUS default.
- When running on 12 V only, PA9 back-powers the unpowered FT230X.
- U9 pad 21 (GND) is unconnected in the netlist.
- No reverse-polarity protection on the 12 V input.
