import asyncio
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_UUID    = "0000ffe1-0000-1000-8000-00805f9b34fb"

def notification_handler(sender, data: bytearray):
    # Print raw hex bytes
    hex_str = " ".join(f"{b:02X}" for b in data)
    print(f"RAW [{len(data)}]: {hex_str}")

async def run():
    print("Scanning for EUC...")
    devices = await BleakScanner.discover()
    euc = None
    for d in devices:
        if SERVICE_UUID.lower() in [s.lower() for s in d.metadata.get("uuids", [])]:
            euc = d
            break

    if not euc:
        print("EUC not found!")
        return

    print(f"Connecting to {euc.name} [{euc.address}]")

    async with BleakClient(euc) as client:
        await client.start_notify(CHAR_UUID, notification_handler)
        print("Subscribed. Press Ctrl+C to quit.")
        while True:
            await asyncio.sleep(1)

if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        print("\nDisconnected.")