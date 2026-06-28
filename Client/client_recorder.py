#!/usr/bin/env python3
"""
mouse_recorder.py — STM32 Mouse Jiggler Client (Improved)
===============================================
Records relative mouse movement.
"""

import sys
import time
import argparse
import threading
import queue
import platform
from dataclasses import dataclass
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pip install pyserial")
    sys.exit(1)

try:
    from pynput import mouse as pynput_mouse
    from pynput.mouse import Controller as MouseController
except ImportError:
    print("ERROR: pip install pynput")
    sys.exit(1)

try:
    from rich.console import Console
    from rich.panel import Panel
    console = Console()
except ImportError:
    class Panel:
        @staticmethod
        def fit(text, **kw):
            # Strip basic rich markup tags for plain output
            import re
            return re.sub(r'\[/?[^\]]*\]', '', text)
    class Console:
        def print(self, *a, **kw): print(*a)
    console = Console()

# ── Protocol ───────────────────────────────────────────────────────────────

PROTO_START      = 0xAA
CMD_REPORT       = 0x01
CMD_START        = 0x02
CMD_STOP         = 0x03
CMD_PING         = 0x04
CMD_ERASE        = 0x05
PROTO_ACK        = 0x06
PROTO_NAK        = 0x07

BAUD_RATE        = 115200
ACK_TIMEOUT      = 0.5
IDLE_INTERVAL_MS = 800   # Reduced for better timing fidelity

# ── Frame builder ──────────────────────────────────────────────────────────

def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    xor = cmd ^ len(payload)
    for b in payload:
        xor ^= b
    return bytes([PROTO_START, cmd, len(payload)]) + payload + bytes([xor])

def build_report_frame(buttons: int, x: int, y: int,
                       delay_ms: int) -> bytes:
    """Build an absolute mouse report frame.
    x, y: HID space 0..32767 (mapped from screen pixels).
    """
    buttons  = buttons   & 0x07
    x        = max(0,    min(32767, x))
    y        = max(0,    min(32767, y))
    delay_ms = max(0,    min(60000, delay_ms))
    payload  = bytes([
        buttons,
        0x00,                      # reserved
        x        & 0xFF,
        (x >> 8) & 0xFF,
        y        & 0xFF,
        (y >> 8) & 0xFF,
        delay_ms & 0xFF,
        (delay_ms >> 8) & 0xFF,
    ])
    return build_frame(CMD_REPORT, payload)

# ── Screen size ────────────────────────────────────────────────────────────

def get_screen_size():
    os = platform.system()
    if os == "Windows":
        try:
            import ctypes
            user32 = ctypes.windll.user32
            user32.SetProcessDPIAware()
            return user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
        except Exception:
            pass
    elif os == "Linux":
        try:
            import subprocess
            out = subprocess.check_output(["xrandr", "--current"],
                                          text=True, stderr=subprocess.DEVNULL)
            for line in out.splitlines():
                if "*" in line:
                    for part in line.split():
                        if "x" in part and part[0].isdigit():
                            w, h = part.split("x")
                            return int(w), int(h)
        except Exception:
            pass
    elif os == "Darwin":
        try:
            from AppKit import NSScreen
            f = NSScreen.mainScreen().frame()
            return int(f.size.width), int(f.size.height)
        except Exception:
            pass
    return None, None

# ── Serial comm ────────────────────────────────────────────────────────────

class STM32Comm:
    def __init__(self, port: str, baud: int = BAUD_RATE):
        self.port  = port
        self.baud  = baud
        self.ser   = None
        self._lock = threading.Lock()

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=ACK_TIMEOUT)
            time.sleep(0.1)
            return True
        except serial.SerialException as e:
            console.print(f"[red]{e}[/red]")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _send(self, frame: bytes, timeout: float = ACK_TIMEOUT) -> bool:
        with self._lock:
            if not self.ser: return False
            try:
                self.ser.write(frame)
                self.ser.flush()
                deadline = time.time() + timeout
                while time.time() < deadline:
                    b = self.ser.read(1)
                    if b and b[0] == PROTO_ACK: return True
                    if b and b[0] == PROTO_NAK: return False
                return False
            except serial.SerialException:
                return False

    def ping(self) -> bool:
        for _ in range(5):
            if self._send(build_frame(CMD_PING)): return True
            time.sleep(0.2)
        return False

    def send_erase(self) -> bool:
        # STM32 ACKs immediately (sets a flag), then erases in the main loop.
        # MX25L6473E: 2044 sectors × ~60ms = ~2 minutes total.
        # After the ACK we wait, then ping repeatedly until the STM32
        # responds again — that signals erase is complete.
        if not self._send(build_frame(CMD_ERASE), timeout=2.0):
            return False
        console.print("[yellow]Erasing flash — LED solid on STM32 (~30s)...[/yellow]")
        deadline = time.time() + 120.0   # 2 min absolute max (chip spec: 80s)
        dots = 0
        time.sleep(2.0)   # give STM32 time to start erasing before we ping
        while time.time() < deadline:
            time.sleep(5.0)
            dots += 1
            print(".", end="", flush=True)
            if self.ping():
                print("")
                console.print("[green]✓ Erase complete[/green]")
                return True
        return False

    def send_start(self) -> bool:
        return self._send(build_frame(CMD_START))

    def send_stop(self) -> bool:
        return self._send(build_frame(CMD_STOP))

    def send_report(self, buttons: int, x: int, y: int,
                    delay_ms: int) -> bool:
        return self._send(build_report_frame(buttons, x, y, delay_ms))

