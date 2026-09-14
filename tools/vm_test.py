#!/usr/bin/env python3
"""Test dynamic refresh on ESP32-P4 board."""

import paramiko
import sys
import time

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

TEST_CMD = r"""
echo "=== Testing dynamic refresh ==="

# Kill any existing screen sessions
screen -X -S esp32p4 quit 2>/dev/null || true
sleep 1

# Check serial port
ls -la /dev/ttyACM0 2>/dev/null || echo "No /dev/ttyACM0"

# Create a script to send commands and capture output
cat > /tmp/test_dynamic.sh << 'SCRIPT'
#!/bin/bash
# Open serial port
exec 3>/dev/ttyACM0

# Send newline to wake up
echo "" >&3
sleep 1

# Send the command
echo "camera_diag --dsi-ui-live" >&3
sleep 3

# Read output for a few seconds
timeout 5 cat /dev/ttyACM0 2>/dev/null

# Close port
exec 3>&-
SCRIPT

chmod +x /tmp/test_dynamic.sh

# Run the test
echo "Running test..."
/tmp/test_dynamic.sh 2>&1

echo "=== Test complete ==="
echo ""
echo "Please check the physical screen:"
echo "1. First frame UI should display (dark blue background, white text)"
echo "2. No black screen or cyan screen"
echo "3. No error messages in output above"
"""

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        print("Testing dynamic refresh...")
        stdin, stdout, stderr = client.exec_command(TEST_CMD, timeout=60)

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
