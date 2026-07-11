#!/usr/bin/env python3
"""Capture the ESP32 serial diagnostics to a file for automated debugging.

Auto-reconnects if the USB CDC drops (e.g. during a power cycle), so it can
span a power-cycle + wiggle test. Prints the last few ###DIAG/###RAW/###ASC
blocks plus a peak-bytesRx summary.
"""
import argparse
import re
import sys
import time

import serial  # pyserial


def open_port(port, baud, deadline):
    while time.time() < deadline:
        try:
            return serial.Serial(port, baud, timeout=0.4)
        except Exception:
            time.sleep(0.4)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM3')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--seconds', type=float, default=15.0)
    ap.add_argument('--out', default='diag.txt')
    args = ap.parse_args()

    end = time.time() + args.seconds
    ser = open_port(args.port, args.baud, time.time() + 15)
    if ser is None:
        print('ERROR: could not open %s' % args.port)
        return 2

    lines = []
    with open(args.out, 'w', encoding='utf-8', errors='replace') as f:
        while time.time() < end:
            try:
                raw = ser.readline().decode('utf-8', 'replace')
            except Exception:
                # Port dropped (power cycle?) - try to reopen and keep going.
                try:
                    ser.close()
                except Exception:
                    pass
                ser = open_port(args.port, args.baud, min(end, time.time() + 15))
                if ser is None:
                    break
                continue
            if raw:
                f.write(raw)
                lines.append(raw.rstrip('\r\n'))
    try:
        ser.close()
    except Exception:
        pass

    diag = [l for l in lines if l.startswith('###')]
    peak = 0
    for l in lines:
        m = re.search(r'bytesRx=(\d+)', l)
        if m:
            peak = max(peak, int(m.group(1)))
    print('captured %d lines (%d diagnostic) to %s' % (len(lines), len(diag), args.out))
    print('PEAK bytesRx = %d' % peak)
    print('--- last diagnostic blocks ---')
    for l in diag[-9:]:
        print(l)
    if not diag:
        print('(no ###DIAG lines - last 12 raw lines:)')
        for l in lines[-12:]:
            print(l)
    return 0


if __name__ == '__main__':
    sys.exit(main())
