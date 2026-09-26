# OBD12 — Hardware Handoff

OBD12 is a small OBD-II dongle. It plugs straight into a vehicle's J1962 diagnostic port and bridges the vehicle's **high-speed CAN bus (ISO 15765-4)** to **Bluetooth Low Energy** (BlueNRG-M2SP module) and to a **USB-C serial port** (FT230X). An STM32G431 runs the whole board.

| Item | Location |
|---|---|
| Schematic / PCB (KiCad 9) | `pcb/pcb.kicad_sch` (root), `pcb/usb.kicad_sch`, `pcb/can.kicad_sch`, `pcb/ble.kicad_sch`, `pcb/pcb.kicad_pcb` |
| STM32 firmware (STM32CubeIDE) | `firmware/OBD12/` (`OBD12.ioc` is the CubeMX pin config) |
| BlueNRG-M2SP network-processor image | `firmware/BLUENRG-M2SP_DTM_SPI.hex` |
| Host-side test script (USB serial) | `software/main.py` |

---

## 1. Block diagram

```
                         ┌──────────────────────── +3V3 rail ───────────────────────────┐
                         │                                                              │
 USB-C J2 ──VBUS(5V)──► U4 SPX3819 LDO ──USB_REG_OUT(3.3V)──► U5 LM66100 ─┐             │
    │                                                                     ├──► +3V3 ────┤
    │                                                                     │             │
 OBD J3 pin16 ─► D2 TVS ─► FB1 ─► +12V ─► U7 LM2842 buck ─CAN_REG_OUT(3.3V)─► U8 LM66100 ─┘  │
                                                                                        │
 USB-C D+/D- ─► U2 USBLC6 ESD ─► U3 FT230X ◄──UART 115200──► ┌───────────────┐         │
                                                             │  U1 STM32G431 │◄────────┤
 OBD J3 pin6/14 ─► D1 PESD1CAN ─► U6 TCAN337 ◄──FDCAN1─────► │  32 MHz HSE   │         │
                                                             │               │         │
                                  U9 BlueNRG-M2SP ◄──SPI1───►└───────────────┘         │
                                  (VIN via JP3) ◄───────────────────────────────────────┘
```

---

## 2. Board summary

| Parameter | Value |
|---|---|
| Board outline | ~41.8 × 39.9 mm |
| Layers | 4 layers, 1.6 mm FR4 (0.21 mm prepreg / 1.065 mm core / 0.21 mm prepreg) |
| Layer use | F.Cu signal + local pours (+12V, buck SW, CAN_REG_OUT, GND) · In1.Cu **GND plane** · In2.Cu **power plane** (+3V3 and USB_VBUS areas) · B.Cu signal |
| Passives | 0603 throughout |
| Assembly | Every BOM line has an LCSC part number (built for JLCPCB PCBA) |
| Supply inputs | Vehicle +12 V (OBD pin 16) **or** USB-C 5 V. Either one runs the whole board. |
| Logic level | **3.3 V everywhere.** Nothing on the board runs at 5 V logic. |
| MCU clock | 32 MHz crystal (HSE), SYSCLK = 32 MHz (PLL not used) |
| Regulatory | The BlueNRG-M2SP is a pre-certified module; the silkscreen carries "Contains FCC ID S9NBNRGM2SP" |

---

## 3. Power

### 3.1 Rails

| Net | Nominal | Source | Loads | Notes |
|---|---|---|---|---|
| `+12V` | 9–16 V (vehicle battery) | OBD J3 pin 16 → D2 → FB1 | U7 VIN, U7 EN | 12 V vehicles only (see TVS note). The LM2842 accepts 4.5–42 V. |
| `/usb/USB_VBUS` | 5 V | USB-C J2 VBUS | U4 IN/EN, U3 VCC, U2 VBUS | The CC pins have 5.1 kΩ pulldowns (R8, R9), so the board is a USB-C sink at default USB current. |
| `CAN_REG_OUT` | 3.31 V | U7 LM2842X buck | U8 VIN, MCU PA7 (ADC) | Max 600 mA. |
| `USB_REG_OUT` | 3.33 V | U4 SPX3819M5-L LDO | U5 VIN, U8 ~CE, MCU PA6 (ADC) | Max 500 mA. |
| `+3V3` | ~3.3 V | U5 / U8 ideal-diode OR | MCU, TCAN337, BlueNRG (via JP3), LEDs, pull-ups | LM66100: 1.5 A max, ~141 mΩ Ron. |
| `Net-(U3-3V3OUT)` | 3.3 V | FT230X internal regulator | FT230X VCCIO and ~RESET | Local to U3 only. |

