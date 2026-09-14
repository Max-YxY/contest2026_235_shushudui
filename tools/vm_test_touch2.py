#!/usr/bin/env python3
"""Test touch detection on ESP32-P4 board."""

import paramiko
import sys
import time

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

TEST_CMD = r"""
echo "=== Testing touch detection ==="

# Kill any existing screen sessions
screen -X -S esp32p4 quit 2>/dev/null || true
sleep 1

# Check serial port
ls -la /dev/ttyACM0 2>/dev/null || echo "No /dev/ttyACM0"

# Create a script to send commands and capture output
cat > /tmp/test_touch2.sh << 'SCRIPT'
#!/bin/bash
# Open serial port
exec 3>/dev/ttyACM0

# Send newline to wake up
echo "" >&3
sleep 1

# First run dsi-ui-live to set up the display
echo "camera_diag --dsi-ui-live" >&3
sleep 3

# Then run touch-ui-live
echo "camera_diag --touch-ui-live" >&3
sleep 2

echo "========================================="
echo "Please touch the screen now!"
echo "Expected behavior:"
echo "  1. Green circle appears when pressing"
echo "  2. Circle disappears when releasing"
echo "  3. Serial shows 'touch press x=... y=...'"
echo "========================================="
echo ""

# Read output for 30 seconds to capture touch events
timeout 30 cat /dev/ttyACM0 2>/dev/null

# Close port
exec 3>&-
SCRIPT

chmod +x /tmp/test_touch2.sh

# Run the test
echo "Running touch test..."
echo "Please touch the screen when instructed!"
echo ""
/tmp/test_touch2.sh 2>&1

echo "=== Test complete ==="
echo ""
echo "Please verify on the physical screen:"
echo "1. Green circle appears when you touch the screen"
echo "2. Circle disappears when you release"
echo "3. No black screen or freezing"
"""

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        print("Testing touch detection...")
        print("Please touch the screen when instructed!")
        print("")

        stdin, stdout, stderr = client.exec_command(TEST_CMD, timeout=120)

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
