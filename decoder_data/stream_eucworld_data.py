import asyncio
import struct
from datetime import datetime
from bleak import BleakClient, BleakScanner
from rich.live import Live
from rich.table import Table
from rich.panel import Panel
from rich.console import Console
from rich.text import Text
from rich.layout import Layout
from rich import box

TARGET_NAME = "EUC World 4FDE22"
EUCW_NOTIFY_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"

ALARM_FLAGS = [
    "1st Speed", "2nd Speed", "3rd Speed",
    "Current (peak)", "Current (sustained)", "Temperature",
    "Overvoltage", "(reserved)", "Safety Margin",
    "Speed Limit", "Undervoltage", "Tire Pressure"
]

# ── Unit conversion helpers ───────────────────────────────────────────────────
def kmh_to_mph(v):  return v * 0.621371 if v is not None else None
def m_to_mi(v):     return v * 0.000621371 if v is not None else None
def c_to_f(v):      return v * 9/5 + 32 if v is not None else None
def whkm_to_whmi(v): return v * 1.60934 if v is not None else None

state = {
    "raw": "–",
    "last_update": "–",
    "frame_counts": {0: 0, 1: 0, 2: 0, 3: 0},
    # Frame 0/2
    "alarms_02": [],
    "distance": (None, False),
    "speed": (None, False),
    "avg_cell_v": (None, False),
    "energy_cons": (None, False),
    "safety_margin": (None, False),
    "relative_load": (None, False),
    "battery_level": (None, False),
    "temperature": (None, False),
    # Frame 1
    "alarms_1": [],
    "speedo_range": None,
    "phone_battery": None,
    "avg_speed": (None, False),
    "avg_ride_speed": (None, False),
    "max_speed": (None, False),
    "avg_energy": (None, False),
    "min_safety": (None, False),
    "min_rel_load": (None, False),
    "max_rel_load": (None, False),
    "min_battery": (None, False),
    "max_battery": (None, False),
    # Frame 3
    "alarms_3": [],
    "min_temp": (None, False),
    "max_temp": (None, False),
}

def fmt(value, unit="", decimals=1, valid=True):
    if value is None:
        return Text("–", style="dim")
    text = f"{value:.{decimals}f} {unit}".strip() if isinstance(value, float) else f"{value} {unit}".strip()
    return Text(text, style="bright_white" if valid else "yellow dim")

def alarm_text(alarms):
    return Text("None", style="green") if not alarms else Text(", ".join(alarms), style="bold red")

def valid_dot(valid):
    return Text("●", style="green") if valid else Text("●", style="dim yellow")

def build_dashboard():
    layout = Layout()
    layout.split_column(
        Layout(name="header", size=3),
        Layout(name="main"),
        Layout(name="footer", size=3),
    )
    layout["main"].split_row(
        Layout(name="live"),
        Layout(name="stats"),
        Layout(name="extremes"),
    )

    # ── Header ────────────────────────────────────────────────────────────────
    fc = state["frame_counts"]
    header_text = Text(justify="center")
    header_text.append(f"⚡ {TARGET_NAME}", style="bold bright_cyan")
    header_text.append(f"   last update: {state['last_update']}", style="dim")
    header_text.append(
        f"   frames: [0]×{fc[0]}  [1]×{fc[1]}  [2]×{fc[2]}  [3]×{fc[3]}",
        style="dim"
    )
    layout["header"].update(Panel(header_text, box=box.HORIZONTALS, style="bright_blue"))

    # ── Current (Frame 0/2) ───────────────────────────────────────────────────
    t0 = Table(box=box.SIMPLE_HEAVY, expand=True, show_header=True)
    t0.add_column("", width=2, no_wrap=True)
    t0.add_column("Live", style="cyan", no_wrap=True)
    t0.add_column("Value", justify="right")

    def row0(label, key, unit="", decimals=1, convert=None):
        val, valid = state[key]
        display = convert(val) if (convert and val is not None) else val
        t0.add_row(valid_dot(valid), label, fmt(display, unit, decimals, valid))

    t0.add_row("", "[bold]Alarms[/bold]", alarm_text(state["alarms_02"]))
    row0("Speed",          "speed",         "mph",    1, kmh_to_mph)
    row0("Battery",        "battery_level", "%",      0)
    row0("Avg Cell V",     "avg_cell_v",    "V",      2)
    row0("Energy Cons",    "energy_cons",   "Wh/mi",  1, whkm_to_whmi)
    row0("Safety Margin",  "safety_margin", "%",      0)
    row0("Relative Load",  "relative_load", "%",      0)
    row0("Temperature",    "temperature",   "°F",     1, c_to_f)
    row0("Distance",       "distance",      "mi",     3, m_to_mi)

    layout["live"].update(Panel(t0, title="[bold green]Current", border_style="green"))

    # ── Stats (Frame 1) ───────────────────────────────────────────────────────
    t1 = Table(box=box.SIMPLE_HEAVY, expand=True, show_header=True)
    t1.add_column("", width=2, no_wrap=True)
    t1.add_column("Stats", style="cyan", no_wrap=True)
    t1.add_column("Value", justify="right")

    def row1(label, key, unit="", decimals=1, convert=None):
        val, valid = state[key]
        display = convert(val) if (convert and val is not None) else val
        t1.add_row(valid_dot(valid), label, fmt(display, unit, decimals, valid))

    sb = state["speedo_range"]
    pb = state["phone_battery"]
    t1.add_row("", "[bold]Alarms[/bold]", alarm_text(state["alarms_1"]))
    t1.add_row("", "Speedo Range",  fmt(kmh_to_mph(sb), "mph", 0) if sb is not None else Text("–", style="dim"))
    t1.add_row("", "Phone Battery", fmt(pb, "%", 0) if pb is not None else Text("–", style="dim"))
    row1("Avg Speed",      "avg_speed",      "mph",   1, kmh_to_mph)
    row1("Avg Ride Speed", "avg_ride_speed", "mph",   1, kmh_to_mph)
    row1("Max Speed",      "max_speed",      "mph",   1, kmh_to_mph)
    row1("Avg Energy",     "avg_energy",     "Wh/mi", 1, whkm_to_whmi)
    row1("Min Safety",     "min_safety",     "%",     0)
    row1("Min Rel Load",   "min_rel_load",   "%",     0)
    row1("Max Rel Load",   "max_rel_load",   "%",     0)

    layout["stats"].update(Panel(t1, title="[bold yellow]Session Stats", border_style="yellow"))

    # ── Extremes (Frame 1 battery + Frame 3) ──────────────────────────────────
    t3 = Table(box=box.SIMPLE_HEAVY, expand=True, show_header=True)
    t3.add_column("", width=2, no_wrap=True)
    t3.add_column("Extremes", style="cyan", no_wrap=True)
    t3.add_column("Value", justify="right")

    def row3(label, key, unit="", decimals=0, convert=None):
        val, valid = state[key]
        display = convert(val) if (convert and val is not None) else val
        t3.add_row(valid_dot(valid), label, fmt(display, unit, decimals, valid))

    t3.add_row("", "[bold]Alarms[/bold]", alarm_text(state["alarms_3"]))
    row3("Min Battery", "min_battery", "%")
    row3("Max Battery", "max_battery", "%")
    row3("Min Temp",    "min_temp",    "°F", 1, c_to_f)
    row3("Max Temp",    "max_temp",    "°F", 1, c_to_f)

    layout["extremes"].update(Panel(t3, title="[bold magenta]Extremes", border_style="magenta"))

    # ── Footer ────────────────────────────────────────────────────────────────
    layout["footer"].update(
        Panel(
            Text(f"RAW: {state['raw']}", style="dim", overflow="fold"),
            title="Last Frame",
            border_style="dim",
            box=box.HORIZONTALS,
        )
    )

    return layout


