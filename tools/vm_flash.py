#!/usr/bin/env python3
"""Flash firmware to ESP32-P4 board."""

import paramiko
import sys

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

FLASH_CMD = r"""
echo "=== Flashing firmware to ESP32-P4 ==="

cd /home/max/openvela-p4-integration/nuttx

# Check if nuttx.bin exists
if [ ! -f nuttx.bin ]; then
    echo "ERROR: nuttx.bin not found"
    exit 1
fi

echo "=== File info ==="
ls -la nuttx.bin
sha256sum nuttx.bin

echo "=== Checking serial port ==="
ls -la /dev/ttyACM0 2>/dev/null || echo "No /dev/ttyACM0"

echo "=== Flashing ==="
/home/max/.local/bin/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 write-flash 0x2000 nuttx.bin 2>&1

echo "=== Verifying ==="
/home/max/.local/bin/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 verify-flash 0x2000 nuttx.bin 2>&1

echo "=== Flash complete ==="
"""

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        print("Flashing firmware...")
        stdin, stdout, stderr = client.exec_command(FLASH_CMD, timeout=300)

        # Print stdout in real-time
        for line in iter(stdout.readline, ""):
            print(line, end="")

        # Print stderr
        err = stderr.read().decode()
        if err:
            print("STDERR:", err)

        exit_status = stdout.channel.recv_exit_status()
        print(f"\nCommand exit status: {exit_status}")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    finally:
        client.close()

    return 0

if __name__ == "__main__":
    sys.exit(main())