### 3.2 12 V input path (sheet `can`)
- **D2 SMAJ24A**: 400 W unidirectional TVS from OBD pin 16 to GND. It has a 24 V standoff and clamps around 39 V at rated peak current. Because it would start conducting on a 24 V truck's charging voltage, the board is **12 V vehicles only**.
- **FB1 BLM18AG121SN1D**: ferrite bead that filters the input.
- **No series reverse-polarity diode.** OBD pin 16 is always battery positive in a real vehicle. On a bench supply, a reversed connection forward-biases D2 and shorts the supply.
- **U7 LM2842X**: 550 kHz, 600 mA asynchronous buck.
  - Parts: L1 15 µH (Würth WE-LQSH-3012), D3 SM5819PL Schottky catch diode, C19 100 nF bootstrap cap. EN is tied to VIN, so the buck is always on.
  - Feedback divider: R19 3.4 kΩ (top) and R20 1.02 kΩ (bottom).
  - Vout = 0.765 V × (1 + 3.4/1.02) ≈ **3.31 V**.
  - Input caps: C22 100 nF, C23 10 µF. Output caps: C20 10 µF, C21 100 nF.

### 3.3 USB input path (sheet `usb`)
- **U4 SPX3819M5-L** adjustable LDO. EN is tied to VBUS.
  - Divider: R12 56 kΩ (top) and R14 33 kΩ (bottom).
  - Vout = 1.235 V × (1 + 56/33) ≈ **3.33 V**.
  - Input caps: C10 1 µF, C11 100 nF, C12 10 µF.

### 3.4 Power OR-ing / priority (U5, U8 — LM66100 ideal diodes)
- U5 (USB side) and U8 (CAN side) both drive `+3V3`.
- **USB has priority:**
  - `USB_REG_OUT` drives U8 `~CE` directly. When USB power is present, the 12 V path is shut off.
  - U8's `ST` output (net `CAN_REG_ST`, pulled up to `CAN_REG_OUT` by R18 10 kΩ) drives U5 `~CE`.
- Both status pins go to the MCU:
  - `USB_REG_ST` (U5 ST, pulled up to +3V3 by R13 10 kΩ) → **PB6**
  - `CAN_REG_ST` (U8 ST) → **PB7**
- Both regulator outputs are also measured by ADC2: `USB_REG_OUT` → **PA6** (IN3) and `CAN_REG_OUT` → **PA7** (IN4). Firmware reports all four values over BLE (Status → Power characteristic).
- +3V3 bulk and decoupling capacitors:
  - C13 10 µF, C14 100 nF (usb sheet)
  - C16 1 µF, C17 10 µF, C24 10 µF, C25 100 nF (can sheet)
  - MCU decoupling C1–C5

### 3.5 Power indicator
- **DS1** (red, silk "PWR"): +3V3 → DS1 → R5 1.02 kΩ → GND. It stays on whenever +3V3 is up.

---

## 4. STM32G431KBT6 pin map (U1, LQFP-32)

The MCU is an STM32G431KBT6: Cortex-M4F, 128 KB flash, 32 KB RAM, running at 3.3 V.