# ── Mouse listener (Improved) ──────────────────────────────────────────────

@dataclass
class MouseEvent:
    buttons:   int
    x:         int     # absolute screen pixel X
    y:         int     # absolute screen pixel Y
    fire_time: float   # time.monotonic() when pynput fired this event

class MouseRecorder:
    """Records absolute mouse positions.
    No accumulation, no drift — every event carries the exact screen coordinate.
    """
    def __init__(self, screen_w: int, screen_h: int):
        self._queue    = queue.Queue()
        self._buttons  = 0
        self._screen_w = screen_w
        self._screen_h = screen_h
        self._listener = None

    def start(self):
        self._listener = pynput_mouse.Listener(
            on_move   = self._on_move,
            on_click  = self._on_click,
        )
        self._listener.start()

    def stop(self):
        if self._listener: self._listener.stop()

    def get_event(self, timeout=0.01) -> Optional[MouseEvent]:
        try:    return self._queue.get(timeout=timeout)
        except queue.Empty: return None

    def _screen_to_hid(self, x: float, y: float):
        """Map screen pixel coords to HID absolute space (0..32767)."""
        hx = int(x / self._screen_w  * 32767)
        hy = int(y / self._screen_h * 32767)
        return max(0, min(32767, hx)), max(0, min(32767, hy))

    def _on_move(self, x, y):
        now = time.monotonic()
        hx, hy = self._screen_to_hid(x, y)
        self._queue.put(MouseEvent(self._buttons, hx, hy, now))

    def _on_click(self, x, y, button, pressed):
        now = time.monotonic()
        btn_map = {
            pynput_mouse.Button.left:   0x01,
            pynput_mouse.Button.right:  0x02,
            pynput_mouse.Button.middle: 0x04,
        }
        mask = btn_map.get(button, 0)
        self._buttons = (self._buttons | mask) if pressed else (self._buttons & ~mask)
        hx, hy = self._screen_to_hid(x, y)
        self._queue.put(MouseEvent(self._buttons, hx, hy, now))

# ── Recording session ──────────────────────────────────────────────────────

def move_to_center(screen_w: int, screen_h: int, label: str = ""):
    mouse = MouseController()
    cx, cy = screen_w // 2, screen_h // 2
    if label:
        console.print(f"[dim]{label}[/dim]")
    mouse.position = (cx, cy)
    time.sleep(0.3)
    # Nudge to confirm the OS accepted the warp (some compositors need this)
    mouse.position = (cx, cy)

