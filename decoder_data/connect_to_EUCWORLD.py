import asyncio
import struct
from bleak import BleakClient, BleakScanner

TARGET_NAME = "EUC World 4FDE22"

# EUC World companion telemetry characteristic
EUCW_NOTIFY_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"

def handle_notification(sender, data):
    """
    Decode EUC World telemetry packet
    """
    # Raw hex view
    print("RAW:", data.hex())

    # Example decoding (depends on EUC World protocol version)
    if len(data) >= 12:
        speed = struct.unpack_from("<h", data, 0)[0] / 100     # km/h
        battery = data[2]                                      # %
        voltage = struct.unpack_from("<H", data, 3)[0] / 100  # V
        current = struct.unpack_from("<h", data, 5)[0] / 100  # A
        temp = struct.unpack_from("<h", data, 7)[0] / 100     # °C

        print(
            f"Speed: {speed:.2f} km/h | "
            f"Battery: {battery}% | "
            f"Voltage: {voltage:.2f} V | "
            f"Current: {current:.2f} A | "
            f"Temp: {temp:.2f} °C"
        )

async def run():
    print("Scanning...")
    devices = await BleakScanner.discover()

    target = None
    for d in devices:
        if (d.name or "").strip() == TARGET_NAME:
            target = d
            break

    if not target:
        print("EUC World not found.")
        return

    print("Connecting...")

    async with BleakClient(target.address) as client:
        print("Connected")

        print("Subscribing to telemetry stream...")
        await client.start_notify(EUCW_NOTIFY_UUID, handle_notification)

        print("Receiving live telemetry (Ctrl+C to stop)...\n")
        while True:
            await asyncio.sleep(1)

if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        print("\nStopped")