| Pin | Port | Net (schematic) | Function / peripheral | Dir | Connected to | Notes |
|---|---|---|---|---|---|---|
| 1 | VDD | +3V3 | Supply | – | | C1–C5 decoupling |
| 2 | PF0 | SYS_OSC_IN | RCC_OSC_IN | – | Y1 pin 1, C6 12 pF | 32 MHz HSE |
| 3 | PF1 | SYS_OSC_OUT | RCC_OSC_OUT | – | Y1 pin 3, C7 12 pF | |
| 4 | PG10-NRST | SYS_NRST | Reset | I | R3 10 k pull-up, SW1 to GND, J1 pin 3 | Silk "RESET" |
| 5 | PA0 | SPI_IRQ | GPIO EXTI0 (rising) | I | JP4 → U9 DIO7 | BlueNRG IRQ |
| 6 | PA1 | BLE_NRST | GPIO out | O | U9 RESETN, J5 pin 3 | BlueNRG reset, active low |
| 7 | PA2 | FDCAN_FAULT | GPIO EXTI2 | I | U6 FAULT, R15 10 k pull-up | Transceiver fault, open-drain, active high |
| 8 | PA3 | FDCAN_SILENT | GPIO out | O | U6 S | High = silent (listen-only). Firmware drives it low. |
| 9 | PA4 | HWCONF0 | GPIO in, internal pull-down | I | R1 0 Ω (DNP) to +3V3 | HW strap bit 0 |
| 10 | PA5 | LED_STATUS_1 | GPIO out | O | DS2 anode (green, R6 1.02 k) | Silk "S1", active high |
| 11 | PA6 | USB_REG_OUT | ADC2_IN3 | AI | U4 OUT | USB LDO output monitor |
| 12 | PA7 | CAN_REG_OUT | ADC2_IN4 | AI | U7 buck output | 12 V buck output monitor |
| 13 | PB0 | LED_STATUS_2 | GPIO out | O | DS3 anode (green, R7 1.02 k) | Silk "S2", active high |
| 14 | VSSA | GND | | | | |
| 15 | VDDA | +3V3 | | | | No separate analog filtering |
| 16 | VSS | GND | | | | |
| 17 | VDD | +3V3 | | | | |
| 18 | PA8 | HWCONF1 | GPIO in, internal pull-down | I | R2 0 Ω (DNP) to +3V3 | HW strap bit 1 |
| 19 | PA9 | USB_RXD,MCU_TXD | USART1_TX | O | U3 RXD | 115200 8N1 |
| 20 | PA10 | USB_TXD,MCU_RXD | USART1_RX | I | U3 TXD | |
| 21 | PA11 | FDCAN_RX | FDCAN1_RX | I | U6 RXD | |
| 22 | PA12 | FDCAN_TX | FDCAN1_TX | O | U6 TXD | |
| 23 | PA13 | T_JTMS_SWDIO | SWDIO | IO | J1 pin 2 | |
| 24 | PA14 | T_JCLK_SWCLK | SWCLK | I | J1 pin 4 | |
| 25 | PA15 | SPI_CS | GPIO out (SPI1 CS) | O | U9 DIO11 | Active low |
| 26 | PB3 | SPI_CLK | SPI1_SCK | O | U9 DIO0 | |
| 27 | PB4 | SPI_MISO | SPI1_MISO | I | U9 DIO2 | |
| 28 | PB5 | SPI_MOSI | SPI1_MOSI | O | U9 DIO3 | |
| 29 | PB6 | USB_REG_ST | GPIO in | I | U5 ST, R13 10 k pull-up | USB-path ideal diode status |
| 30 | PB7 | CAN_REG_ST | GPIO in | I | U8 ST, U5 ~CE, R18 10 k pull-up to CAN_REG_OUT | CAN-path ideal diode status |
| 31 | PB8-BOOT0 | SYS_BOOT0 | BOOT0 | I | R4 10 k pull-down, JP1 to +3V3 | Bridge JP1 to boot the ST ROM bootloader |
| 32 | VSS | GND | | | | |

### Peripheral configuration (from `OBD12.ioc`)

| Peripheral | Settings |
|---|---|
| RCC | HSE 32 MHz crystal → SYSCLK / AHB / APB = 32 MHz. FDCAN kernel clock = HSE. |
| FDCAN1 | Classic CAN, **500 kbit/s**. Prescaler 1, Seg1 50, Seg2 13 (64 tq, sample point ≈ 79.7 %), SJW 4. Auto-retransmit off. One standard-ID range filter (0x000–0x7FF → FIFO0). |
| SPI1 | Master, 8-bit, CPOL low / CPHA 2nd edge (mode 1), prescaler 16 → 2 Mbit/s |
| USART1 | 115200 baud, 8N1, async |
| ADC2 | 12-bit, single-ended, software trigger, IN3 / IN4 |
| TIM6 / TIM7 | 1 kHz tick base (32000 prescaler). TIM6 period 1000 (1 s), TIM7 period 100 (100 ms). |
| EXTI | EXTI0 (BLE IRQ), EXTI2 (CAN fault) |

### Hardware configuration straps
- R1 (PA4 → HWCONF0) and R2 (PA8 → HWCONF1) are 0 Ω links to +3V3. Both are **DNP** by default, and the internal pull-downs read the strap as `0b00`.
- Firmware prints the strap at boot (`Hardware Configuration: 0bXY`). Silkscreen: "CONF 0 1".
- Use the straps to mark board revisions or assembly variants.

---

