import asyncio
import struct
from bleak import BleakClient, BleakScanner
from rich.live import Live
from rich.table import Table
from rich.panel import Panel
from rich.console import Console
from rich.layout import Layout

TARGET_NAME = "EUC World 4FDE22"
EUCW_NOTIFY_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"

# Global state to hold the latest telemetry
telemetry_data = {
    "speed": 0.0,
    "battery": 0,
    "voltage": 0.0,
    "current": 0.0,
    "temp": 0.0,
    "raw": "Waiting..."
}

def generate_dashboard():
    """Builds the UI components for the current telemetry state."""
    table = Table(expand=True)
    table.add_column("Metric", style="cyan", no_wrap=True)
    table.add_column("Value", style="magenta")

    table.add_row("Speed", f"{telemetry_data['speed']:.2f} km/h")
    table.add_row("Battery", f"{telemetry_data['battery']}%")
    table.add_row("Voltage", f"{telemetry_data['voltage']:.2f} V")
    table.add_row("Current", f"{telemetry_data['current']:.2f} A")
    table.add_row("Temperature", f"{telemetry_data['temp']:.2f} °C")
    
    # Wrap it in a nice panel with a border
    return Panel(table, title=f"[bold green]{TARGET_NAME} Status", border_style="bright_blue")

def handle_notification(sender, data):
    """Updates the global telemetry state (no printing here!)"""
    telemetry_data["raw"] = data.hex()

    if len(data) >= 12:
        telemetry_data["speed"] = struct.unpack_from("<h", data, 0)[0] / 100
        telemetry_data["battery"] = data[2]
        telemetry_data["voltage"] = struct.unpack_from("<H", data, 3)[0] / 100
        telemetry_data["current"] = struct.unpack_from("<h", data, 5)[0] / 100
        telemetry_data["temp"] = struct.unpack_from("<h", data, 7)[0] / 100

async def run():
    console = Console()
    console.print("[yellow]Scanning for EUC...[/yellow]")
    
    devices = await BleakScanner.discover()
    target = next((d for d in devices if (d.name or "").strip() == TARGET_NAME), None)

    if not target:
        console.print("[red]EUC World not found.[/red]")
        return

    console.print(f"[green]Connecting to {target.address}...[/green]")

    async with BleakClient(target.address) as client:
        await client.start_notify(EUCW_NOTIFY_UUID, handle_notification)
        
        # Start the Live display context
        with Live(generate_dashboard(), refresh_per_second=10) as live:
            while True:
                # Update the display with the latest data from the notification handler
                live.update(generate_dashboard())
                await asyncio.sleep(0.1)

if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        print("\n[bold red]Dashboard Stopped.[/bold red]")