# ── Parsers ───────────────────────────────────────────────────────────────────

def parse_alarms(flags):
    return [name for i, name in enumerate(ALARM_FLAGS) if flags & (1 << i)]

def decode_frame_0_2(data):
    alarm_flags = struct.unpack_from("<H", data, 1)[0]
    vf          = data[5]
    state["alarms_02"]     = parse_alarms(alarm_flags)
    state["distance"]      = (struct.unpack_from("<I", data, 6)[0],         bool(vf & (1 << 0)))
    state["speed"]         = (struct.unpack_from("<H", data, 10)[0] * 0.1,  bool(vf & (1 << 1)))
    state["avg_cell_v"]    = (struct.unpack_from("<H", data, 12)[0] * 0.01, bool(vf & (1 << 2)))
    state["energy_cons"]   = (struct.unpack_from("<h", data, 14)[0] * 0.1,  bool(vf & (1 << 3)))
    state["safety_margin"] = (data[16],                                      bool(vf & (1 << 4)))
    state["relative_load"] = (data[17],                                      bool(vf & (1 << 5)))
    state["battery_level"] = (data[18],                                      bool(vf & (1 << 6)))
    state["temperature"]   = (data[19] - 40,                                 bool(vf & (1 << 7)))

def decode_frame_1(data):
    alarm_flags = struct.unpack_from("<H", data, 1)[0]
    vf          = struct.unpack_from("<H", data, 5)[0]
    state["alarms_1"]       = parse_alarms(alarm_flags)
    state["speedo_range"]   = data[3]
    state["phone_battery"]  = data[4]
    state["avg_speed"]      = (struct.unpack_from("<H", data, 7)[0]  * 0.1, bool(vf & (1 << 0)))
    state["avg_ride_speed"] = (struct.unpack_from("<H", data, 9)[0]  * 0.1, bool(vf & (1 << 1)))
    state["max_speed"]      = (struct.unpack_from("<H", data, 11)[0] * 0.1, bool(vf & (1 << 2)))
    state["avg_energy"]     = (struct.unpack_from("<h", data, 13)[0] * 0.1, bool(vf & (1 << 3)))
    state["min_safety"]     = (data[15],                                     bool(vf & (1 << 4)))
    state["min_rel_load"]   = (data[16],                                     bool(vf & (1 << 5)))
    state["max_rel_load"]   = (data[17],                                     bool(vf & (1 << 6)))
    state["min_battery"]    = (data[18],                                     bool(vf & (1 << 7)))
    state["max_battery"]    = (data[19],                                     bool(vf & (1 << 8)))

def decode_frame_3(data):
    alarm_flags = struct.unpack_from("<H", data, 1)[0]
    vf          = struct.unpack_from("<H", data, 3)[0]
    state["alarms_3"] = parse_alarms(alarm_flags)
    state["min_temp"] = (data[5] - 40, bool(vf & (1 << 0)))
    state["max_temp"] = (data[6] - 40, bool(vf & (1 << 1)))

def handle_notification(sender, data):
    state["raw"] = data.hex()
    state["last_update"] = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    if len(data) < 20:
        return
    frame_idx = data[0]
    if frame_idx in state["frame_counts"]:
        state["frame_counts"][frame_idx] += 1
    if frame_idx in (0, 2):
        decode_frame_0_2(data)
    elif frame_idx == 1:
        decode_frame_1(data)
    elif frame_idx == 3:
        decode_frame_3(data)


# ── Main ──────────────────────────────────────────────────────────────────────

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
        with Live(build_dashboard(), refresh_per_second=10, screen=True) as live:
            while True:
                live.update(build_dashboard())
                await asyncio.sleep(0.05)

if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass