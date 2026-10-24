"""Talk to a board on a serial port from the PC: show its output, or reset it.

    python extras/tools/board.py tail  --port COM3 --seconds 5     # print what the board prints
    python extras/tools/board.py reset --port COM3                 # hard reset (like after an upload)
    python extras/tools/board.py reset --port COM3 --times 3 --watch 15
        # reset 3 times; after each, wait for "samples/s" lines and report how long recovery took

The reset pulses RTS with DTR low, which restarts ESP32 boards normally (not into the
bootloader). Close the Arduino Serial Monitor first: only one program can open the port.
Needs pyserial (`pip install pyserial`). The port is the board's: COM3, /dev/ttyACM0, /dev/cu.usbmodem…
"""

import argparse
import sys
import time

import serial


def open_port(port, attempts=20):
    for _ in range(attempts):
        try:
            return serial.Serial(port, 115200, timeout=0.2)
        except serial.SerialException:
            time.sleep(0.5)  # the USB port disappears for a moment while the board reboots
    sys.exit(f"Couldn't open {port}. Is the Serial Monitor open, or the board unplugged?")


def read_lines(port, seconds, until=None):
    """Lines printed by the board for `seconds` (or until a line contains `until`)."""
    s = open_port(port)
    s.dtr = True
    out, start, hit = b"", time.time(), None
    while time.time() - start < seconds:
        out += s.read(512)
        if until and hit is None and until.encode() in out:
            hit = time.time() - start
    s.close()
    return [l.strip() for l in out.decode("utf-8", "replace").splitlines() if l.strip()], hit


def reset(port):
    s = open_port(port)
    s.dtr = False
    s.rts = True
    time.sleep(0.15)
    s.rts = False
    s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["tail", "reset"])
    ap.add_argument("--port", required=True, help="the board's serial port, e.g. COM3 or /dev/ttyACM0")
    ap.add_argument("--seconds", type=float, default=5)
    ap.add_argument("--times", type=int, default=1)
    ap.add_argument("--watch", type=float, default=0, help="after each reset, watch this long for 'samples/s'")
    a = ap.parse_args()

    if a.command == "tail":
        lines, _ = read_lines(a.port, a.seconds)
        print("\n".join(lines) or "(no output)")
        return
    for n in range(1, a.times + 1):
        reset(a.port)
        print(f"reset {n} at {time.strftime('%H:%M:%S')}")
        if a.watch:
            time.sleep(1.2)
            lines, hit = read_lines(a.port, a.watch, until="samples/s")
            live = [l for l in lines if "samples/s" in l]
            print(f"  data again after ~{1.2 + hit:.1f} s: {live[-1]}" if hit is not None
                  else f"  no data within {a.watch:g} s; last line: {lines[-1] if lines else '-'}")


if __name__ == "__main__":
    main()
