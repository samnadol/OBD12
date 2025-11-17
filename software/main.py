import serial.tools.list_ports
import time

ports = serial.tools.list_ports.comports()

obd12_portinfo = None
for port in ports:
    if (port.vid == 0x0403 and port.pid == 0x6015):
        if (port.serial_number.startswith("OBD12_")):
            obd12_portinfo = port

if obd12_portinfo == None:
    print("Could not discover OBD12!")
    exit()

print(obd12_portinfo.serial_number, obd12_portinfo.device)
obd12_port = serial.Serial(
    port = obd12_portinfo.device, baudrate = 115200, bytesize=8, timeout=2, stopbits=serial.STOPBITS_ONE
)