#!/usr/bin/env python3
"""Debug DMA state on ESP32-P4 board."""

import paramiko
import sys
import time

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

DEBUG_CMD = r"""
echo "=== Debug DMA state ==="

# Kill any existing screen sessions
screen -X -S esp32p4 quit 2>/dev/null || true
sleep 1

# Check serial port
ls -la /dev/ttyACM0 2>/dev/null || echo "No /dev/ttyACM0"

# Create a script to send commands and capture output
cat > /tmp/debug_dma.sh << 'SCRIPT'
#!/bin/bash
# Open serial port
exec 3>/dev/ttyACM0

# Send newline to wake up
echo "" >&3
sleep 1

# First run dsi-ui-live to set up the display
echo "camera_diag --dsi-ui-live" >&3
sleep 3

# Check DMA state
echo "=== DMA state after initial start ==="
echo "" >&3
sleep 1

# Wait a few seconds to see if DMA re-arms
sleep 5

# Check if DMA is still running
echo "=== DMA state after 5 seconds ==="
echo "" >&3
sleep 1

# Close port
exec 3>&-
SCRIPT

chmod +x /tmp/debug_dma.sh

# Run the test
echo "Running DMA debug..."
/tmp/debug_dma.sh 2>&1

echo "=== Debug complete ==="
"""

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        print("Debugging DMA state...")
        stdin, stdout, stderr = client.exec_command(DEBUG_CMD, timeout=60)

        # Print stdout
        output = stdout.read().decode()
        print(output)

        # Print stderr
        err = stderr.read().decode()
        if err:
            print("STDERR:", err)

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    finally:
        client.close()

    return 0

if __name__ == "__main__":
    sys.exit(main())
