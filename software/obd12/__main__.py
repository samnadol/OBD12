"""Command-line tool: python -m obd12 [--usb [PORT] | --ble [ADDRESS]] <command>"""

from __future__ import annotations

import argparse
import asyncio
import sys

from . import pids
from .client import Device, DeviceError
from dataclasses import replace

from .protocol import OBD_BROADCAST, DiagFlag, DiagTarget, InfoFlag, Topic, format_can_id, parse_can_id
from .transports import BleTransport, UsbTransport

DEFAULT_LIVE_PIDS = "0C,0D,05,0F,11"
MAX_LIVE_PIDS = 32


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
    print(f"OBD address   {getattr(info.obd_addressing, 'name', info.obd_addressing)}")
    if any(info.uid):
        print(f"UID           {''.join(f'{w:08X}' for w in info.uid)}")
    print(f"BLE address   {info.ble_mac}")
    if info.ble_passkey:
        print(f"BLE passkey   {info.ble_passkey:06d}")
    flags = ", ".join(f.name for f in InfoFlag if f in info.flags) or "-"
    print(f"flags         {flags}")


async def cmd_power(dev: Device, args) -> None:
    p = await dev.get_power()
    print(f"USB regulator  output {'up  ' if p.usb_reg_out else 'down'}  ST {'high' if p.usb_reg_st else 'low'}")
    print(f"CAN regulator  output {'up  ' if p.can_reg_out else 'down'}  ST {'high' if p.can_reg_st else 'low'}")


async def cmd_can_stats(dev: Device, args) -> None:
    for name, value in vars(await dev.get_can_stats()).items():
        print(f"{name:20} {value}")


async def cmd_vin(dev: Device, args) -> None:
    print(await dev.get_vin())


def diag_target(args) -> DiagTarget:
    if args.dest is None:
        if args.rx or args.ext or args.functional:
            raise SystemExit("--rx, --ext and --functional need --dest")
        return replace(OBD_BROADCAST, pad=int(args.pad, 16), timeout_ms=args.timeout)

    target = DiagTarget.iso15765(parse_can_id(args.dest), pad=int(args.pad, 16), timeout_ms=args.timeout)
    if args.rx:
        target = replace(target, rx_id=parse_can_id(args.rx), rx_mask=int(args.rx_mask, 16))
    if args.functional:
        target = replace(target, flags=target.flags | DiagFlag.FUNCTIONAL)
    if args.ext:
        tx_ext, _, rx_ext = args.ext.partition(":")
        target = replace(target, flags=target.flags | DiagFlag.EXT_ADDR,
                         tx_ext=int(tx_ext, 16), rx_ext=int(rx_ext or tx_ext, 16))
    return target


async def cmd_obd(dev: Device, args) -> None:
    data = bytes(parse_hex_list(args.request))
    for r in await dev.diag_request(data, diag_target(args)):
        status = f"negative, NRC 0x{r.nrc:02X}" if r.negative else "ok"
        print(f"{format_can_id(r.ecu_id)}: {r.message.hex(' ').upper()}  ({status})")


async def supported_data_pids(dev: Device) -> list[int]:
    return [p for p in await dev.get_supported_pids() if not pids.is_range_pid(p)]


async def cmd_pids(dev: Device, args) -> None:
    for pid in await supported_data_pids(dev):
        info = pids.PIDS.get(pid)
        fields = ", ".join(f"{f.name} ({f.unit})" if f.unit else f.name for f in info.fields) if info else ""
        print(f"{pid:02X}  {pids.label(pid):34} {fields or 'raw'}")


async def cmd_live(dev: Device, args) -> None:
    def show(values: dict[int, bytes]) -> None:
        parts = []
        for pid, data in values.items():
            decoded = pids.decode(pid, data)
            if decoded is None:
                parts.append(f"{pid:02X}={data.hex()}")
            for name, value, unit in decoded or []:
                parts.append(f"{name}={value:.1f}{unit}")
        print("  ".join(parts), flush=True)

    if args.pids == "supported":
        selected = await supported_data_pids(dev)
        if len(selected) > MAX_LIVE_PIDS:
            print(f"polling the first {MAX_LIVE_PIDS} of {len(selected)} supported PIDs", file=sys.stderr)
            selected = selected[:MAX_LIVE_PIDS]
    else:
        selected = parse_hex_list(args.pids)

    dev.on_live_data(show)
    await dev.live_start(selected, args.interval)
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
    "power": (cmd_power, "regulator outputs and status pins"),
    "can-stats": (cmd_can_stats, "CAN controller counters"),
    "vin": (cmd_vin, "vehicle identification number"),
    "pids": (cmd_pids, "mode 01 PIDs the vehicle supports"),
    "obd": (cmd_obd, "raw diagnostic request, e.g. 'obd 01,0C' or 'obd 22,F1,90 --dest 7E0'"),
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
            p.add_argument("--dest", metavar="ID",
                           help="CAN ID in hex to send on (IDs above 7FF are 29-bit). Without it, the request goes "
                                "to the vehicle's OBD broadcast (7DF or 18DB33F1). 7DF and 18DB33F1 collect every "
                                "OBD ECU's answer; other IDs expect one ECU on the ISO 15765-4 response ID "
                                "(7E0 -> 7E8, 18DA10F1 -> 18DAF110) unless --rx says otherwise")
            p.add_argument("--rx", metavar="ID", help="CAN ID in hex the answers come from, e.g. 758 or 142AF140")
            p.add_argument("--rx-mask", default="0", metavar="MASK",
                           help="with --rx: ID bits that must match, in hex (default: all)")
            p.add_argument("--functional", action="store_true",
                           help="several ECUs may answer: collect them all until the timeout")
            p.add_argument("--ext", metavar="TX[:RX]",
                           help="extended addressing: address byte to send and to expect in answers, in hex "
                                "(e.g. 40 for a Toyota module on 750)")
            p.add_argument("--pad", default="00", help="padding byte in hex (default 00)")
            p.add_argument("--timeout", type=int, default=0, metavar="MS",
                           help="answer timeout in ms, at most 5000 (default 50)")
        elif name == "live":
            p.add_argument("--pids", default=DEFAULT_LIVE_PIDS,
                           help=f"mode 01 PIDs in hex, or 'supported' for all the vehicle supports (default {DEFAULT_LIVE_PIDS})")
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
