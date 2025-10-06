import asyncio
import os
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_UUID    = "0000ffe1-0000-1000-8000-00805f9b34fb"


# --- Frame extraction + parsing ---
def extract_frames(buffer: bytearray):
    frames = []
    i = 0
    while i < len(buffer) - 24:
        if buffer[i] == 0x55 and buffer[i+1] == 0xAA:
            candidate = buffer[i:i+24]
            if len(candidate) == 24 and candidate[-4:] == b'\x5A\x5A\x5A\x5A':
                frames.append(candidate)
                i += 24
                continue
        i += 1
    return frames

def parse_frame(frame: bytes):
    if len(frame) != 24 or frame[0] != 0x55 or frame[1] != 0xAA:
        return None

    if frame[18] == 0x00:  # Frame A
        return {
            "type": "A",
            "voltage_raw": int.from_bytes(frame[2:4], "big", signed=False),
            "speed_raw":   int.from_bytes(frame[4:6], "big", signed=True) * 3.6, # For Begode Master...
            "distance":    int.from_bytes(frame[6:10], "big", signed=False),
            "current_raw": int.from_bytes(frame[10:12], "big", signed=True),
            "temp_raw":    int.from_bytes(frame[12:14], "big", signed=False) * 0.0394,  # For Begode Master. double check this value when temperature changes
        }
    elif frame[18] == 0x04:  # Frame B
        return {
            "type": "B",
            "total_distance": int.from_bytes(frame[2:6], "big", signed=False) / 1000,
            "pedal_speed": frame[6],
            "led_mode": frame[13],
        }
    return {"type": "Unknown"}

# Voltage rescaling (for 134V pack)
SCALE_FACTOR = 134.4 / 67.2

def scale_voltage(raw_v):
    return raw_v * SCALE_FACTOR / 100  # in volts

def scale_speed(raw_s):
    return abs(raw_s) / 100  # km/h

def scale_current(raw_c):
    return raw_c / 100  # Amps

def scale_temp(raw_t):
    return raw_t / 100  # °C

# --- Globals ---
buffer = bytearray()
latest_A = {}
latest_B = {}

def notification_handler(sender, data: bytearray):
    global buffer, latest_A, latest_B
    buffer.extend(data)

    frames = extract_frames(buffer)
    for f in frames:
        decoded = parse_frame(f)
        if decoded:
            if decoded["type"] == "A":
                decoded["voltage"] = round(scale_voltage(decoded["voltage_raw"]), 2)
                decoded["speed"]   = round(scale_speed(decoded["speed_raw"]), 2)
                decoded["current"] = round(scale_current(decoded["current_raw"]), 2)
                decoded["temp"]    = round(scale_temp(decoded["temp_raw"]), 2)
                latest_A = decoded
            elif decoded["type"] == "B":
                latest_B = decoded

    # trim buffer so it doesn’t grow forever
    if len(buffer) > 2000:
        buffer = buffer[-2000:]

    # Live summary
    os.system("cls" if os.name == "nt" else "clear")
    print("=== EUC Live Summary ===")
    if latest_A:
        print(f"Frame A: Voltage = {latest_A['voltage']:.2f} V (scaled 134V)")
        print(f"         Speed   = {latest_A['speed']:.2f} km/h")
        print(f"         Current = {latest_A['current']:.2f} A")
        print(f"         Temp    = {latest_A['temp']:.2f} °C")
        print(f"         Distance= {latest_A['distance']} m (raw counter)")
    if latest_B:
        print(f"Frame B: Total Distance = {latest_B['total_distance']} miles")
        print(f"         Pedal/Alarm    = {latest_B['pedal_speed']}")
        print(f"         LED Mode       = {latest_B['led_mode']}")

# --- Main loop ---
async def run():
    print("Scanning for EUC...")
    devices = await BleakScanner.discover()
    euc = None
    for d in devices:
        if SERVICE_UUID.lower() in [s.lower() for s in d.metadata.get("uuids", [])]:
            euc = d
            break

    if not euc:
        print("EUC not found, or is connected to another device!")
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