def run_recording(comm: STM32Comm, duration_s: float,
                  no_erase: bool, screen_w: int, screen_h: int,
                  dump: bool = False):

    console.print(Panel.fit(
        "[bold cyan]STM32 Mouse Jiggler — Recording[/bold cyan]\n"
        f"Duration: [yellow]{duration_s:.0f}s[/yellow]  "
        f"Port: [yellow]{comm.port}[/yellow]",
        border_style="cyan"
    ))

    console.print("[dim]Pinging STM32...[/dim]")
    if not comm.ping():
        console.print("[red]✗ No response from STM32[/red]")
        sys.exit(1)
    console.print("[green]✓ STM32 connected[/green]")

    if not no_erase:
        console.print("[dim]Erasing flash...[/dim]")
        if not comm.send_erase():
            console.print("[red]✗ Erase failed[/red]")
            sys.exit(1)
        console.print("[green]✓ Flash erased[/green]")

    cx, cy = screen_w // 2, screen_h // 2
    console.print(f"[dim]Parking cursor at screen center ({cx}, {cy})...[/dim]")
    move_to_center(screen_w, screen_h)
    console.print(f"[dim]Center parked. Recording starts in 3...[/dim]")
    time.sleep(1.0)
    console.print("[dim]2...[/dim]")
    time.sleep(1.0)
    console.print("[dim]1...[/dim]")
    time.sleep(1.0)

    if not comm.send_start():
        console.print("[red]✗ START not acknowledged[/red]")
        sys.exit(1)
    console.print(f"[green]✓ Recording — move your mouse! (started at center {cx}, {cy})[/green]\n")

    recorder      = MouseRecorder(screen_w, screen_h)
    recorder.start()


    sent          = 0
    failed        = 0
    flash_full    = False
    deadline      = time.monotonic() + duration_s
    last_print    = time.monotonic()
    last_idle     = time.monotonic()
    last_sent_ts  = time.monotonic()   # tracks when last event was actually transmitted

    try:
        while time.monotonic() < deadline and not flash_full:
            now = time.monotonic()
            if now - last_print >= 1.0:
                last_print = now
                elapsed = now - (deadline - duration_s)
                pct = min(100.0, elapsed / duration_s * 100)
                rem = max(0.0, deadline - now)
                filled = int(30 * pct / 100)
                bar = "█" * filled + "░" * (30 - filled)
                print(f"\r  [{bar}] {pct:5.1f}%  {rem:5.0f}s left  "
                      f"sent:{sent}  fail:{failed}    ", end="", flush=True)

            event = recorder.get_event(timeout=0.01)

            if event is not None:
                last_idle = time.monotonic()
                # Compute delay from when the last event was actually transmitted,
                # not from when pynput fired the previous callback. This avoids
                # storing artificially short delay_ms values when the queue was
                # backed up, which would cause playback to replay too fast.
                delay_ms = max(0, int((event.fire_time - last_sent_ts) * 1000))
                delay_ms = min(delay_ms, 60000)
                ok = comm.send_report(event.buttons, event.x, event.y, delay_ms)
                if ok:
                    sent += 1
                    last_sent_ts = time.monotonic()
                    if dump:
                        print(f"  SENT x={event.x:5d} y={event.y:5d} "
                              f"delay={delay_ms:5d}ms")
                else:
                    failed += 1
                    if failed > 20:
                        console.print("\n[red]Too many failures — flash may be full[/red]")
                        flash_full = True
                        break
            else:
                # Absolute mouse: no idle heartbeat needed.
                # The STM32 RLE will merge identical consecutive positions anyway.
                pass

    except KeyboardInterrupt:
        print("\n")
        console.print("[yellow]Stopped early[/yellow]")

    finally:
        recorder.stop()

    # Drain any events that pynput captured but the main loop didn't get to send.
    # This happens during fast movements where events queue faster than serial can
    # drain them. Without this, those deltas are lost and the playback position
    # drifts from the recorded end position.
    drain_count = 0
    while True:
        event = recorder.get_event(timeout=0.005)
        if event is None:
            break
        delay_ms = max(0, min(60000, int((event.fire_time - last_sent_ts) * 1000)))
        ok = comm.send_report(event.buttons, event.x, event.y, delay_ms)
        if ok:
            sent += 1
            drain_count += 1
            if dump:
                print(f"  DRAIN x={event.x:5d} y={event.y:5d} "
                      f"delay={delay_ms:5d}ms")
        else:
            failed += 1
    if drain_count:
        console.print(f"[dim]Drained {drain_count} queued events after loop exit.[/dim]")

    print("\n")
    console.print("[dim]Sending STOP...[/dim]")
    comm.send_stop()



    # Return to center so you can see the offset: if the cursor does NOT
    # land exactly at center, the delta from center is your playback error.
    console.print(f"[dim]Returning cursor to screen center ({cx}, {cy})...[/dim]")
    move_to_center(screen_w, screen_h, label="")
    console.print(
        f"[bold yellow]► Cursor is now at center ({cx}, {cy}).[/bold yellow]\n"
        f"  During playback, watch where it ends up relative to this point.\n"
        f"  Offset direction and magnitude = your residual replay error."
    )

    console.print(Panel.fit(
        f"[bold green]Recording saved![/bold green]\n"
        f"Sent:   [cyan]{sent}[/cyan]\n"
        f"Failed: [red]{failed}[/red]",
        border_style="green"
    ))

# ── Entry point ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="STM32 Mouse Jiggler — Recording Client",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--port", "-p", type=str, required=False)
    parser.add_argument("--baud", "-b", type=int, default=BAUD_RATE)
    parser.add_argument("--duration", "-d", type=float, default=60.0)
    parser.add_argument("--width", "-W", type=int)
    parser.add_argument("--height", "-H", type=int)
    parser.add_argument("--list-ports", "-l", action="store_true")
    parser.add_argument("--no-erase", action="store_true")
    parser.add_argument("--dump", "-D", action="store_true",
                        help="Print every event sent (dx, dy, delay_ms) and running delta sum")
    args = parser.parse_args()

    if args.list_ports:
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            print(f"  {p.device:<10} {p.description}")
        return

    if not args.port:
        print("ERROR: --port required. Use --list-ports")
        sys.exit(1)

    screen_w = args.width
    screen_h = args.height
    if not screen_w or not screen_h:
        screen_w, screen_h = get_screen_size()
    if not screen_w or not screen_h:
        console.print("[red]Could not detect screen resolution.[/red]")
        sys.exit(1)

    console.print(f"[dim]Screen: {screen_w}x{screen_h}[/dim]")

    comm = STM32Comm(args.port, args.baud)
    if not comm.connect():
        sys.exit(1)

    try:
        run_recording(comm, args.duration, args.no_erase, screen_w, screen_h, args.dump)
    finally:
        comm.disconnect()

if __name__ == "__main__":
    main()
