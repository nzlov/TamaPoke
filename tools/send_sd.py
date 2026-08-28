#!/usr/bin/env python3
"""Envia los paquetes generados a la SD de la placa por USB.

  python3 tools/send_sd.py                  # envia web/packs/*.tui|*.tmove|*.tregion
  python3 tools/send_sd.py --port /dev/cu.usbmodem101
  python3 tools/send_sd.py --ls             # lista lo que hay en la SD
  python3 tools/send_sd.py --only zh-cn      # envia solo nombres coincidentes
"""
import argparse
import glob
import os
import sys
import time
import serial

def find_port():
    ports = glob.glob('/dev/cu.usbmodem*')
    if not ports:
        sys.exit("no encuentro la placa (/dev/cu.usbmodem*)")
    return ports[0]

def wait_line(ser, expect, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline().decode(errors='replace').strip()
        if not line:
            continue
        print(f"  placa: {line}")
        if line == expect:
            return True
        if line == 'ERR' or line.startswith('ERR '):
            return False
    return False

def wait_ack(ser, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline().decode(errors='replace').strip()
        if not line:
            continue
        if line == '#':
            return True
        print(f"  placa: {line}")
        if line == 'ERR' or line.startswith('ERR '):
            return False
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port')
    ap.add_argument('--ls', action='store_true')
    ap.add_argument('--only', help='only send files whose name contains this')
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"puerto {port}")
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(1.5)
    ser.reset_input_buffer()

    if args.ls:
        ser.write(b'LS\n')
        wait_line(ser, 'DONE', 5)
        return

    pack_dir = os.path.join(os.path.dirname(__file__), '..', 'web', 'packs')
    files = sorted(
        path for path in glob.glob(os.path.join(pack_dir, '*'))
        if os.path.splitext(path)[1] in ('.tui', '.tmove', '.tregion')
    )
    if not files:
        sys.exit("no hay paquetes; ejecuta antes tools/gen_data_packs.py")
    if args.only:
        files = [f for f in files if args.only in os.path.basename(f)]
        if not files:
            sys.exit(f"nothing matches --only {args.only}")
        print(f"--only {args.only}: {len(files)} file(s)")

    for path in files:
        size = os.path.getsize(path)
        name = f"packs/{os.path.basename(path)}"
        print(f"-> {name} ({size/1024:.0f} KB)")
        ser.write(f"PUT {name} {size}\n".encode())
        if not wait_line(ser, 'OK', 5):
            print("   la placa no acepto el PUT, sigo con el siguiente")
            continue
        t0 = time.time()
        ok = True
        with open(path, 'rb') as f:
            while chunk := f.read(2048):
                ser.write(chunk)
                if not wait_ack(ser):
                    ok = False
                    break
        if ok and wait_line(ser, 'DONE', 30):
            kbs = size / 1024 / max(0.01, time.time() - t0)
            print(f"   ok ({kbs:.0f} KB/s)")
        else:
            print("   fallo la transferencia")
    print("listo")

if __name__ == '__main__':
    main()
