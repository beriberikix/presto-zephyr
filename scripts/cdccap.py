#!/usr/bin/env python3
"""Read a CDC ACM console with DTR/RTS explicitly asserted.

Zephyr's CDC ACM UART discards output until the host signals it is listening,
so a terminal that opens the port without raising DTR sees perfect silence and
a live board look identical to a hung one.
"""
import fcntl, os, struct, sys, termios, time

TIOCMBIS = 0x8004746C
TIOCM_DTR, TIOCM_RTS = 0x002, 0x004

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0            # iflag, oflag, lflag: raw
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    fcntl.ioctl(fd, TIOCMBIS, struct.pack("I", TIOCM_DTR | TIOCM_RTS))
    print(f"DTR+RTS asserted on {port}, reading {secs:.0f}s", flush=True)

    end, total = time.time() + secs, 0
    while time.time() < end:
        try:
            data = os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.05)
            continue
        if data:
            total += len(data)
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
    print(f"\n=== {total} bytes ===", flush=True)
finally:
    os.close(fd)