## 5. CAN interface (sheet `can`)

| Item | Detail |
|---|---|
| Transceiver | **U6 TCAN337** (TI), SOIC-8, 3.3 V supply (VCC = +3V3), up to 1 Mbit/s. Pin 5 = FAULT, pin 8 = S (silent). |
| Logic side | TXD/RXD at 3.3 V directly on PA12/PA11. No level shifting is needed. |
| Bus levels | ISO 11898-2 compatible. The differential is ~0 V when recessive and ≥ 1.5 V when dominant into 60 Ω. A 3.3 V transceiver works on the same bus as 5 V transceivers. |
| Bit rate | 500 kbit/s in firmware (standard OBD-II CAN rate). The hardware supports up to 1 Mbit/s classic CAN. |
| Modes | PA3 low = normal. PA3 high = silent/listen-only (TX disabled, RX still active). |
| Fault | FAULT is open-drain, pulled up by R15 10 k to +3V3, and raises EXTI2 on PA2. It flags TXD dominant timeout, thermal shutdown, and similar faults. |
| ESD | **D1 PESD1CAN**: dual-line CAN ESD/TVS (Nexperia) on CANH/CANL to GND |
| Termination | **None populated (DNP).** Optional split termination: R16 60 Ω + R17 60 Ω in series across CANH–CANL, with C18 4.7 nF from the midpoint to GND. |
| Connector pins | CANH → J3 pin 6, CANL → J3 pin 14 |

**Termination guidance**
- **In a vehicle: leave R16, R17, and C18 unpopulated.** The OBD port is a stub off an already-terminated bus (two 120 Ω ends = 60 Ω). Adding a third terminator loads the bus.
- **On the bench** with only one other node: fit R16/R17 (60 Ω; 60.4 Ω 1 % is fine) and C18 4.7 nF to get split termination. The other end still needs its own 120 Ω.
- Measure CANH–CANL with the vehicle off. You should see ~60 Ω from the vehicle side alone.

### OBD-II connector (J3, Comtech C-OBD-II-16M, male J1962 plug)

| J3 pin | Standard use | Board net | Connected? |
|---|---|---|---|
| 2 | SAE J1850 bus + | `OBD_J1850+` | Labelled only, no circuitry |
| 4 | Chassis ground | `OBD_CGND` → J4 pin 1 | Only through **J4 (DNP)**. Fit a jumper/short on J4 to tie chassis GND to board GND. |
| 5 | Signal ground | GND | Yes (board ground reference) |
| 6 | CAN high (ISO 15765) | `CAN_T+` | Yes → U6 CANH |
| 7 | ISO 9141/14230 K-line | `OBD_1941-2K` | Labelled only, no circuitry |
| 10 | SAE J1850 bus − | `OBD_J1850-` | Labelled only, no circuitry |
| 14 | CAN low (ISO 15765) | `CAN_T-` | Yes → U6 CANL |
| 15 | ISO 9141/14230 L-line | `OBD_1941-2L` | Labelled only, no circuitry |
| 16 | Battery + (unswitched) | +12V (via D2/FB1) | Yes |
| 1, 3, 8, 9, 11, 12, 13 | OEM-specific | – | Not connected |

Only CAN (ISO 15765-4) is implemented. The J1850, K-line, and L-line pins are broken out to named nets for a future revision.

---

## 6. Bluetooth LE (sheet `ble`)

| Item | Detail |
|---|---|
| Module | **U9 ST BlueNRG-M2SP**: BlueNRG-2 SoC with integrated chip antenna, pre-certified, used as an SPI network processor |
| Firmware on module | `firmware/BLUENRG-M2SP_DTM_SPI.hex` (ST DTM image, SPI transport). Flash it once through J5. |
| Supply | VIN from +3V3 through **JP3 "BLE_3V3_ISO"** (solder jumper, bridged by default). Decoupling: C26 100 nF, C27 1 µF, C28 10 µF. Cut JP3 to isolate or measure module current. |
| Host link | SPI1 at 2 Mbit/s, mode 1 |

**MCU ↔ module wiring**

| Signal | STM32 | BlueNRG-M2SP |
|---|---|---|
| SPI_CLK | PB3 | DIO0 (pin 15) |
| SPI_MISO | PB4 | DIO2 (pin 16) |
| SPI_MOSI | PB5 | DIO3 (pin 17) |
| SPI_CS | PA15 | DIO11 (pin 11) |
| SPI_IRQ | PA0 | DIO7/BOOT (pin 7), through JP4 |
| RESET | PA1 | RESETN (pin 19) |
| SWDIO / SWCLK | – | DIO10 (pin 13) / DIO9 (pin 12) → J5 |

