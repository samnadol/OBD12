"""Command-line tool: python -m obd12 [--usb [PORT] | --ble [ADDRESS]] <command>"""

from __future__ import annotations

import argparse
import asyncio
import sys

from . import pids
from .client import Device, DeviceError
from .protocol import InfoFlag, Topic
from .transports import BleTransport, UsbTransport

DEFAULT_LIVE_PIDS = "0C,0D,05,0F,11"


async def make_transport(args):
    if args.ble is not None:
        if args.ble:
            return BleTransport(args.ble, pair=args.pair)
        return await BleTransport.discover(pair=args.pair)
    if args.usb:
        return UsbTransport(args.usb)
    return UsbTransport.discover()


def parse_hex_list(text: str) -> list[int]:
    return [int(x, 16) for x in text.replace(" ", ",").split(",") if x]


async def cmd_info(dev: Device, args) -> None:
    info = await dev.get_info()
    print(f"protocol      {info.proto_version}")
    print(f"firmware      {'.'.join(map(str, info.fw_version))}")
    print(f"hw config     0b{info.hwconf:02b}")
    print(f"reset cause   {getattr(info.reset_cause, 'name', info.reset_cause)}")
    fault = getattr(info.last_fault, 'name', info.last_fault)
    print(f"last fault    {fault}" + (f" (0x{info.last_fault_info:08X})" if info.last_fault_info else ""))
    print(f"CAN state     {getattr(info.can_state, 'name', info.can_state)}")
    if any(info.uid):
        print(f"UID           {''.join(f'{w:08X}' for w in info.uid)}")
    print(f"BLE address   {info.ble_mac}")
    if info.ble_passkey:
        print(f"BLE passkey   {info.ble_passkey:06d}")
    flags = ", ".join(f.name for f in InfoFlag if f in info.flags) or "-"
    print(f"flags         {flags}")


async def cmd_power(dev: Device, args) -> None:
    p = await dev.get_power()
    print(f"USB regulator  {'ok ' if p.usb_regulator_ok else 'off'}  {p.usb_regulator_mv} mV")
    print(f"CAN regulator  {'ok ' if p.can_regulator_ok else 'off'}  {p.can_regulator_mv} mV")


async def cmd_can_stats(dev: Device, args) -> None:
    for name, value in vars(await dev.get_can_stats()).items():
        print(f"{name:20} {value}")


async def cmd_vin(dev: Device, args) -> None:
    print(await dev.get_vin())


async def cmd_obd(dev: Device, args) -> None:
    data = bytes(parse_hex_list(args.request))
    for r in await dev.obd_request(data, dest=int(args.dest, 16)):
        status = f"negative, NRC 0x{r.nrc:02X}" if r.negative else "ok"
        print(f"0x{r.ecu_id:03X}: {r.message.hex(' ').upper()}  ({status})")


async def cmd_live(dev: Device, args) -> None:
    def show(values: dict[int, bytes]) -> None:
        parts = []
        for pid, data in values.items():
            decoded = pids.decode(pid, data)
            if decoded:
                name, value, unit = decoded
                parts.append(f"{name}={value:.1f}{unit}")
            else:
                parts.append(f"{pid:02X}={data.hex()}")
        print("  ".join(parts), flush=True)

    dev.on_live_data(show)
    await dev.live_start(parse_hex_list(args.pids), args.interval)
    try:
        await asyncio.Event().wait()
    finally:
        await dev.live_stop()


async def cmd_log(dev: Device, args) -> None:
    dev.on_log(lambda line: print(line, flush=True))
    dev.on_can_status(lambda s: print(f"[CAN state: {getattr(s, 'name', s)}]", flush=True))
    await dev.set_events(Topic.LOG | Topic.CAN_STATUS)
    await asyncio.Event().wait()


async def cmd_ping(dev: Device, args) -> None:
    loop = asyncio.get_running_loop()
    start = loop.time()
    await dev.ping(b"obd12")
    print(f"pong in {(loop.time() - start) * 1000:.1f} ms")


async def cmd_reset(dev: Device, args) -> None:
    await dev.reset()
    print("device is rebooting")


async def cmd_clear_bonds(dev: Device, args) -> None:
    await dev.ble_clear_bonds()
    print("BLE bonds cleared")


COMMANDS = {
    "info": (cmd_info, "device information (the BLE passkey is only shown over USB)"),
    "power": (cmd_power, "regulator status and voltages"),
    "can-stats": (cmd_can_stats, "CAN controller counters"),
    "vin": (cmd_vin, "vehicle identification number"),
    "obd": (cmd_obd, "raw OBD request, e.g. 'obd 01,0C'"),
    "live": (cmd_live, "stream live data until Ctrl+C"),
    "log": (cmd_log, "show device log output until Ctrl+C"),
    "ping": (cmd_ping, "round-trip time"),
    "reset": (cmd_reset, "reboot the device (needs an authorized link)"),
    "clear-bonds": (cmd_clear_bonds, "forget all bonded BLE devices (needs an authorized link)"),
}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="obd12", description=__doc__)
    link = parser.add_mutually_exclusive_group()
    link.add_argument("--usb", nargs="?", const="", metavar="PORT", help="connect over USB (default; auto-detects the port)")
    link.add_argument("--ble", nargs="?", const="", metavar="ADDRESS", help="connect over BLE (scans for OBD12 if no address)")
    parser.add_argument("--pair", action="store_true", help="BLE: pair after connecting (passkey from 'info' over USB)")

    sub = parser.add_subparsers(dest="command", required=True)
    for name, (_, help_text) in COMMANDS.items():
        p = sub.add_parser(name, help=help_text)
        if name == "obd":
            p.add_argument("request", help="request bytes in hex, e.g. 01,0C")
            p.add_argument("--dest", default="7DF", help="destination CAN ID in hex (default 7DF, broadcast)")
        elif name == "live":
            p.add_argument("--pids", default=DEFAULT_LIVE_PIDS, help=f"mode 01 PIDs in hex (default {DEFAULT_LIVE_PIDS})")
            p.add_argument("--interval", type=int, default=500, help="polling interval in ms (default 500)")

    # "--ble info": argparse would take the command as the optional ADDRESS/PORT
    argv = list(sys.argv[1:] if argv is None else argv)
    for i, arg in enumerate(argv[:-1]):
        if arg in ("--usb", "--ble") and argv[i + 1] in COMMANDS:
            argv[i] = arg + "="

    args = parser.parse_args(argv)
    handler = COMMANDS[args.command][0]

    async def run() -> None:
        async with Device(await make_transport(args)) as dev:
            await handler(dev, args)

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
    except ModuleNotFoundError as e:
        print(f"error: {e} (pip install -r requirements.txt)", file=sys.stderr)
        return 1
    except (DeviceError, ConnectionError, TimeoutError) as e:
        print(f"error: {e or type(e).__name__}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