**Boot / IRQ jumpers.** DIO7 is both the module's SPI IRQ output and its bootloader-select pin.

| Jumper | Silk | Default | Purpose |
|---|---|---|---|
| JP4 | BLE BOOT ISO | Bridged | Connects DIO7 to MCU PA0 (normal operation) |
| JP2 | BLE_BOOT 1 | Open | Ties DIO7 to +3V3 (force the module's bootloader at reset) |
| JP5 | BLE_BOOT 0 | Open | Ties DIO7 to GND |
| JP3 | BLE_3V3_ISO | Bridged | Module VIN from +3V3 |

**Firmware BLE profile (for reference).** The module advertises at 250–500 ms intervals (0.625 ms units: 400–800) and exposes three services:
- Device Information
- Nordic-UART-style RX/TX service
- Status service with a Power characteristic carrying the regulator ST bits and ADC readings

---

## 7. USB-C / debug UART (sheet `usb`)

| Item | Detail |
|---|---|
| Connector | **J2** USB-C receptacle (G-Switch GT-USB-7010ASV), USB 2.0 only |
| CC | R8, R9 5.1 kΩ to GND on CC1 and CC2 → default-current sink |
| ESD | **U2 USBLC6-2SC6** on D+/D−, VBUS-referenced |
| Bridge | **U3 FTDI FT230XS**. VCC = VBUS (5 V). The internal 3V3OUT feeds VCCIO, so the UART is **3.3 V**. RTS is looped to CTS. |
| UART | FT230X TXD → PA10 (USART1_RX) and PA9 (USART1_TX) → FT230X RXD, 115200 8N1 |
| LEDs | DS4 (RX) on CBUS1 and DS5 (TX) on CBUS2, both green. Each LED is fed from +3V3 through R10/R11 60 Ω, with the cathode sunk by CBUS. Silk "TX RX". |
| USB IDs | VID 0x0403 / PID 0x6015 (FT230X default). `software/main.py` finds the board by an FTDI serial number starting with `OBD12_`, so **program each FT230X EEPROM with FT_PROG** to set that serial. |

FT230X decoupling: C8 100 nF + C9 1 µF on 3V3OUT, C10–C12 on VBUS.

---

## 8. Programming & debug connectors

Both programming headers are Tag-Connect **TC2030-NL** (no-legs) footprints and use a TC2030-NL cable with a standard 10-pin Cortex adapter.

| Ref | Target | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 | Pin 6 |
|---|---|---|---|---|---|---|---|
| J1 | STM32 (U1) | +3V3 (VTref) | SWDIO (PA13) | NRST | SWCLK (PA14) | GND | SWO (NC) |
| J5 | BlueNRG-M2SP (U9) | +3V3 | DIO10 (SWDIO) | RESETN (shared with PA1) | DIO9 (SWCLK) | GND | NC |

**The board must be powered (USB or 12 V) while programming.** Pin 1 is a voltage reference, not a supply input.

Other user controls:
- **SW1** (silk "RESET"): tactile switch that pulls the STM32 NRST low.
- **JP1** (silk "BOOT0 PB8"): open solder jumper. Bridge it to hold BOOT0 high and enter the STM32 system bootloader (USART1 is reachable through the FT230X).

---

## 9. LEDs

| Ref | Colour | Silk | Driven by | Series R | Meaning |
|---|---|---|---|---|---|
| DS1 | Red | PWR | +3V3 | R5 1.02 k | +3V3 present |
| DS2 | Green | S1 | PA5, active high | R6 1.02 k | Firmware status 1 (heartbeat / app state) |
| DS3 | Green | S2 | PB0, active high | R7 1.02 k | Firmware status 2 |
| DS4 | Green | RX | FT230X CBUS1, active low | R10 60 Ω | USB UART RX activity |
| DS5 | Green | TX | FT230X CBUS2, active low | R11 60 Ω | USB UART TX activity |

---

## 10. Bill of materials

This is the populated BOM, generated from the schematic. LCSC numbers come from the `LCSC` field.

| Refs | Qty | Value / part | Package | LCSC | Description |
|---|---|---|---|---|---|
| U1 | 1 | STM32G431KBT6 | LQFP-32 | C5270317 | MCU, Cortex-M4F, 128 KB / 32 KB |
| U2 | 1 | USBLC6-2SC6 | SOT-23-6 | C7519 | USB ESD protection |
| U3 | 1 | FT230XS | SSOP-16 | C69082 | USB ↔ UART bridge |
| U4 | 1 | SPX3819M5-L | SOT-23-5 | C9056 | 500 mA adjustable LDO (USB → 3.3 V) |
| U5, U8 | 2 | LM66100DCK | SC-70-6 | C2869734 | Ideal diode (power OR-ing) |
| U6 | 1 | TCAN337 | SOIC-8 | C2860648 | 3.3 V CAN transceiver with silent mode and fault output |
| U7 | 1 | LM2842X | TSOT-23-6 | C2865947 | 42 V / 600 mA buck (12 V → 3.3 V) |
| U9 | 1 | BLUENRG-M2SP | module | C1850403 | BLE 5 module (SPI) |
| Y1 | 1 | Würth 830103622909 | 3225-4 | C9009 | Crystal (32 MHz HSE per firmware config) |
| D1 | 1 | PESD1CAN | SOT-23 | C15771 | CAN bus ESD |
| D2 | 1 | SMAJ24A | SMA | C113962 | 24 V TVS on +12 V input |
| D3 | 1 | SM5819PL-TP | SOD-123F | C669023 | Buck catch Schottky |
| L1 | 1 | 15 µH WE-LQSH-3012 | 3×3 mm | C668450 | Buck inductor |
| FB1 | 1 | BLM18AG121SN1D | 0603 | C76892 | Ferrite bead, 12 V input |
| J2 | 1 | USB-C 16P (GT-USB-7010ASV) | SMD | C2988369 | USB 2.0 Type-C receptacle |
| J3 | 1 | Comtech C-OBD-II-16M | – | – | OBD-II J1962 male plug |
| J1, J5 | 2 | TC2030-NL | footprint | – | SWD Tag-Connect pads (no part) |
| SW1 | 1 | TS-1187A | SMD | C318884 | Reset button |
| DS1 | 1 | Red LED | 0603 | C965799 | Power |
| DS2–DS5 | 4 | Green LED | 0603 | C965804 | Status / USB activity |
| C2–C4, C8, C11, C14, C15, C19, C21, C22, C25, C26 | 12 | 100 nF | 0603 | C14663 | Decoupling / bootstrap |
| C1, C9, C10, C16, C27 | 5 | 1 µF | 0603 | C559769 | Decoupling |
| C5, C12, C13, C17, C20, C23, C24, C28 | 8 | 10 µF | 0603 | C92487 | Bulk |
| C6, C7 | 2 | 12 pF | 0603 | C107034 | Crystal load |
| R3, R4, R13, R15, R18 | 5 | 10 k | 0603 | C25804 | Pull-ups / pull-downs |
| R5–R7, R20 | 4 | 1.02 k | 0603 | C22831 | LED resistors, buck FB bottom |
| R8, R9 | 2 | 5.1 k | 0603 | C23186 | USB-C CC pulldowns |
| R10, R11 | 2 | 60 Ω | 0603 | C166918 | FT230X LED resistors |
| R12 | 1 | 56 k | 0603 | C23206 | LDO ADJ top |
| R14 | 1 | 33 k | 0603 | C4216 | LDO ADJ bottom |
| R19 | 1 | 3.4 k | 0603 | C22997 | Buck FB top |
| JP1–JP5 | 5 | Solder jumpers | – | – | JP3 and JP4 bridged by default; JP1, JP2, JP5 open |
| G1 | 1 | Logo | – | – | Silkscreen |

**DNP (do not populate) by default**

| Ref | Value | Purpose |
|---|---|---|
| R1, R2 | 0 Ω | HWCONF0/1 straps |
| R16, R17 | 60 Ω | CAN split termination |
| C18 | 4.7 nF | CAN split-termination midpoint cap |
| J4 | 1×2 2.54 mm header | Links OBD chassis ground (pin 4) to board GND |

---

## 11. Bring-up checklist

1. **Power, USB only.**
   - `USB_REG_OUT` ≈ 3.33 V and `+3V3` ≈ 3.3 V. DS1 lights.
   - FT230X enumerates (0403:6015).
2. **Power, 12 V only** (current-limited bench supply on J3 pins 16 and 5):
   - `CAN_REG_OUT` ≈ 3.31 V and `+3V3` up.
   - Check the buck switch node `/can/SW` for clean 550 kHz switching.
3. **Both supplies connected.**
   - `+3V3` must stay up with no glitch when either source is added or removed.
   - PB6/PB7 read back the expected status. See review note 2 below.
4. **Program the STM32** over J1 (SWD) and program the **BlueNRG-M2SP** over J5 with `BLUENRG-M2SP_DTM_SPI.hex`.
5. **Program the FT230X EEPROM** so its serial number starts with `OBD12_`.
6. **Check the UART console** at 115200 8N1. The boot banner shows the HWCONF strap and the STM32 UID.
7. **Check CAN.**
   - On the bench, fit the termination or use a terminated CAN adapter at 500 kbit/s.
   - In a vehicle, the firmware requests the VIN (Mode 09 PID 02) at start-up.
   - PA2 FAULT must stay low.
8. **Check BLE.** The module advertises, and the Nordic UART and Status services are visible from a phone.

---

## 12. Design review notes / open items

These came up while documenting the design. **Check each one before the next spin.**

1. **U4 SPX3819 has no output capacitor on `USB_REG_OUT`.**
   - The LDO needs ≥ 1 µF on its output to be stable.
   - The 10 µF parts (C13, C17, C24) sit on `+3V3`, *behind* the LM66100 ideal diode.
   - Add ~1–10 µF directly on `USB_REG_OUT`. The buck side already has C20/C21.
2. **Power-priority logic depends on the LM66100 `ST` polarity.**
   - With USB present, U8 is disabled, and U5's `~CE` is driven by U8's `ST` pin (pulled up to `CAN_REG_OUT` by R18).
   - If `ST` goes high-Z while U8 is disabled, R18 pulls U5 `~CE` high and **both paths turn off when USB and 12 V are both present.**
   - Verify on hardware (checklist step 3) and against the LM66100 datasheet.
3. **ADC monitoring of the 3.3 V rails.**
   - `USB_REG_OUT` and `CAN_REG_OUT` (~3.31–3.33 V) go straight to PA6/PA7 while VDDA = `+3V3`, which is slightly lower after the ideal-diode drop.
   - The readings will sit at or near full scale (4095), so they only work as presence detectors.
   - Add a divider (for example 2:1) if real voltage measurement is wanted. The ADC pin also sees more than VDDA when that rail is backed by the other source.
4. **Voltage rating of C23 (10 µF, 0603, LCSC C92487) on the +12 V rail.**
   - Confirm the rated voltage is at least 25 V, and preferably 50 V, for automotive transients up to the TVS clamp (~39 V).
   - Also account for DC-bias derating: a 0603 10 µF part at 12 V keeps only a small fraction of its nominal capacitance.
   - C22 (100 nF, C14663) is 50 V.
5. **FT230X LED resistors R10/R11 = 60 Ω.**
   - That gives roughly 15–20 mA per LED, above the FT230X CBUS default drive strength.
   - Consider ~1 k, or configure CBUS drive in the EEPROM.
6. **FT230X back-powering.** When the board runs on 12 V only, the MCU's TX (PA9) drives the RXD pin of the unpowered FT230X. A series resistor on PA9 would limit that current.
7. **U9 pad 21 (GND) shows as unconnected in the netlist.** Only pad 8 is tied to GND. Check the footprint against the BlueNRG-M2SP datasheet and ground every GND pad.
8. **No reverse-polarity protection on the 12 V input.** This is acceptable for a J1962 port but not for a bench supply. A series Schottky or a P-FET would protect it.
9. **VDDA is tied directly to +3V3.** There is no ferrite or RC filter, which is acceptable for the current coarse ADC use.
10. Items carried over from the previous README:
    - Add an activity/status LED for the BlueNRG-M2SP, driven by the DTM firmware.
    - Add 10–100 k pull-down resistors on each regulator output so the outputs don't float when that source is unpowered. This partly overlaps item 1. `USB_REG_OUT` already has R12+R14 (89 k).
    - Future: move from the M2SP module to a bare BlueNRG-2 with a direct antenna.

### Firmware TODO (carried over)
- CAN peripheral enable and setup. The FDCAN init, filter, and RX path now exist in `Core/Src/CAN/`.
- OBD layer (`Core/Src/CAN/OBD.c`, `ISOTP.c`)
- BLE and UART transport layers
- App protocol layer
